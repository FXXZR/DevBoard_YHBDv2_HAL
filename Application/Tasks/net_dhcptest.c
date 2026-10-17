/**
 * @file    net_dhcptest.c
 * @brief   D5 脚手架 —— DHCP 要地址 (DORA) + DNS 解析公网域名
 *
 * 两条通路各占一个专属 socket (DHCP=6 / DNS=7):
 *
 *   ① DHCP   0.0.0.0:68 → 255.255.255.255:67 广播 DISCOVER
 *            → OFFER → REQUEST → ACK, 得到 ip / sn / gw / dns + 租期,
 *            出口是 getIPfromDHCP() / getSNfromDHCP() / getGWfromDHCP() / getDNSfromDHCP()
 *   ② DNS    拿 ① 下发的 DNS 地址, 解析一个公网域名 (A 记录)
 *
 * ★ 这两个库与 w5500_net.c 的 socket 封装是【两种形态】, 别混着用:
 *
 *     w5500_net.c    "我们等, 它搬" —— 等待条件由我们带期限地轮询, 库只管数据面
 *     dhcp.c/dns.c   整段阻塞 —— 它们自己轮询 Sn_RX_RSR, 没有让出点, 也没有
 *                    超时参数; 一次调用就可能占住几秒到几十秒 (DNS_run() 更
 *                    是一个纯忙等的 while(1), 靠 1 秒计数兜底退出)
 *
 *   所以它们【不能】被塞进周期服务"顺便跑一下" —— 一旦某次调用卡住, 链路
 *   监控、收包、日志全跟着停。本文件的形态是"一次性阻塞段", 跑完还给事件循环。
 *
 * ★ 1 秒时基必须由【别处】提供 (见 s_tick_1s_cb): 两个库都把超时做成秒计数,
 *   而计数只由 DHCP_time_handler() / DNS_time_handler() 推进。DNS_run() 忙等
 *   期间网络任务根本回不到主循环 —— 时基要是放在任务里, 就等于永不推进,
 *   超时兜底整个失效 (网络不通时 DNS_run() 会永远转下去)。
 */

#define LOG_TAG "W5500"
#include "log.h"

#include "net_dhcptest.h"

#include "w5500_net.h"

#include "dhcp.h"
#include "dns.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

/* ================================================================
 * 参数
 * ================================================================ */

enum {
    /* socket 号: 0~4 归 net_selftest 的阻塞组, 5 归 D4 的中断演示。
     * DHCP 与 DNS 各要一个【独占】的号 —— 两个库的 socket 号是
     * DHCP_init()/DNS_init() 的第一个参数, 默认都取 0, 共用就互相踩。 */
    NET_DHCP_SOCK = 6,
    NET_DNS_SOCK  = 7,

    /* DHCP_run() 的调用节奏。库的状态机是"收到报文就推进", 这个值只决定我们
     * 多快去看一眼; DISCOVER 的重发间隔是 DHCP_WAIT_TIME (10s), 20ms 足够密。 */
    NET_DHCP_POLL_MS = 20,

    /* 整段 DHCP 的兜底期限。库自己有 2 次 ×10s 的重试上限, 之后在 DISCOVER
     * 态返回 DHCP_FAILED —— 兜底仍要有: 卡在别的分支时库不报错。 */
    NET_DHCP_GIVEUP_MS = 60000,

    /* 单次 DHCP_run() 超过这个耗时就要解释一句 (见里面那段 ARP 冲突检测) */
    NET_DHCP_SLOW_MS = 200,

    /* DHCP 报文缓冲: 236 字节 BOOTP 固定头 + 312 字节 OPT = 548。
     * 对应 dhcp.c 里的 RIP_MSG_SIZE —— 那个宏在 .c 里, 头文件看不到。 */
    NET_DHCP_MSG_SIZE = 548,
};

/* 解析目标 —— 换域名改这里 */
static const char *const kQueryNames[] = {
    "www.baidu.com",
    "www.qq.com",
};

/* ================================================================
 * 缓冲 —— 都不能放栈上: 网络任务栈只有 512 字 (2KB)
 * ================================================================ */

static uint8_t s_dhcp_msg[NET_DHCP_MSG_SIZE];
static uint8_t s_dns_msg[MAX_DNS_BUF_SIZE];     /* 256, 见 dns.h */

/* ================================================================
 * 1 秒时基
 * ================================================================ */

static void s_tick_1s_cb(TimerHandle_t xTimer)
{
    (void) xTimer;

    /* 两个库的"秒"都是别人替它们数的 —— 谁用它们, 谁就得每秒来一下。
     * ⚠️ 这里只递增计数, 不碰任何状态机, 所以从定时器任务上下文进来是安全的。 */
    DHCP_time_handler();
    DNS_time_handler();
}

/**
 * @brief 成组打印之间的排空间隙
 *
 * ★ 日志环形缓冲只有 512 字节 (main.c 的 s_log_tx_buf), 而 115200 下每毫秒
 *   只排掉约 11.5 字节 —— 连着打几行就会把环打满, 而 BSP_USART_Write 的语义是
 *   【装不下的直接扔掉】, 现象是日志被拦腰截断、后面整行消失 (实测 D5 第一次
 *   跑: DORA 成功那一串连着 8 行约 575 字节, 丢了两行, 其中就有网络参数)。
 *
 *   20ms 排掉约 230 字节, 够放下这里任何一行。真要让日志本身抗突发, 那是
 *   改 log.c / 加大缓冲的事, 不在 D5 范围内。
 */
static void s_log_gap(void)
{
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* ================================================================
 * ① DHCP
 * ================================================================ */

/**
 * @brief 跑完 DORA, 取回租约
 * @return 1 = 拿到地址, 0 = 失败/超时
 * @note  途中会把已识别的步骤打进日志 —— Wireshark 是"报文视角", 这里是
 *        "客户端视角", 两边对不上的那一步就是问题所在。
 */
static int s_dhcp_acquire(uint8_t ip[4], uint8_t sn[4], uint8_t gw[4], uint8_t dns[4])
{
    TickType_t t0           = xTaskGetTickCount();
    int        offer_logged = 0;

    /* DHCP_init 内部会把 SIPR/GAR 清零 (源地址 0.0.0.0 才能发广播),
     * 并读走 SHAR 当 CHADDR —— 所以 MAC 必须在那之前配好。 */
    DHCP_init(NET_DHCP_SOCK, s_dhcp_msg);
    LOG_I("DHCP: 发 DISCOVER —— 0.0.0.0:68 → 255.255.255.255:67 (广播)");

    for (;;) {
        TickType_t c0   = xTaskGetTickCount();
        uint8_t    r    = DHCP_run();
        uint32_t   cost = (uint32_t)((xTaskGetTickCount() - c0) * portTICK_PERIOD_MS);

        /* 有一次调用会明显卡住: ACK 之后库要做 IP 冲突检测 —— 往租到的地址
         * 发一个 UDP 包, 芯片于是先发 ARP 探测, 网内没人应答 (RCR 次重试耗尽)
         * 才算这个地址是干净的。那段等待在库内部, 我们只能从外面量到。
         * ⚠️ 这段时间网络任务完全阻塞, 日志会静默 —— 不是死机。 */
        if (cost >= NET_DHCP_SLOW_MS) {
            LOG_I("DHCP: 本次 DHCP_run 阻塞 %u ms (IP 冲突检测: ARP 探测租到的地址, 无应答才算通过)",
                  (unsigned)cost);
        }

        /* OFFER 只体现在"库把 yiaddr 记进了 DHCP_allocated_ip"上 ——
         * DHCP_run 的返回值里没有 OFFER 这个状态, 所以从外部这样看。 */
        if (!offer_logged) {
            uint8_t offered[4];
            getIPfromDHCP(offered);
            if ((offered[0] | offered[1] | offered[2] | offered[3]) != 0) {
                offer_logged = 1;
                LOG_I("DHCP: 收到 OFFER —— 服务器提议 %u.%u.%u.%u, 已发 REQUEST",
                      offered[0], offered[1], offered[2], offered[3]);
            }
        }

        /* DHCP_IP_ASSIGN 这个返回码在 dhcp.c 里其实【从不返回】(只在头文件
         * 里承诺), 真正会来的是 LEASED —— 首次是它, 续租不变时也是它。
         * 三个一起判, 免得哪天换了上游版本行为变了要重查。 */
        if (r == DHCP_IP_ASSIGN || r == DHCP_IP_LEASED || r == DHCP_IP_CHANGED) {
            getIPfromDHCP(ip);
            getSNfromDHCP(sn);
            getGWfromDHCP(gw);
            getDNSfromDHCP(dns);
            LOG_I("DHCP: 收到 ACK —— DORA 走完, 全程 %u ms",
                  (unsigned)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
            return 1;
        }

        if (r == DHCP_FAILED) {
            LOG_E("DHCP: 失败 —— DISCOVER 重发 %d 次无人应答; Wireshark 用过滤器 bootp 看板子到底发了没有",
                  MAX_DHCP_RETRY);
            return 0;
        }

        if ((xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(NET_DHCP_GIVEUP_MS)) {
            LOG_E("DHCP: 超过 %u s 兜底期限, 放弃", (unsigned)(NET_DHCP_GIVEUP_MS / 1000));
            return 0;
        }

        vTaskDelay(pdMS_TO_TICKS(NET_DHCP_POLL_MS));
    }
}

/**
 * @brief 把租约写进芯片, 并读回校验
 * @note  为什么非写不可 —— ioLibrary 的 default_ip_assign() 只写 SIPR/SUBR/GAR,
 *        【不写 DNS】。这个版本的 W5500 分支把 DNS 存在 wizchip_conf.c 的一个
 *        静态数组里 (_DNS_), 不写它就还留着静态配置时的旧值: 芯片照样能用
 *        (DNS_run 是拿参数里的地址去 sendto 的), 但读回来的那份网络参数是错的,
 *        下次谁按 wizchip_getnetinfo() 取 DNS 就会被坑。
 */
static void s_apply_lease(const uint8_t ip[4], const uint8_t sn[4],
                          const uint8_t gw[4], const uint8_t dns[4])
{
    wiz_NetInfo lease;

    /* 先把当前那份读出来只为拿 MAC —— 它是部署数据 (task_net.c 的表), 不该
     * 在这里再抄一份。其余字段全部按租约覆盖。 */
    wizchip_getnetinfo(&lease);
    for (int i = 0; i < 4; i++) {
        lease.ip[i]  = ip[i];
        lease.sn[i]  = sn[i];
        lease.gw[i]  = gw[i];
        lease.dns[i] = dns[i];
    }
    lease.dhcp = NETINFO_DHCP;

    W5500_Net_Apply(&lease);        /* 写入 + 读回校验, 日志在那边 */
}

/* ================================================================
 * ② DNS
 * ================================================================ */

static void s_dns_resolve(const uint8_t dns_ip[4])
{
    LOG_I("DNS: 服务器 = DHCP 下发的 %u.%u.%u.%u (socket %u)",
          dns_ip[0], dns_ip[1], dns_ip[2], dns_ip[3], NET_DNS_SOCK);
    s_log_gap();

    DNS_init(NET_DNS_SOCK, s_dns_msg);

    for (unsigned i = 0; i < sizeof(kQueryNames) / sizeof(kQueryNames[0]); i++) {
        uint8_t    ip[4] = { 0, 0, 0, 0 };
        TickType_t c0    = xTaskGetTickCount();
        /* 上游签名是非 const 指针 (它只读不改), 投一次去掉 const */
        int8_t     ret   = DNS_run((uint8_t *)dns_ip, (uint8_t *)kQueryNames[i], ip);
        uint32_t   cost  = (uint32_t)((xTaskGetTickCount() - c0) * portTICK_PERIOD_MS);

        if (ret == 1) {
            /* 第二个域名通常会快一截 —— 路由器上的 DNS 缓存 + 芯片不用再 ARP */
            LOG_I("DNS: %s → %u.%u.%u.%u (%u ms)",
                  kQueryNames[i], ip[0], ip[1], ip[2], ip[3], (unsigned)cost);
        } else {
            LOG_E("DNS: %s 解析失败 (ret=%d, %u ms) —— 上限 = (MAX_DNS_RETRY+1) × DNS_WAIT_TIME = 9 s",
                  kQueryNames[i], ret, (unsigned)cost);
        }
        s_log_gap();
    }
}

/* ================================================================
 * 入口
 * ================================================================ */

void NetDhcpTest_Run(void)
{
    TimerHandle_t timer;
    uint8_t       ip[4], sn[4], gw[4], dns[4];

    LOG_I("--- D5 ① DHCP 客户端 (socket %u) ---", NET_DHCP_SOCK);
    s_log_gap();
    LOG_I("    Wireshark 过滤器: bootp   (四步全是广播, 板子接路由器时 PC 侧也抓得到)");
    s_log_gap();

    /* socket 6/7 的 Sn_IMR 已由 W5500_Int_Enable() 统一打底 (库自己开的
     * socket 同样受管), 这里不需要额外动作 —— 见那里的说明。 */

    /* 1 秒时基: 软件定时器任务优先级 = configMAX_PRIORITIES-1, 高于网络任务,
     * 所以 DNS_run() 忙等时它照样能进来 —— 这正是不能用任务内计数的原因。 */
    timer = xTimerCreate("net1s", pdMS_TO_TICKS(1000), pdTRUE, NULL, s_tick_1s_cb);
    if (timer == NULL) {
        LOG_E("D5: 1s 定时器创建失败 (FreeRTOS 堆不够?), 跳过 —— 没有它 DNS 超时兜底失效");
        return;
    }
    xTimerStart(timer, 0);

    if (s_dhcp_acquire(ip, sn, gw, dns)) {
        LOG_I("DHCP: ip %u.%u.%u.%u / %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3],
              sn[0], sn[1], sn[2], sn[3]);
        s_log_gap();
        LOG_I("DHCP: gw %u.%u.%u.%u, dns %u.%u.%u.%u, 租期 %lu s",
              gw[0], gw[1], gw[2], gw[3], dns[0], dns[1], dns[2], dns[3],
              (unsigned long)getDHCPLeasetime());
        s_log_gap();

        s_apply_lease(ip, sn, gw, dns);     /* 里面自己打两行, 见 W5500_Net_Apply */
        s_log_gap();

        /* ★ 这一行是给【PC 侧】看的: 地址是路由器发的, 每次实验都可能不同,
         *   后面所有 net_peer.py 的 --peer 和 Wireshark 的 ip.addr == 都用它。 */
        LOG_I("★ 板子地址 %u.%u.%u.%u —— net_peer.py --peer 与 Wireshark ip.addr == 都改成它",
              ip[0], ip[1], ip[2], ip[3]);
        s_log_gap();

        LOG_I("--- D5 ② DNS 解析 (socket %u) ---", NET_DNS_SOCK);
        s_log_gap();

        s_dns_resolve(dns);
    } else {
        LOG_W("D5: DHCP 没拿到地址, 跳过 DNS —— 静态 IP 仍是默认路径, 不影响其它测试");
    }

    xTimerStop(timer, 0);
    xTimerDelete(timer, 0);

    /* 栈余量: DNS 解析那一路在库里吃栈 (parse_name 的 MAXCNAME=192B 局部数组,
     * 嵌套调用一次就是数百字节), 而网络任务栈只给了 512 字。这个数字就是
     * "够不够、要不要加" 的唯一依据 —— 别的都是猜。 */
    LOG_I("D5 结束: 网络任务栈余量 %u 字 (共 512)",
          (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
