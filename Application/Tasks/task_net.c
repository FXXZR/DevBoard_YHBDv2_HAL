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
 *   ② 中断事件        —— W5500 的 INT 引脚 (PG8) → EXTI → ISR 置【事件位】
 *                        (D4 已接: 生产者是 net_int_isr)
 *   ③ 其他任务的请求   —— 【队列】传负载, 置位只用来唤醒
 *
 * ⚠️ 事件位不得承担 ③: 通知值只有一个 32 位数, 塞不下"哪个 socket 的哪段
 *    数据"。请求必须走队列 —— 否则就退化成"全局变量传参 + 置位通知"。
 *    队列等 D2 出现第一个真实请求者时接入, 不预造接口。
 *
 * ★ 中断这一路的分工 (BSP 不能反向依赖 Application, 所以走回调注册):
 *
 *        W5500 芯片                 BSP                      Application
 *        ──────────                 ───                      ───────────
 *        有事 → INT 拉低   →  EXTI ISR 清除 EXTI 挂起位   →  回调 net_int_isr
 *                            不认识 FreeRTOS,               记账 + 置事件位
 *                            只调注册进来的回调             (xTaskNotifyFromISR)
 *
 *    BSP 侧只认一个 void(*)(void) 函数指针, 与 bsp_usart 的 rx_idle 回调是
 *    同一个套路 —— 于是 BSP 不需要认识 FreeRTOS, 更不需要认识本任务。
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
#include "net_dhcptest.h"
#include "w5500_drv.h"
#include "w5500_net.h"
#include "bsp_w5500_hw.h"

#include "FreeRTOS.h"
#include "task.h"

#include <limits.h>     /* ULONG_MAX */

/* ================================================================
 * 网络参数 —— 部署数据, 放应用层
 * 主要用于静态IP测试阶段，方便在应用层直接改，后续可以搭配存储持久化。
 * ================================================================ */

static const wiz_NetInfo kNetCfg = {
    .mac  = { 0x02, 0x08, 0xDC, 0x01, 0x02, 0x03 },
    .ip   = { 192, 168, 1, 100 },
    .sn   = { 255, 255, 255, 0 },
    .gw   = { 192, 168, 1, 1 },
    .dns  = { 192, 168, 1, 1 },     /* 静态路径下的 DNS */
    .dhcp = NETINFO_STATIC,         /* 默认仍走静态 —— 动态地址那条路见 net_dhcptest.c */
};

/* ================================================================
 * 配置
 * ================================================================ */

/* 服务周期, 也是阻塞等待的超时。当前只做 PHY 链路轮询 —— 1Hz 足够观察自协商
 * 过程 (开销: 每周期一帧 4 字节 SPI 事务 + 一次任务唤醒)。
 *
 * ⚠️ 这里【不】挂 DHCP_run: 它整段阻塞 (ACK 之后还要等一轮 ARP 冲突检测),
 *    塞进周期服务就等于把链路监控和收包一起停掉。D5 把它放在链路起来后的
 *    一次性阻塞段里 —— 见 net_dhcptest.c。 */
enum { TASK_NET_TICK_MS = 1000 };

/* W5500 上电失败后的重试间隔 (每次重试内含约 100ms 复位等待) */
enum { TASK_NET_RETRY_MS = 1000 };

/* INT 线没放掉时的重看间隔 —— 见 vNetTask 里取 wait 那段的说明 */
enum { TASK_NET_INT_RETRY_MS = 2 };

/* 一次事件处理最多来回搬几趟。收包洪水时不能把主循环饿死: 周期服务
 * (链路监控 / D5 的 DHCP_run) 也得有机会跑。搬不完不要紧 —— 线一直低,
 * 收尾会把等待超时改成 2ms, 马上回来接着搬。 */
enum { TASK_NET_SOCK_ROUNDS = 8 };

/* 事件位 —— 一位一个来源, 生产者写在注释里 */
enum {
    /* 生产者: net_int_isr (W5500 INT 引脚 PG8 → EXTI8 下降沿)。
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
static int     s_intdemo_started;

/* 任务自己的句柄 —— ISR 要拿它置事件位。在任务入口登记, 之后只读。 */
static TaskHandle_t s_task;

/* ================================================================
 * ISR → 任务
 * ================================================================ */

/**
 * @brief W5500 INT 引脚的 ISR 回调 (由 bsp_w5500_hw 从 EXTI9_5 里调进来)
 *
 * 只做两件事: 记一笔账 (D4 验证要拿它和收包数对比) 和置事件位。
 *
 * 为什么用【事件位】而不是计数信号量: 位是累积的, 任务在处理上一次事件时
 * 又来的事件不会丢; 而且我们并不关心"来过几次中断" —— 芯片缓冲里有几包,
 * 是任务自己去读 SIR / Sn_IR 才知道的, 中断只负责说一声"有事"。
 */
static void net_int_isr(void)
{
    BaseType_t woken = pdFALSE;

    NetIntTest_OnIsr();         /* 脚手架: D4 的 ISR 计数与时间戳, D5 删 */

    xTaskNotifyFromISR(s_task, TASK_NET_EVT_SOCKET, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ================================================================
 * 中断事件的处理
 * ================================================================ */

/**
 * @brief 一个 socket 的事件 —— 产品的业务处理就挂在这里
 * @note  ir 只会是 W5500_SOCK_IR_OWNED 里的位 (见 w5500_net.h 的
 *        W5500_Sock_IntClear): SENDOK/TIMEOUT 是 ioLibrary 的账本, 既不
 *        上报到这里、也不由我们清 —— 它们由库在下次 send() 时自行消费
 *        (超时那条路库会 close 掉 socket 并返回错误, 由发送封装报出来)。
 */
static void net_sock_event(uint8_t sn, uint8_t ir)
{
    if ((ir & W5500_SOCK_IR_RECV) != 0) {
        NetIntTest_OnRecv(sn);  /* 脚手架: D4 的 UDP 收包统计, D5 换成业务 */
    }
    if ((ir & W5500_SOCK_IR_CON) != 0) {
        LOG_I("sn%u 连接建立", sn);
    }
    if ((ir & W5500_SOCK_IR_DISCON) != 0) {
        LOG_I("sn%u 对端断开", sn);
    }
}

/** @brief 全局事件 (与具体 socket 无关的两个) */
static void net_global_event(uint8_t ir)
{
    if ((ir & W5500_IR_CONFLICT) != 0) {
        LOG_W("IP 冲突: 网内有别的设备用了同一个地址");
    }
    if ((ir & W5500_IR_UNREACH) != 0) {
        LOG_W("UDP 对端回了 ICMP 端口不可达");
    }
}

/**
 * @brief 处理一次 INT: 读标志 → 逐 socket 派发 → 直到搬空
 *
 * ★ 为什么是循环而不是"处理一次就走" (w2-plan §D4 坑 a):
 *
 *   芯片的 IR/Sn_IR 只能被【读到它的那一方】清掉, 而 ISR 里不能碰 SPI ——
 *   所以清标志只能在任务里做。于是"最后一次清标志"与"INT 线真的回到高"
 *   之间存在窗口, 那期间新到的数据只把线维持在低、**不产生新的下降沿**:
 *   只处理一轮就会漏掉它, 而且不会再有中断来救。
 *
 *   对策有两层:
 *     ① 循环里每轮【先读 SIR 再清】, 清完立刻回读 —— 清标志期间新置的位
 *        会在下一轮被看到 (这就是"结尾重查"的实际形态, 读 SIR 比读引脚
 *        更准: SIR 非空 ⇔ 线该是低的);
 *     ② 真退出后 vNetTask 再看一次引脚电平 —— 还低就把等待超时降到 2ms
 *        再来一趟 (见那里的说明)。
 *
 * ★ 为什么要限量: 收包洪水时 SIR 会一直是满的, 无限循环等于把周期服务
 *   (链路监控 / D5 的 DHCP_run) 饿死。搬不完没关系, 线还低就马上回来。
 */
static void net_socket_service(void)
{
    for (int round = 0; round < TASK_NET_SOCK_ROUNDS; round++) {
        uint8_t sir = W5500_Sock_IntPending();  /* 哪些 socket 有事 (读不清) */
        uint8_t ir  = W5500_IntClear();         /* 全局的, 顺手清掉 */

        if (ir != 0) {
            net_global_event(ir);
        }
        if (sir == 0) {
            break;                              /* 标志清空了 */
        }

        /* 逐 socket 派发。Sn_IR 必须取走并清掉 —— 留一位没清, INT 线就
         * 一直低 (芯片的"有事"是靠电平表达的, 不是靠边沿)。 */
        for (uint8_t sn = 0; sn < W5500_SOCK_COUNT; sn++) {
            if ((sir & (1u << sn)) != 0) {
                net_sock_event(sn, W5500_Sock_IntClear(sn));
            }
        }
    }
}

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

    /* W2 自检脚手架: 链路一起来就跑一次。
     *
     * ⚠️ 顺序是硬要求 —— DHCP 要在【有链路】之后才能发广播, 而它跑完会换掉
     *    板子的地址, 所以后面开出来的演示 socket 用的已经是新地址了。 */
    if (!s_selftest_done && (s_phy_last & W5500_PHY_LNK) != 0) {
        s_selftest_done = 1;

        /* D2/D3 的静态 IP 自检当前整段注释着 (见 net_selftest.c 入口),
         * 想跑那一组时把下面两行换回来。 */
        NetDhcpTest_Run();      /* D5: DHCP 拿地址 + DNS 解析 (net_dhcptest.c) */
        //NetSelfTest_Run();
    }
    /* 自检跑完之后才开演示 socket —— 那一大段是阻塞的, 期间任务根本回不到
     * 事件循环, 开着也收不到东西 */
    if (s_selftest_done && !s_intdemo_started) {
        s_intdemo_started = 1;
        NetIntTest_Start();
    }

    NetIntTest_Report();        /* 脚手架: 突发结束时打总账, D5 删 */
}

/* ================================================================
 * 任务
 * ================================================================ */

void vNetTask(void *pvParameters)
{
    (void) pvParameters;

    TickType_t last_tick;

    /* 登记句柄 + 开中断。顺序不能反: ISR 一开就可能进来拿 s_task 置位,
     * 那时句柄必须已经有效。 */
    s_task = xTaskGetCurrentTaskHandle();

    /* ================================================================
     * W5500 的全生命周期归本任务 —— 上电、配网、socket 收发都在这里。
     * 网络是【本任务的意义】而不是系统启动的前提: 芯片起不来时其它任务
     * (心跳灯 / 显示) 照常跑, 将来接 DHCP、从 NVM 读配置也都长在这一段。
     *
     * 顺序有讲究: Init 内部的软复位会清掉 PHY 配置和中断使能位, 所以配网
     * 与 W5500_Int_Enable 都必须排在它【之后】—— 见 w5500_drv.h 的步骤表。
     * ================================================================ */

    /* 起不来不删任务: 芯片晚供电、网线后插都还有救。失败原因由 Init 自己
     * 打进日志, 这里只负责节奏。 */
    while (W5500_Drv_Init() != W5500_OK) {
        vTaskDelay(pdMS_TO_TICKS(TASK_NET_RETRY_MS));
    }

    W5500_Net_Apply(&kNetCfg);

    /* 同一个开关的两头, 两头都得开, 缺哪头都是静默的:
     *   W5500_Int_Enable      芯片愿意报 (IMR/SIMR 复位默认是关的!)
     *   BSP_..._EnableInt     MCU 愿意听 (EXTI 边沿 + NVIC) */
    W5500_Int_Enable();
    BSP_W5500_HW_EnableInt(net_int_isr);

    last_tick = xTaskGetTickCount();

    for (;;) {
        uint32_t   evt = 0;
        TickType_t now;
        TickType_t wait;

        /* ★ 唯一的阻塞点。
         *
         * 两个 clear 参数: 进入时清 0 (不动已经累积的位), 退出时全清 ——
         * 于是读到的 evt 就是"上一轮之后发生过什么"。读值与清位在临界区里
         * 完成, 所以清位那一刻新来的事件不会被吞掉。
         *
         * ★ 超时值有讲究: INT 线还低 = "可能还有事没搬完" (标志清了但芯片
         *   还没放线, 或预算用完), 那就 2ms 后再来看一眼 —— 线一直低的时候
         *   **不会有新的下降沿**, 干等边沿等不到。线是高的时候才敢睡满一个
         *   服务周期, 那种情况一定有下降沿来叫醒我们。 */
        wait = (BSP_W5500_HW_IntLevel() == 0)
             ? pdMS_TO_TICKS(TASK_NET_INT_RETRY_MS)
             : pdMS_TO_TICKS(TASK_NET_TICK_MS);

        (void) xTaskNotifyWait(0, ULONG_MAX, &evt, wait);

        /* ② 中断事件 (生产者: net_int_isr)。第二个条件是上面那个 2ms 重看
         *    的入口 —— 它没有事件位, 只有"线还低"这个事实。 */
        if ((evt & TASK_NET_EVT_SOCKET) != 0 || BSP_W5500_HW_IntLevel() == 0) {
            net_socket_service();
        }

        /* ① 周期服务。按时间戳判定, 而不是"超时了就跑" —— 突发事件会提前
         * 唤醒本任务, 那时不该顺手把每个服务都跑一遍 (D5 起是 DHCP_run 这种
         * 带网络交互的)。 */
        now = xTaskGetTickCount();
        if ((now - last_tick) >= pdMS_TO_TICKS(TASK_NET_TICK_MS)) {
            last_tick = now;
            net_tick();
        }
    }
}
