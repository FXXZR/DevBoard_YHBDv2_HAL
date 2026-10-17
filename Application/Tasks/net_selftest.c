/**
 * @file    net_selftest.c
 * @brief   网络自检脚手架 —— W2 的验证代码, 不是产品流程
 *
 * 前半 (①②③④⑤) 是【同步阻塞】的: 整段跑几十秒, 每组都要 PC 侧
 * tools/net_peer.py 配合 (命令会打进日志):
 *
 *   ① UDP 收包统计   PC: net_peer.py udp --peer 192.168.1.100 --port 5002 --burst 1000
 *                    ← 丢包率决定"要不要给 W5500 接环形缓冲"
 *   ② TCP 客户端     PC: net_peer.py server --port 5000
 *   ③ TCP 服务端     PC: net_peer.py client --host 192.168.1.100 --port 5001
 *
 * ⚠️ 阻塞期间任务主循环里的周期服务 (net_tick 的链路监控) 完全停摆 ——
 *    实测拔网线时 PHYCFGR 一行都没打, 就是这个原因。临时脚手架可以这样,
 *    但**产品的周期服务不能被长操作堵住** (D5 的 DHCP_run 同理)。
 *
 * 后半 (⑥) 是 D4 的【事件驱动】形态: 不阻塞, 挂在网络任务的事件循环上,
 * 由 W5500 的 INT 引脚触发 —— 这才是产品代码该有的样子, 两组对照着看。
 *
 * socket 号分配: 0~4 归阻塞组, 5 归 ⑥, 6/7 归 D5 的 DHCP / DNS
 * (见 net_dhcptest.c)。
 */

#define LOG_TAG "W5500"
#include "log.h"

#include "net_selftest.h"
#include "w5500_net.h"
#include "bsp_w5500_hw.h"       /* 中断通路自检要读 INT 引脚电平 */

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>     /* memcmp */

/* ================================================================
 * 参数
 * ================================================================ */

enum {
    SELFTEST_TCP_SOCK   = 0,        /* 板子做客户端 (连 PC) */
    SELFTEST_SRV_SOCK   = 1,        /* 板子做服务端 (PC 来连) */
    SELFTEST_UDP_SOCK   = 2,        /* UDP */

    SELFTEST_PEER_PORT  = 5000,     /* net_peer.py server 的默认端口 */
    SELFTEST_SRV_PORT   = 5001,     /* 板子监听的端口 */
    SELFTEST_UDP_PORT   = 5002,     /* 板子的 UDP 端口 */

    SELFTEST_BYTES      = 2048,     /* 用满 socket 缓冲, 逼出最坏耗时 */
    SELFTEST_TIMEOUT_MS = 3000,
    SELFTEST_ROUNDS     = 2,

    SELFTEST_UDP_WAIT_MS = 20000,   /* UDP 总等待窗口 */
    SELFTEST_UDP_QUIET_MS = 2000,   /* 收到过包之后, 静默这么久就算结束 */
    SELFTEST_UDP_PKT_MAX = 512,     /* 单个 UDP 包的最大长度 (net_peer 默认 100) */
};

/* 对端 = PC。直连场景里 PC 就是网关 (见 w5500_net.c 的参数表) */
static const uint8_t kPeerIp[4] = { 192, 168, 1, 1 };

/* ⚠️ 缓冲必须 static: 放栈上会直接把 512 字的任务栈撑爆 */
static uint8_t s_buf[2][SELFTEST_BYTES];
static uint8_t s_udp_rx[SELFTEST_UDP_PKT_MAX];

/**
 * @brief 带重试的连接 —— 链路刚起来时【必须】重试
 *
 * 实测: 板子的 PHY 一报 link up 就发起连接, 而对端(PC)网卡此刻往往正在做
 * 重复地址检测 (DAD), 那一秒内它不接收任何单播流量 —— 于是 SYN 石沉大海,
 * 板子重传几次后 3 秒超时。同一个固件同样的时机, 有时连上有时候连不上,
 * 就是这么来的。
 *
 * 真实业务同理: 连接失败要重试, 而不是一次连不上就当网络坏了。
 */
static W5500_Sock_Status_t s_connect_retry(uint8_t sn, uint32_t attempts)
{
    W5500_Sock_Status_t st = W5500_SOCK_ERR_TIMEOUT;

    for (uint32_t i = 1; i <= attempts; i++) {
        st = W5500_Sock_TcpOpen(sn, 0);
        if (st == W5500_SOCK_OK) {
            st = W5500_Sock_Connect(sn, kPeerIp, SELFTEST_PEER_PORT,
                                    SELFTEST_TIMEOUT_MS);
            if (st == W5500_SOCK_OK) {
                return W5500_SOCK_OK;
            }
        }
        if (i < attempts) {
            LOG_W("连接第 %lu 次失败 (%s), 1 秒后重试", (unsigned long)i,
                  W5500_Sock_StatusStr(st));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    return st;
}

/* ================================================================
 * ① UDP 收包统计
 *
 * 目的: 量出"PC 全速打 1000 个包, 板子能收下多少"。这个数字是环形缓冲
 *       要不要做的依据 —— 芯片的 socket 缓冲只有 2KB, 任务来不及取就丢。
 *
 * 包格式: 前 4 字节大端序号 (net_peer.py 的 make_payload 生成),
 *        据此统计丢包与乱序 —— 只数收了多少个看不出"中间少了哪些"。
 * ================================================================ */

static void net_udp_loss_test(void)
{
    uint32_t rx_pkts = 0, lost = 0, out_of_order = 0, next_seq = 0;
    int      first_pkt = 1;
    TickType_t t0, last_rx = 0;
    uint8_t  from_ip[4];
    uint16_t from_port = 0;

    if (W5500_Sock_UdpOpen(SELFTEST_UDP_SOCK, SELFTEST_UDP_PORT) != W5500_SOCK_OK) {
        LOG_E("UDP 打开失败, 跳过");
        return;
    }

    LOG_I("--- UDP 收包统计: 窗口 %u 秒 ---", SELFTEST_UDP_WAIT_MS / 1000);
    LOG_I("    PC 侧执行: net_peer.py udp --peer 192.168.1.100 --port %u --burst 1000",
          SELFTEST_UDP_PORT);

    t0 = xTaskGetTickCount();
    for (;;) {
        uint16_t            got = 0;
        W5500_Sock_Status_t st  = W5500_Sock_UdpRecvFrom(SELFTEST_UDP_SOCK,
                                                         s_udp_rx, sizeof(s_udp_rx),
                                                         200, &got, from_ip, &from_port);

        if (st == W5500_SOCK_OK && got >= 4) {
            uint32_t seq = ((uint32_t)s_udp_rx[0] << 24) | ((uint32_t)s_udp_rx[1] << 16)
                         | ((uint32_t)s_udp_rx[2] << 8)  |  (uint32_t)s_udp_rx[3];

            if (first_pkt) {
                first_pkt = 0;
                next_seq  = seq;
                LOG_I("首包来自 %u.%u.%u.%u:%u, seq=%u, %u B",
                      from_ip[0], from_ip[1], from_ip[2], from_ip[3],
                      from_port, seq, got);
            }

            if (seq == next_seq) {
                next_seq++;
            } else if (seq > next_seq) {
                lost    += seq - next_seq;      /* 中间缺的个数 */
                next_seq = seq + 1;
            } else {
                out_of_order++;                 /* 重复或迟到 */
            }
            rx_pkts++;
            last_rx = xTaskGetTickCount();
        }

        /* 收尾: 收到过包 + 静默够久 → 说明对端打完了 */
        TickType_t now = xTaskGetTickCount();
        if (rx_pkts > 0 && (now - last_rx) >= pdMS_TO_TICKS(SELFTEST_UDP_QUIET_MS)) {
            break;
        }
        if ((now - t0) >= pdMS_TO_TICKS(SELFTEST_UDP_WAIT_MS)) {
            break;
        }
    }

    if (rx_pkts == 0) {
        LOG_W("--- UDP 结果: 一个包都没收到 (PC 侧的 burst 起了吗?) ---");
    } else {
        uint32_t total = rx_pkts + lost;
        LOG_I("--- UDP 结果: 收 %u 包, 丢 %u (整数 %u%%), 乱序 %u, 窗口 %u ms ---",
              rx_pkts, lost, (lost * 100U) / total, out_of_order,
              (unsigned)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
    }

    W5500_Sock_Close(SELFTEST_UDP_SOCK);
}

/* ================================================================
 * ② TCP 客户端 (D2 起就在跑, 保留作为通路基线)
 * ================================================================ */

static void net_tcp_client_test(void)
{
    LOG_I("--- TCP 客户端: 连 %u.%u.%u.%u:%u, %u 轮 x %u B ---",
          kPeerIp[0], kPeerIp[1], kPeerIp[2], kPeerIp[3],
          SELFTEST_PEER_PORT, SELFTEST_ROUNDS, SELFTEST_BYTES);
    LOG_I("    PC 侧执行: net_peer.py server --port %u", SELFTEST_PEER_PORT);

    for (int round = 1; round <= SELFTEST_ROUNDS; round++) {
        uint16_t sent = 0, got = 0;
        W5500_Sock_Status_t st;

        for (int i = 0; i < SELFTEST_BYTES; i++) {
            s_buf[0][i] = (uint8_t)(i + round);     /* 每轮不同, 便于核对 */
        }
        memset(s_buf[1], 0, SELFTEST_BYTES);

        if (s_connect_retry(SELFTEST_TCP_SOCK, 3) != W5500_SOCK_OK) {
            LOG_E("#%d 连接失败 —— PC 上的 net_peer.py 起来了吗?", round);
            return;
        }

        st = W5500_Sock_Send(SELFTEST_TCP_SOCK, s_buf[0], SELFTEST_BYTES,
                             SELFTEST_TIMEOUT_MS, &sent);
        if (st != W5500_SOCK_OK) {
            /* 不早退的话下面会表现成"接收超时", 分不清是哪一头出的问题 */
            LOG_E("#%d 发送失败: %s (发出 %u B)", round, W5500_Sock_StatusStr(st), sent);
            W5500_Sock_Close(SELFTEST_TCP_SOCK);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(50));      /* 让回包先到, 收的时候不用等 */
        st = W5500_Sock_Recv(SELFTEST_TCP_SOCK, s_buf[1], SELFTEST_BYTES,
                             SELFTEST_TIMEOUT_MS, &got);

        LOG_I("#%d 发 %u B, 收 %u B (%s), 内容%s",
              round, sent, got, W5500_Sock_StatusStr(st),
              (got == 0) ? "未收到"
                         : (memcmp(s_buf[0], s_buf[1], got) == 0 ? "一致" : "不一致"));

        W5500_Sock_Close(SELFTEST_TCP_SOCK);
    }
}

/* ================================================================
 * ③ TCP 服务端
 *
 * 客户端由 PC 发起 (net_peer.py client), 板子只负责监听 + 回显。
 * 与客户端的差别全在 Sn_SR 的轨迹上: 这里是 LISTEN → SYNRECV → ESTABLISHED。
 * ================================================================ */

static void net_tcp_server_test(void)
{
    LOG_I("--- TCP 服务端: 监听 %u, 等 %u 秒 ---", SELFTEST_SRV_PORT, 20);
    LOG_I("    PC 侧执行: net_peer.py client --host 192.168.1.100 --port %u",
          SELFTEST_SRV_PORT);

    if (W5500_Sock_TcpOpen(SELFTEST_SRV_SOCK, SELFTEST_SRV_PORT) != W5500_SOCK_OK
        || W5500_Sock_Listen(SELFTEST_SRV_SOCK) != W5500_SOCK_OK) {
        LOG_E("监听失败, 跳过");
        return;
    }

    if (W5500_Sock_WaitConnected(SELFTEST_SRV_SOCK, 20000) != W5500_SOCK_OK) {
        LOG_W("没人连上来, 跳过回显");
        W5500_Sock_Close(SELFTEST_SRV_SOCK);    //避免留下僵尸Listen socket (issue-log 008)
        return;
    }

    /* 回显: 收到多少回多少, 直到对端关闭或静默 */
    for (;;) {
        uint16_t got = 0, sent = 0;
        W5500_Sock_Status_t st = W5500_Sock_Recv(SELFTEST_SRV_SOCK, s_buf[1],
                                                 SELFTEST_BYTES, SELFTEST_TIMEOUT_MS, &got);
        if (st == W5500_SOCK_OK) {
            if (W5500_Sock_Send(SELFTEST_SRV_SOCK, s_buf[1], got,
                                SELFTEST_TIMEOUT_MS, &sent) != W5500_SOCK_OK) {
                LOG_W("回显失败 (发出 %u / %u B)", sent, got);
                break;
            }
            LOG_I("回显 %u B", got);
            continue;
        }

        LOG_I("收包结束: %s", W5500_Sock_StatusStr(st));
        break;
    }

    W5500_Sock_Close(SELFTEST_SRV_SOCK);
}

/* ================================================================
 * ④ 异常路径
 *
 * 连上 PC 之后持续小包收发 25 秒, 每轮把两个方向的状态都打出来 ——
 * 四种异常各自会在不同位置露头, 一张表就能分辨:
 *
 *   对端 RST      send/recv 报"对端已关闭", Sn_SR 掉回 CLOSED/INIT
 *   半关闭 (FIN)  recv 报"对端已关闭", 但 send 仍然成功  ← 唯一靠这个区分的
 *   只收不发      send 报"超时" (发送缓冲一直不腾), 连接本身还在
 *   拔网线        PHYCFGR 掉出链路位 (net_tick 打), 随后 send 超时;
 *                 芯片重传耗尽后 Sn_SR 才变
 *
 * PC 侧用 net_peer.py 的 --mode 选: rst / half-close / stall / echo(拔线用)
 * ================================================================ */

enum {
    SELFTEST_ABNORMAL_SOCK    = 3,      /* 别跟上面三个撞 */
    SELFTEST_ABNORMAL_MS      = 25000,  /* 持续窗口, 拔网线要在这段时间里 */
    /* 每轮 2KB: 只有这个量级才能在窗口内把对端的接收缓冲 (>64KB) 填满,
     * 从而造出"发送窗口关掉" —— 用小包发 50 轮也才 12KB, 压不住 */
    SELFTEST_ABNORMAL_CHUNK   = SELFTEST_BYTES,
    SELFTEST_ABNORMAL_PERIOD  = 500,
};

static void net_tcp_abnormal_test(void)
{
    uint8_t *buf   = s_buf[0];      /* 复用静态缓冲: 2KB 放栈上会撑爆任务栈 */
    uint32_t round = 0;
    W5500_Sock_Status_t last_send = W5500_SOCK_OK;
    W5500_Sock_Status_t last_recv = W5500_SOCK_OK;
    uint8_t  last_sr = 0xFF;

    for (uint32_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t)i;
    }

    LOG_I("--- TCP 异常路径: 持续收发 %u 秒 ---", SELFTEST_ABNORMAL_MS / 1000);
    LOG_I("    PC 侧执行: net_peer.py server --port %u --mode rst|half-close|stall",
          SELFTEST_PEER_PORT);

    /* 重试次数给足: 对端(PC)网卡会在链路起来后重跑一遍重复地址检测, 那一两秒
     * 内它丢弃所有入境单播, 撞上就必然连不上 —— 实测过, 不是板子的问题 */
    if (s_connect_retry(SELFTEST_ABNORMAL_SOCK, 5) != W5500_SOCK_OK) {
        LOG_E("连接失败 —— PC 侧的 net_peer.py 起来了吗?");
        return;
    }

    TickType_t t0 = xTaskGetTickCount();
    while ((xTaskGetTickCount() - t0) < pdMS_TO_TICKS(SELFTEST_ABNORMAL_MS)) {
        uint16_t got = 0, sent = 0;

        /* ⚠️ 长度写常量, 不要写 sizeof(buf) —— buf 是指针, sizeof 会得到 4,
         * 于是"每轮 2KB"悄悄变成每轮 4 字节, 窗口永远压不满, 测试静默失效 */
        W5500_Sock_Status_t st_send = W5500_Sock_Send(SELFTEST_ABNORMAL_SOCK, buf,
                                                      SELFTEST_ABNORMAL_CHUNK, 1000, &sent);
        /* 1ms 超时 = 只探一下有没有数据/有没有被关, 不当等待用 */
        W5500_Sock_Status_t st_recv = W5500_Sock_Recv(SELFTEST_ABNORMAL_SOCK, buf,
                                                      SELFTEST_ABNORMAL_CHUNK, 1, &got);

        /* 只在【状态变化】时打 —— 每轮都打会把串口淹掉, 而异常恰恰是"什么时候变的" */
        if (st_send != last_send || st_recv != last_recv) {
            LOG_I("#%lu 发送 %s (%u B), 接收 %s (%u B), Sn_SR=%s",
                  (unsigned long)round, W5500_Sock_StatusStr(st_send), sent,
                  W5500_Sock_StatusStr(st_recv), got,
                  W5500_Sock_StateStr(SELFTEST_ABNORMAL_SOCK));
            last_send = st_send;
            last_recv = st_recv;
        }

        /* Sn_SR 也只在变化时打: 拔网线后芯片要过一阵才把它变掉 */
        {
            uint8_t sr = W5500_Sock_State(SELFTEST_ABNORMAL_SOCK);
            if (sr != last_sr) {
                LOG_I("#%lu Sn_SR = %s", (unsigned long)round,
                      W5500_Sock_StateStr(SELFTEST_ABNORMAL_SOCK));
                last_sr = sr;
            }
        }

        round++;
        vTaskDelay(pdMS_TO_TICKS(SELFTEST_ABNORMAL_PERIOD));
    }

    LOG_I("--- 异常路径结束: 共 %lu 轮, 末态发送 %s / 接收 %s ---",
          (unsigned long)round,
          W5500_Sock_StatusStr(last_send), W5500_Sock_StatusStr(last_recv));
    W5500_Sock_Close(SELFTEST_ABNORMAL_SOCK);
}

/* ================================================================
 * ⑤ 「只收不发」—— 压满对端接收窗口
 *
 * 验的是发送封装的核心承诺: 发送缓冲一直不腾出来时, 必须【带期限退出】,
 * 而不是像 ioLibrary 的 send() 那样无界自旋 (那会让任务永远卡在里面,
 * 心跳照闪、系统看着还活着)。
 *
 * 为什么走服务端: 板子主动 connect 那条路要跟 PC 网卡的地址探测/残留连接
 * 抢时间 (见 s_connect_retry 的说明), 而 LISTEN → ESTABLISHED 每次都是稳的。
 * 对端用 net_peer.py client --stall: 连上就一个字节都不读。
 * ================================================================ */

enum {
    SELFTEST_STALL_SOCK   = 4,
    SELFTEST_STALL_PORT   = 5003,
    SELFTEST_STALL_ROUNDS = 40,         /* 上限, 正常会在几轮内就发不动 */
};

static void net_tcp_stall_test(void)
{
    uint32_t sent_total = 0;
    uint32_t round;
    W5500_Sock_Status_t st = W5500_SOCK_OK;

    LOG_I("--- 「只收不发」: 监听 %u ---", SELFTEST_STALL_PORT);
    LOG_I("    PC 侧执行: net_peer.py client --host 192.168.1.100 --port %u --stall",
          SELFTEST_STALL_PORT);

    if (W5500_Sock_TcpOpen(SELFTEST_STALL_SOCK, SELFTEST_STALL_PORT) != W5500_SOCK_OK
        || W5500_Sock_Listen(SELFTEST_STALL_SOCK) != W5500_SOCK_OK) {
        LOG_E("监听失败, 跳过");
        return;
    }
    if (W5500_Sock_WaitConnected(SELFTEST_STALL_SOCK, 20000) != W5500_SOCK_OK) {
        LOG_W("没人连上来, 跳过");
        return;
    }

    for (round = 1; round <= SELFTEST_STALL_ROUNDS; round++) {
        uint16_t sent = 0;

        st = W5500_Sock_Send(SELFTEST_STALL_SOCK, s_buf[0], SELFTEST_BYTES,
                             1000, &sent);
        sent_total += sent;
        if (st != W5500_SOCK_OK) {
            LOG_W("第 %lu 轮发不动了: %s (已发 %lu B) —— 封装带期限退出, 没有卡死",
                  (unsigned long)round, W5500_Sock_StatusStr(st),
                  (unsigned long)sent_total);
            break;
        }
    }

    if (st == W5500_SOCK_OK) {
        LOG_I("--- 「只收不发」结束: %lu 轮全发成功 (共 %lu B), 对端窗口没压满 ---",
              (unsigned long)(round - 1), (unsigned long)sent_total);
    }
    W5500_Sock_Close(SELFTEST_STALL_SOCK);
}

/* ================================================================
 * ⑥ D4: 中断驱动的 UDP 收包
 *
 * 与 ① 是【同一件事的两种驱动方式】, 对照着看差别:
 *
 *   ① 阻塞轮询   任务缩在 recv 的等待循环里, 每 200ms 探一次缓冲。等不到
 *                就干等, 期间什么也做不了。
 *   ⑥ 中断驱动   任务平时睡在 xTaskNotifyWait 上 (不占 CPU、不轮询任何
 *                寄存器), INT 一拉低就被唤醒, 搬完再回去睡。
 *
 * 量什么: ISR 次数 vs 实际收包数。前者远小于后者是正常的 —— 任务在搬包时
 * INT 一直被拉低, 新到的包只把线维持在低、不产生新的下降沿, 也就是"中断
 * 被合并"。反过来 (ISR 多于包) 说明有空唤醒, 得回去查标志清除顺序。
 *
 * ⚠️ 它【不】提升收包吞吐: D3 已实测瓶颈在 SPI 读取速度 (见 w2-plan §3),
 *    中断换来的东西是【响应延迟】和【CPU 不被占着】, 不是更多包。
 * ================================================================ */

enum {
    SELFTEST_INT_SOCK     = 5,      /* 0~4 被上面的阻塞组占着 */
    SELFTEST_INT_PORT     = 5004,   /* 别跟 5000~5003 撞 */
    SELFTEST_INT_QUIET_MS = 2000,   /* 静默这么久 = 一轮突发结束 */
};

static uint8_t  s_int_open;
static uint32_t s_int_pkts;         /* 本轮收到的包数 */
static uint32_t s_int_lost;         /* 按序号算出的缺口 */
static uint32_t s_int_ooo;          /* 乱序或重复 */
static uint32_t s_int_next_seq;
static uint32_t s_int_isr_base;     /* 本轮起点处的 ISR 计数 (在每轮结束时取) */
static int      s_int_armed = 1;    /* 1 = 等下一次突发的首包 */
static TickType_t s_int_last_rx;

/* ISR 侧记账 —— 由 task_net.c 的 ISR 回调调用, 只写两个变量 */
static volatile uint32_t   s_isr_count;
static volatile TickType_t s_isr_last_tick;

/**
 * @brief 成组打印之间的排空间隙
 * @note  日志环形缓冲只有 512 字节 (main.c 的 s_log_tx_buf), 115200 下每毫秒
 *        只排掉约 11.5 字节。下面这一串连着打 5 行约 490 字节 —— 实测会溢出,
 *        而 BSP_USART_Write 的语义是【装不下的直接扔】, 表现就是最后一行
 *        "PC 侧执行 ②" 被拦腰截断, 而那正是要照着敲的命令。
 */
static void s_log_gap(void)
{
    vTaskDelay(pdMS_TO_TICKS(20));
}

void NetIntTest_Start(void)
{
    W5500_IntSnapshot_t snap;
    /* 板子的地址【从芯片读】而不是写死: 走 DHCP 时它是路由器发的, 每次实验
     * 都可能不一样 —— 提示里写死就会把人带到错地址上, 白查半天。 */
    uint8_t             ip[4];

    if (W5500_Sock_UdpOpen(SELFTEST_INT_SOCK, SELFTEST_INT_PORT) != W5500_SOCK_OK) {
        LOG_E("D4: UDP 打开失败, 跳过中断收包演示");
        return;
    }

    s_int_open     = 1;
    s_int_isr_base = s_isr_count;   /* 本轮的中断从这个计数开始算 */

    /* 中断通路自检 —— 收不到中断时, 这行日志能立刻把范围缩到一半:
     *   屏蔽位该是 IMR=0xC0 / SIMR=0xFF (由 W5500_Int_Enable 打开, 芯片复位
     *   默认是 0x00 —— 忘了开就是这个现象: 什么都没发生); 空闲时引脚该是高。 */
    W5500_IntSnapshot(SELFTEST_INT_SOCK, &snap);
    LOG_I("中断通路: INT 引脚=%s, IR=0x%02X SIR=0x%02X IMR=0x%02X SIMR=0x%02X sn%u_IMR=0x%02X SR=%s",
          BSP_W5500_HW_IntLevel() ? "高" : "低",
          snap.ir, snap.sir, snap.imr, snap.simr, SELFTEST_INT_SOCK, snap.sn_imr,
          W5500_Sock_StateStr(SELFTEST_INT_SOCK));
    s_log_gap();

    getSIPR(ip);
    LOG_I("--- D4 中断收包: 板子 %u.%u.%u.%u, UDP 端口 %u 已开, 等 INT 唤醒 ---",
          ip[0], ip[1], ip[2], ip[3], SELFTEST_INT_PORT);
    s_log_gap();

    /* 两个量级的对照: 小突发能全收下 (考验的是【响应速度】), 大突发会大批
     * 丢在芯片缓冲里 (卡的是【SPI 搬出速度】, 与中断无关 —— 见 w2-plan §3)。
     * 两个都跑一遍, 才看得出中断到底换来了什么。 */
    LOG_I("    PC 侧执行 ①: net_peer.py udp --peer %u.%u.%u.%u --port %u --burst 10 --size 100 --no-recv",
          ip[0], ip[1], ip[2], ip[3], SELFTEST_INT_PORT);
    s_log_gap();
    LOG_I("    PC 侧执行 ②: net_peer.py udp --peer %u.%u.%u.%u --port %u --burst 1000 --size 100 --no-recv",
          ip[0], ip[1], ip[2], ip[3], SELFTEST_INT_PORT);
}

void NetIntTest_OnIsr(void)
{
    s_isr_count++;
    s_isr_last_tick = xTaskGetTickCountFromISR();
}

void NetIntTest_OnRecv(uint8_t sn)
{
    uint8_t  from_ip[4];
    uint16_t from_port;

    if (!s_int_open || sn != SELFTEST_INT_SOCK) {
        return;
    }

    /* 一次唤醒就把缓冲里的包全搬走 —— 芯片的 socket 缓冲只有 2KB, 留在里面
     * 的每一包都在挤占后面来的包。超时给 0 表示"只探一次", 没有就收工。 */
    for (;;) {
        uint16_t got = 0;
        W5500_Sock_Status_t st = W5500_Sock_UdpRecvFrom(sn, s_udp_rx, sizeof(s_udp_rx),
                                                        0, &got, from_ip, &from_port);
        if (st != W5500_SOCK_OK || got < 4) {
            break;
        }

        uint32_t seq = ((uint32_t)s_udp_rx[0] << 24) | ((uint32_t)s_udp_rx[1] << 16)
                     | ((uint32_t)s_udp_rx[2] << 8)  |  (uint32_t)s_udp_rx[3];

        if (s_int_armed) {
            s_int_armed = 0;
            s_int_next_seq = seq;
            /* ⚠️ ISR 基准点【不】在这里取: 叫醒我们的那次中断早就发生了
             * (中断 → 任务被唤醒 → 走到这里), 在这里取基准会把本轮的中断
             * 数记成 0。基准点在一轮结束时取 —— 见 NetIntTest_Report。 */

            /* ★ 这一个数字才是 D4 换来的东西: 从 INT 拉低到数据被取走。
             * 阻塞轮询模式下它最坏是一个服务周期 (1000ms), 中断模式下是个
             * 位数毫秒 —— 省下的是响应时间, 不是带宽。 */
            LOG_I("首包 seq=%u (%u B, 前 %u 个已丢在芯片缓冲里), 中断→取出 %u ms",
                  seq, got, seq,
                  (unsigned)((xTaskGetTickCount() - s_isr_last_tick) * portTICK_PERIOD_MS));
        }

        if (seq == s_int_next_seq) {
            s_int_next_seq++;
        } else if (seq > s_int_next_seq) {
            s_int_lost += seq - s_int_next_seq;
            s_int_next_seq = seq + 1;
        } else {
            s_int_ooo++;
        }
        s_int_pkts++;
        s_int_last_rx = xTaskGetTickCount();
    }
}

void NetIntTest_Report(void)
{
    uint32_t isr;

    if (!s_int_open || s_int_pkts == 0) {
        return;
    }
    if ((xTaskGetTickCount() - s_int_last_rx) < pdMS_TO_TICKS(SELFTEST_INT_QUIET_MS)) {
        return;                     /* 还在收, 等这轮突发停下来 */
    }

    isr = s_isr_count - s_int_isr_base;
    LOG_I("--- D4 收包: %u 包 / 中断 %u 次 (每中断 %u 包), 丢 %u, 乱序 %u ---",
          s_int_pkts, isr, (isr != 0) ? (s_int_pkts / isr) : 0,
          s_int_lost, s_int_ooo);

    /* 复位, 等下一轮 —— 演示可以反复打, 不用重启板子。
     * ISR 基准点在这里取: 此刻线已回到高、本轮的账都记完了, 于是下一轮的
     * 中断计数就是从这一刻起算。 */
    s_int_pkts = s_int_lost = s_int_ooo = 0;
    s_int_armed    = 1;
    s_int_isr_base = s_isr_count;
}

/* ================================================================
 * 入口
 * ================================================================ */

void NetSelfTest_Run(void)
{
    net_tcp_abnormal_test();    /* 放最前: 每换一种 PC 侧模式就要重跑一次 */
    net_tcp_stall_test();
    net_udp_loss_test();
    net_tcp_client_test();
    net_tcp_server_test();
    LOG_I("--- 自检结束 ---");
}
