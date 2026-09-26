/**
 * @file    w5500_net.c
 * @brief   W5500 网络层 —— 网络参数配置 + socket 封装
 * @note    寄存器访问一律走 ioLibrary 的 WIZCHIP_READ/WRITE, 它们最终调用
 *          w5500_port.c 注册进来的回调 —— 而回调里带着总线锁和片选控制。
 *          本文件不直接碰 SPI。
 */

#define LOG_TAG "W5500"
#include "log.h"

#include "w5500_net.h"

#include "socket.h"             /* ioLibrary 的 socket API 与 Sn_SR 状态值 */
#include "wizchip_conf.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>     /* memcmp */
#include <stddef.h>     /* NULL */

/* ================================================================
 * 静态网络参数
 *
 * 场景: 板子直连 PC 网口 (PC 网卡配 192.168.1.1/24)。这样 Wireshark 既是
 * 对端又是抓包点, 板子的每个报文都看得到 —— 经过路由器就只能抓广播了。
 *
 * MAC 首字节 0x02 = 本地管理 + 单播, 不会撞到任何真实厂商的 OUI。
 * 换环境只改这张表。
 * ================================================================ */

static const wiz_NetInfo kNetStatic = {
    .mac  = { 0x02, 0x08, 0xDC, 0x01, 0x02, 0x03 },
    .ip   = { 192, 168, 1, 100 },
    .sn   = { 255, 255, 255, 0 },
    .gw   = { 192, 168, 1, 1 },
    .dns  = { 192, 168, 1, 1 },     /* 接 DNS 时改成真实 DNS 服务器 */
    .dhcp = NETINFO_STATIC,
};

/* ================================================================
 * 内部
 * ================================================================ */

/* Sn_SR (socket 状态寄存器) 的取值 → 可读名字。
 * 这张表就是 W5500 的 TCP 状态机 —— 排查连接问题时看它比看代码快。 */
static const char *s_state_str(uint8_t sr)
{
    switch (sr) {
    case SOCK_CLOSED:      return "CLOSED";
    case SOCK_INIT:        return "INIT";
    case SOCK_LISTEN:      return "LISTEN";
    case SOCK_SYNSENT:     return "SYNSENT";
    case SOCK_SYNRECV:     return "SYNRECV";
    case SOCK_ESTABLISHED: return "ESTABLISHED";
    case SOCK_FIN_WAIT:    return "FIN_WAIT";
    case SOCK_CLOSING:     return "CLOSING";
    case SOCK_TIME_WAIT:   return "TIME_WAIT";
    case SOCK_CLOSE_WAIT:  return "CLOSE_WAIT";
    case SOCK_LAST_ACK:    return "LAST_ACK";
    default:               return "?";
    }
}

/* 命令发出后等芯片把 Sn_CR 清零 (表示命令已受理)。
 * 只占几个 SPI 时钟周期, 不设超时 —— 芯片不理采命令属于硬件故障。 */
static void s_wait_cmd_done(uint8_t sn)
{
    while (getSn_CR(sn)) { }
}

/**
 * @brief  比较两份 netinfo 是否一致
 * @note   逐字段比, 【不】memcmp 整个结构体: 结构体里有填充字节, 而读回的那份
 *         是栈上未初始化的, 填充内容不确定 —— memcmp 会误报不一致。
 */
static int netinfo_equal(const wiz_NetInfo *a, const wiz_NetInfo *b)
{
    return memcmp(a->mac, b->mac, sizeof(a->mac)) == 0
        && memcmp(a->ip,  b->ip,  sizeof(a->ip))  == 0
        && memcmp(a->sn,  b->sn,  sizeof(a->sn))  == 0
        && memcmp(a->gw,  b->gw,  sizeof(a->gw))  == 0
        && memcmp(a->dns, b->dns, sizeof(a->dns)) == 0
        && a->dhcp == b->dhcp;
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void W5500_Net_ConfigStatic(void)
{
    wiz_NetInfo back;

    /* 上游签名是非 const 指针 (它只读不改), 这里投一次去掉 const */
    wizchip_setnetinfo((wiz_NetInfo *)&kNetStatic);
    wizchip_getnetinfo(&back);      /* 从寄存器读回 —— 顺便验证写入真的落地了 */

    LOG_I("mac %02X:%02X:%02X:%02X:%02X:%02X",
          back.mac[0], back.mac[1], back.mac[2],
          back.mac[3], back.mac[4], back.mac[5]);
    LOG_I("static ip %u.%u.%u.%u / %u.%u.%u.%u, gw %u.%u.%u.%u, dns %u.%u.%u.%u",
          back.ip[0], back.ip[1], back.ip[2], back.ip[3],
          back.sn[0], back.sn[1], back.sn[2], back.sn[3],
          back.gw[0], back.gw[1], back.gw[2], back.gw[3],
          back.dns[0], back.dns[1], back.dns[2], back.dns[3]);

    if (!netinfo_equal(&kNetStatic, &back)) {
        LOG_W("netinfo 读回与写入不一致 —— SPI 帧有错时寄存器写入会被静默丢弃");
    }
}

/* ================================================================
 * socket —— 收发 + 超时
 *
 * 为什么要在 ioLibrary 之上再包一层 (它本来就有 connect/send/recv):
 *
 *   ① 它的 send()/recv() 是【无界自旋】。send() 是
 *          while (1) { if (len <= getSn_TX_FSR(sn)) break; }
 *      —— 对端把接收窗口关掉就永远转下去; recv() 同形, 等一个永远不来的包
 *      也一样。两个都没有超时参数可传。
 *   ② 它的 connect() 只有芯片自己的重传超时, 调用方给不了期限。
 *   ③ 状态变迁没有日志, 而 Sn_SR 恰好是硬件协议栈最值钱的排查入口。
 *
 * 手法是【我们等, 它搬】: 先用带期限的轮询等到条件成立 (发送缓冲够 /
 * 收到数据), 再调 ioLibrary 的函数 —— 那时它内部第一轮就通过, 不会自旋。
 * 数据面仍是它的代码, 我们只接管"等"这件事。
 *
 * ⚠️ 本组 API 只在网络任务里调用: socket 层不可重入 (ioLibrary 的 socket.c
 *    有静态状态), 而且轮询里的 vTaskDelay 需要任务上下文。
 * ================================================================ */

const char *W5500_Sock_StateStr(uint8_t sn)
{
    if (sn >= W5500_SOCK_COUNT) {
        return "?";
    }
    return s_state_str(getSn_SR(sn));
}

const char *W5500_Sock_StatusStr(W5500_Sock_Status_t st)
{
    switch (st) {
    case W5500_SOCK_OK:          return "OK";
    case W5500_SOCK_ERR_PARAM:   return "参数错";
    case W5500_SOCK_ERR_STATE:   return "状态不对";
    case W5500_SOCK_ERR_TIMEOUT: return "超时";
    case W5500_SOCK_ERR_CLOSED:  return "对端已关闭";
    default:                     return "未知";
    }
}

W5500_Sock_Status_t W5500_Sock_Open(uint8_t sn, uint16_t local_port)
{
    if (sn >= W5500_SOCK_COUNT) {
        return W5500_SOCK_ERR_PARAM;
    }

    /* socket() 要求 SIPR 非 0, 所以网络参数必须先配好。
     * 返回负数说明命令被拒 (socket 状态不对 / 模式不支持)。 */
    if (socket(sn, Sn_MR_TCP, local_port, 0) != (int8_t)sn) {
        return W5500_SOCK_ERR_STATE;
    }
    return W5500_SOCK_OK;
}

W5500_Sock_Status_t W5500_Sock_Connect(uint8_t sn, const uint8_t ip[4],
                                       uint16_t port, uint32_t timeout_ms)
{
    if (sn >= W5500_SOCK_COUNT || ip == NULL || port == 0) {
        return W5500_SOCK_ERR_PARAM;
    }
    if (getSn_SR(sn) != SOCK_INIT) {
        return W5500_SOCK_ERR_STATE;    /* 没 Open, 或上次连接还没清干净 */
    }

    setSn_DPORTR(sn, port);
    setSn_DIPR(sn, (uint8_t *)ip);
    setSn_CR(sn, Sn_CR_CONNECT);
    s_wait_cmd_done(sn);                /* CONNECT 命令被受理 → 芯片进 SYNSENT */

    LOG_I("sn%d 连接 %u.%u.%u.%u:%u", sn, ip[0], ip[1], ip[2], ip[3], port);

    TickType_t t0    = xTaskGetTickCount();
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    uint8_t    last  = 0;
    int        first = 1;

    for (;;) {
        uint8_t    sr      = getSn_SR(sn);
        TickType_t elapsed = xTaskGetTickCount() - t0;

        /* 状态一变就打 —— 握手卡在哪一步, 芯片自己写着 */
        if (first || sr != last) {
            if (first) {
                LOG_I("sn%d Sn_SR = %s", sn, s_state_str(sr));
                first = 0;
            } else {
                LOG_I("sn%d Sn_SR %s -> %s (%u ms)",
                      sn, s_state_str(last), s_state_str(sr),
                      (unsigned)pdTICKS_TO_MS(elapsed));
            }
            last = sr;
        }

        if (sr == SOCK_ESTABLISHED) {
            LOG_I("sn%d 握手完成, 用时 %u ms", sn, (unsigned)pdTICKS_TO_MS(elapsed));
            return W5500_SOCK_OK;
        }
        if (sr == SOCK_CLOSED) {
            LOG_W("sn%d 连接过程中被关闭 (对端 RST?)", sn);
            return W5500_SOCK_ERR_CLOSED;
        }

        /* 芯片自己的重传超时 —— 对端 ARP 不通或 SYN 没人理 */
        if (getSn_IR(sn) & Sn_IR_TIMEOUT) {
            setSn_IR(sn, Sn_IR_TIMEOUT);
            LOG_W("sn%d 芯片报连接超时 (对端无应答)", sn);
            W5500_Sock_Close(sn);
            return W5500_SOCK_ERR_TIMEOUT;
        }

        if (elapsed >= limit) {
            LOG_W("sn%d 连接超时 %u ms, 最后状态 %s", sn, timeout_ms, s_state_str(sr));
            W5500_Sock_Close(sn);       /* 留着 SYNSENT 半死不活更难查 */
            return W5500_SOCK_ERR_TIMEOUT;
        }

        vTaskDelay(1);                  /* 让出 CPU; 握手是毫秒级事件, 1ms 粒度足够 */
    }
}

void W5500_Sock_Close(uint8_t sn)
{
    uint8_t sr;

    if (sn >= W5500_SOCK_COUNT) {
        return;
    }

    sr = getSn_SR(sn);

    /* ★ 已连接时必须先 disconnect() —— 这是【发 FIN 告诉对端】的那条路。
     *
     * 只调 close() 的后果实测过: 芯片本地进入 CLOSED, 但**对端一个字节都
     * 收不到**, 它会一直停在 ESTABLISHED 占着那条连接。于是我们的下一次
     * 连接虽然能握手成功 (内核 backlog 会自动回 SYN-ACK), 但对端应用
     * accept 不到、永远读不到数据 —— 现象是"连上了却收不到任何回包",
     * 而且抓包看到的全是正常握手, 极难往"上一次没关干净"上想。
     *
     * SYNSENT / SYNRECV (连接都没建起来) 不用 DISCON, 直接 CLOSE 中止。 */
    if (sr == SOCK_ESTABLISHED || sr == SOCK_CLOSE_WAIT || sr == SOCK_FIN_WAIT
        || sr == SOCK_CLOSING || sr == SOCK_LAST_ACK) {
        disconnect(sn);
    }

    /* 兜底: 确保回到 SOCK_CLOSED, 并清掉 ioLibrary 的静态记录。
     * ⚠️ 内含 `while (getSn_SR(sn) != SOCK_CLOSED);` 的无超时等待 ——
     * 正常约 1 个 RTT, 对端消失时要等芯片耗完重传次数。见头文件。 */
    close(sn);
}

W5500_Sock_Status_t W5500_Sock_Send(uint8_t sn, const uint8_t *buf, uint16_t len,
                                    uint32_t timeout_ms, uint16_t *sent)
{
    if (sent != NULL) {
        *sent = 0;
    }
    if (sn >= W5500_SOCK_COUNT || buf == NULL || len == 0) {
        return W5500_SOCK_ERR_PARAM;
    }

    TickType_t t0    = xTaskGetTickCount();
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);

    /* 先等发送缓冲腾出 len 字节 —— 这一段就是 ioLibrary 无界自旋的那个等待 */
    for (;;) {
        uint8_t  sr = getSn_SR(sn);
        uint16_t fsr;

        if (sr == SOCK_CLOSED) {
            return W5500_SOCK_ERR_CLOSED;
        }
        /* CLOSE_WAIT 也允许发送: 对端发了 FIN 但连接是半开的 */
        if (sr != SOCK_ESTABLISHED && sr != SOCK_CLOSE_WAIT) {
            return W5500_SOCK_ERR_STATE;
        }

        fsr = (uint16_t)getSn_TX_FSR(sn);
        if (fsr >= len) {
            break;
        }

        if ((xTaskGetTickCount() - t0) >= limit) {
            LOG_W("sn%d 发送缓冲不够 (剩 %u 需 %u), 等满 %u ms 放弃",
                  sn, fsr, len, timeout_ms);
            return W5500_SOCK_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }

    /* 到这里 send() 内部第一轮就通过, 不会再自旋 */
    int32_t n = send(sn, (uint8_t *)buf, len);
    if (n <= 0) {
        return W5500_SOCK_ERR_STATE;
    }

    *sent = (uint16_t)n;
    return W5500_SOCK_OK;
}

W5500_Sock_Status_t W5500_Sock_Recv(uint8_t sn, uint8_t *buf, uint16_t len,
                                    uint32_t timeout_ms, uint16_t *received)
{
    if (received != NULL) {
        *received = 0;
    }
    if (sn >= W5500_SOCK_COUNT || buf == NULL || len == 0) {
        return W5500_SOCK_ERR_PARAM;
    }

    TickType_t t0    = xTaskGetTickCount();
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);

    for (;;) {
        uint16_t rsr = (uint16_t)getSn_RX_RSR(sn);

        if (rsr != 0) {
            /* 一次最多取走 len; 芯片 RX 缓冲只有 2KB, 剩下的下次再取 */
            uint16_t want = (rsr < len) ? rsr : len;
            int32_t  n    = recv(sn, buf, want);

            if (n <= 0) {
                return W5500_SOCK_ERR_STATE;
            }
            *received = (uint16_t)n;
            return W5500_SOCK_OK;
        }

        /* 对端发了 FIN 且数据已取完。不在这里 close —— 半关闭下还能继续发,
         * 关不关由调用方决定。 */
        {
            uint8_t sr = getSn_SR(sn);
            if (sr == SOCK_CLOSED || sr == SOCK_CLOSE_WAIT || sr == SOCK_LAST_ACK) {
                return W5500_SOCK_ERR_CLOSED;
            }
        }

        if ((xTaskGetTickCount() - t0) >= limit) {
            return W5500_SOCK_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
}
