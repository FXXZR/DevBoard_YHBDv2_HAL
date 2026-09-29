/**
 * @file    task_net.c
 * @brief   网络任务 —— 单任务持有 W5500 socket 层
 *
 * 单任务而不是"一个协议一个任务": ioLibrary 的 socket.c 有静态状态且无锁,
 * 8 个 socket 号是稀缺资源, 而 DHCP/DNS/HTTP 的时序都是"发请求→等回应"的
 * 阻塞式 —— 它们天然属于同一个事件循环。多任务进 socket 层 = socket 状态机
 * 交错。
 *
 * 三个唤醒源, 但只有【一个阻塞点】(xTaskNotifyWait):
 *
 *   ① 周期服务        —— 阻塞调用的超时, 不需要谁通知
 *   ② 中断事件        —— ISR 置【事件位】; 位是累积的, 任务在处理上一个
 *                        事件时又来的事件不会丢
 *   ③ 其他任务的请求   —— 【队列】传负载, 置位只用来唤醒
 *
 * ⚠️ 事件位不得承担 ③: 通知值只有一个 32 位数, 塞不下"哪个 socket 的哪段
 *    数据"。请求必须走队列 —— 否则就退化成"全局变量传参 + 置位通知"。
 *    队列等 D2 出现第一个真实请求者时接入, 不预造接口。
 *
 * 关于总线互斥 —— 本任务【不需要】自己加锁:
 *   SPI2 总线锁由 Services/devices/w5500_port.c 的片选回调负责, 粒度是一次
 *   完整的寄存器事务 (select…deselect)。所以任务层直接调 W5500_* 即可,
 *   加锁是驱动内部的事。
 *
 *   ⚠️ 反过来说: 千万不要在这里再 SpiBus_Lock 一次 —— FreeRTOS 的普通互斥量
 *      不可递归, 重复获取会阻塞到超时再返回 -1 (触发断言通道), 传
 *      SPI_BUS_WAIT_FOREVER 才是真死锁。锁下沉到 port 层的好处正是: 调用方
 *      根本不需要知道有锁。
 */

#define LOG_TAG "W5500"
#include "log.h"

#include "task_net.h"
#include "net_selftest.h"
#include "w5500_drv.h"

#include "FreeRTOS.h"
#include "task.h"

#include <limits.h>     /* ULONG_MAX */

/* ================================================================
 * 配置
 * ================================================================ */

/* 服务周期, 也是阻塞等待的超时。当前只做 PHY 链路轮询 —— 1Hz 足够观察自协商
 * 过程 (开销: 每周期一帧 4 字节 SPI 事务 + 一次任务唤醒)。
 * D5 接 DHCP 后要跟着 DHCP_run 的节奏收紧。 */
enum { TASK_NET_TICK_MS = 1000 };

/* 事件位 —— 一位一个来源, 生产者写在注释里 */
enum {
    /* 由 W5500 的 INT 引脚 (PG8/EXTI8) 下降沿置位, D4 接。
     * ISR 只置位不读数据: 读 socket 缓冲要走 SPI, 中断上下文里耗不起,
     * 而且 bsp_spi 里有 __get_IPSR() 断言挡着。 */
    TASK_NET_EVT_SOCKET = 1u << 0,
};

/* ================================================================
 * 状态
 * ================================================================ */

/* 上一次的 PHYCFGR —— 只在值变化时打日志 */
static uint8_t s_phy_last;
static int     s_phy_first = 1;

/* 自检只跑一次 (脚手架, 见 net_selftest.c) */
static int     s_selftest_done;

/* ================================================================
 * 周期服务
 * ================================================================ */

/**
 * @brief 一次周期服务 —— 当前只有 PHY 链路状态跟踪
 *
 * 为什么需要它: 启动自检在复位后读 PHYCFGR, 实测那时链路还没建立 ——
 * 以太网自协商要 1~2 秒才完成。周期性读一次, 值一变就打出来, 插拔网线
 * 也能立刻看到; 后续 "网线掉了要触发上层处理" 也挂在这里。
 */
static void net_tick(void)
{
    uint8_t phy = W5500_Drv_ReadPhyCfg();

    if (s_phy_first) {
        LOG_I("PHYCFGR 初始 = 0x%02X", phy);
        s_phy_last  = phy;
        s_phy_first = 0;
    } else if (phy != s_phy_last) {
        LOG_I("PHYCFGR 变化: 0x%02X -> 0x%02X", s_phy_last, phy);
        s_phy_last = phy;
    }
}

/* ================================================================
 * 任务
 * ================================================================ */

void vNetTask(void *pvParameters)
{
    (void) pvParameters;

    TickType_t last_tick = xTaskGetTickCount();

    for (;;) {
        uint32_t   evt = 0;
        TickType_t now;

        /* ★ 唯一的阻塞点。
         *
         * 两个 clear 参数: 进入时清 0 (不动已经累积的位), 退出时全清 ——
         * 于是读到的 evt 就是"上一轮之后发生过什么"。读值与清位在临界区里
         * 完成, 所以清位那一刻新来的事件不会被吞掉。 */
        (void) xTaskNotifyWait(0, ULONG_MAX, &evt, pdMS_TO_TICKS(TASK_NET_TICK_MS));

        /* ② 中断事件 (生产者: D4 的 EXTI ISR) */
        if ((evt & TASK_NET_EVT_SOCKET) != 0) {
            /* D4: 读 IR / Sn_IR 判断是哪个 socket, 再 recv 取走数据 */
        }

        /* ① 周期服务。按时间戳判定, 而不是"超时了就跑" —— 突发事件会提前
         * 唤醒本任务, 那时不该顺手把每个服务都跑一遍 (D5 起是 DHCP_run 这种
         * 带网络交互的)。 */
        now = xTaskGetTickCount();
        if ((now - last_tick) >= pdMS_TO_TICKS(TASK_NET_TICK_MS)) {
            last_tick = now;
            net_tick();

            /* W2 自检脚手架: 链路一起来就跑一次 (见 net_selftest.c, D5 删) */
            if (!s_selftest_done && (s_phy_last & W5500_PHY_LNK) != 0) {
                s_selftest_done = 1;
                NetSelfTest_Run();
            }
        }
    }
}
