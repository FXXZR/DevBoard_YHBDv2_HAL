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
#include "w5500_drv.h"
#include "w5500_net.h"
#include "bsp_systick.h"

/* 仅 D2 的裸读基准用 (WIZCHIP_READ_BUF / WIZCHIP_RXBUF_BLOCK), 量完即删 */
#include "w5500.h"

#include "FreeRTOS.h"
#include "task.h"

#include <limits.h>     /* ULONG_MAX */
#include <string.h>     /* memcmp */

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

/* D2 冒烟只跑一次 (脚手架, D5 删) */
static int     s_smoke_done;

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
 * D2 冒烟测试 —— 临时脚手架, D5 接 DHCP/DNS 时由业务流程取代
 *
 * 连 PC 上的 tools/net_peer.py (TCP server, echo 模式), 收发 2KB 并核对内容,
 * 顺带量出单次收发的实际耗时 —— 那是 W5500_SPI_TIMEOUT_MS 取值的依据
 * (18MHz 下 2KB 纯线速 0.91ms)。
 *
 * 量得准的前提: 发之前发送缓冲是空的、收之前数据已经到齐 —— 这样封装里的
 * 轮询首轮就满足条件, 量到的接近纯 SPI 搬运耗时, 不含等待。
 * ================================================================ */

enum {
    NET_TEST_SOCK       = 0,        /* 专用 socket 号 —— DHCP/DNS 各要一个, 别撞 */
    NET_TEST_PEER_PORT  = 5000,     /* net_peer.py 的默认端口 */
    NET_TEST_ROUNDS     = 4,
    NET_TEST_BYTES      = 2048,     /* 用满 socket 缓冲, 逼出最坏耗时 */
    NET_TEST_TIMEOUT_MS = 3000,
    /* 轮间隔: 把测量铺到时间轴上。disp 任务开机后有一段长时间忙等
     * (触摸校准, 每点最长 10s), 间隔取 5s 才能同时采到"被抢"和"没人抢"
     * 两种情形 —— 单看一次数字分不清是搬运慢还是任务没拿到 CPU。 */
    NET_TEST_GAP_MS     = 5000,
};

/* 对端 = PC。直连场景里 PC 就是网关 (见 w5500_net.c 的参数表) */
static const uint8_t kTestPeerIp[4] = { 192, 168, 1, 1 };

/* ⚠️ 收发缓冲必须 static: 2+2KB 放栈上会直接把 512 字的任务栈撑爆 */
static uint8_t s_test_tx[NET_TEST_BYTES];
static uint8_t s_test_rx[NET_TEST_BYTES];

/**
 * @brief 2KB 裸读基准 —— 只量 SPI 搬运本身
 *
 * 直接读 socket 0 的 RX 缓冲内存, 不碰协议栈、不等数据、不发命令 ——
 * 排除掉一切"等"的成分后, 剩下的就是纯搬运耗时。与冒烟里量到的
 * 收发耗时一比, 就知道多出来的时间是花在搬运上还是花在等调度上。
 */
static void net_burst_bench(void)
{
    LOG_I("--- 2KB 裸读基准 ---");

    for (int i = 0; i < 5; i++) {
        uint32_t t0 = BSP_SYSTICK_GetUs();

        WIZCHIP_READ_BUF(((uint32_t)0 << 8) + (WIZCHIP_RXBUF_BLOCK(NET_TEST_SOCK) << 3),
                         s_test_rx, NET_TEST_BYTES);

        LOG_I("  #%d: %u us", i, (unsigned)(BSP_SYSTICK_GetUs() - t0));
    }
}

static void net_smoke_test(void)
{
    net_burst_bench();      /* 先量一次基线 */

    LOG_I("--- 冒烟: 连 %u.%u.%u.%u:%u, %u 轮 x %u B ---",
          kTestPeerIp[0], kTestPeerIp[1], kTestPeerIp[2], kTestPeerIp[3],
          NET_TEST_PEER_PORT, NET_TEST_ROUNDS, NET_TEST_BYTES);

    for (int round = 1; round <= NET_TEST_ROUNDS; round++) {
        uint16_t sent = 0, got = 0;
        uint32_t us_send, us_recv;
        W5500_Sock_Status_t st;

        for (int i = 0; i < NET_TEST_BYTES; i++) {
            s_test_tx[i] = (uint8_t)(i + round);    /* 每轮不同, 便于核对 */
        }
        memset(s_test_rx, 0, sizeof(s_test_rx));

        if (W5500_Sock_Open(NET_TEST_SOCK, 0) != W5500_SOCK_OK) {
            LOG_E("#%d Open 失败", round);
            return;
        }
        if (W5500_Sock_Connect(NET_TEST_SOCK, kTestPeerIp, NET_TEST_PEER_PORT,
                               NET_TEST_TIMEOUT_MS) != W5500_SOCK_OK) {
            LOG_E("#%d 连接失败 —— PC 上的 net_peer.py 起来了吗?", round);
            return;                     /* Connect 失败时已自行 Close */
        }

        us_send = BSP_SYSTICK_GetUs();
        st = W5500_Sock_Send(NET_TEST_SOCK, s_test_tx, NET_TEST_BYTES,
                             NET_TEST_TIMEOUT_MS, &sent);
        us_send = BSP_SYSTICK_GetUs() - us_send;

        if (st != W5500_SOCK_OK) {
            /* 不早退的话, 下面会表现成"接收超时", 分不清是哪一头出的问题 */
            LOG_E("#%d 发送失败: %s (发出 %u B)", round,
                  W5500_Sock_StatusStr(st), sent);
            W5500_Sock_Close(NET_TEST_SOCK);
            return;
        }

        /* 隔开再收: 让回包先到, Recv 首轮就能取走, 量到的才是搬运耗时 */
        vTaskDelay(pdMS_TO_TICKS(50));

        us_recv = BSP_SYSTICK_GetUs();
        st = W5500_Sock_Recv(NET_TEST_SOCK, s_test_rx, NET_TEST_BYTES,
                             NET_TEST_TIMEOUT_MS, &got);
        us_recv = BSP_SYSTICK_GetUs() - us_recv;

        /* 只比对"收到的那些字节": 一次短读不等于内容错 (TCP 是流) */
        LOG_I("#%d 发 %u B / %u us, 收 %u B / %u us (%s), 内容%s",
              round, sent, (unsigned)us_send, got, (unsigned)us_recv,
              W5500_Sock_StatusStr(st),
              (got == 0) ? "未收到"
                         : (memcmp(s_test_tx, s_test_rx, got) == 0 ? "一致" : "不一致"));

        W5500_Sock_Close(NET_TEST_SOCK);
        vTaskDelay(pdMS_TO_TICKS(NET_TEST_GAP_MS));
    }

    net_burst_bench();      /* 再量一次: 与开机那次对比, 看抢占的影响 */
    LOG_I("--- 冒烟结束 ---");
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

            /* D2 脚手架: 链路一起来就跑一次冒烟, 只跑一次 */
            if (!s_smoke_done && (s_phy_last & W5500_PHY_LNK) != 0) {
                s_smoke_done = 1;
                net_smoke_test();
            }
        }
    }
}
