/**
 * @file    net_selftest.c
 * @brief   网络自检脚手架 —— W2 的验证代码, 不是产品流程
 *
 * 三组自检, 每组都要 PC 侧 tools/net_peer.py 配合 (命令会打进日志):
 *
 *   ① UDP 收包统计   PC: net_peer.py udp --peer 192.168.1.100 --port 5002 --burst 1000
 *                    ← 丢包率决定"要不要给 W5500 接环形缓冲"
 *   ② TCP 客户端     PC: net_peer.py server --port 5000
 *   ③ TCP 服务端     PC: net_peer.py client --host 192.168.1.100 --port 5001
 *
 * socket 号分配: 0/1/2 —— DHCP 与 DNS 之后各要一个, 所以留出了后面的号。
 *
 * ⚠️ 它是【同步阻塞】跑的 (整个自检 ~90 秒), 期间任务主循环里的周期服务
 *    (net_tick 的链路监控) 完全停摆 —— 实测拔网线时 PHYCFGR 一行都没打,
 *    就是这个原因。临时脚手架可以这样, 但**产品的周期服务不能被长操作堵住**
 *    (D5 的 DHCP_run 同理)。D4/D5 要把它改成状态机挂回主循环。
 */

#define LOG_TAG "W5500"
#include "log.h"

#include "net_selftest.h"
#include "w5500_net.h"

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
        st = W5500_Sock_Open(sn, 0);
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

    if (W5500_Sock_Listen(SELFTEST_SRV_SOCK, SELFTEST_SRV_PORT) != W5500_SOCK_OK) {
        LOG_E("监听失败, 跳过");
        return;
    }

    if (W5500_Sock_WaitConnected(SELFTEST_SRV_SOCK, 20000) != W5500_SOCK_OK) {
        LOG_W("没人连上来, 跳过回显");
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

    if (W5500_Sock_Listen(SELFTEST_STALL_SOCK, SELFTEST_STALL_PORT) != W5500_SOCK_OK) {
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
