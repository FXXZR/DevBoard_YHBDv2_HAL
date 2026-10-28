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
 * 内部
 * ================================================================ */

/* 关连接时序 —— 三个期限同属"关一个 socket"这一件事。
 * 类型只在本文件出现、也不作为参数传递, 所以用匿名结构体, 不起类型名。 */
static const struct {
    /* 等 Sn_SR 变 CLOSED 的期限。正常约 1 个 RTT (毫秒级);
     * 对端不可达时靠这个值把等待切断, 不让任务无限期挂住。 */
    uint32_t close_ms;
    /* 发完强制 CLOSE 命令后再给芯片的时间 (命令本身是立即生效的) */
    uint32_t force_wait_ms;
    /* 发出 FIN 后等对端也关的期限 —— 对端应用通常毫秒级就跟着关, 这里只是兜底 */
    uint32_t fin_wait_ms;
} close_timeout = {
    .close_ms      = 2000U,
    .force_wait_ms = 100U,
    .fin_wait_ms   = 500U,
};

/* TCP 的打开 flag 只开 SF_TCP_NODELAY —— 理由与警告见 W5500_Sock_TcpOpen */

/* Sn_SR (socket 状态寄存器) 的取值 → 可读名字。
 * 这张表就是 W5500 的 TCP 状态机 —— 排查连接问题时看它比看代码快。 */
static const char *sock_state_str(uint8_t sr)
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
    /* 这三位不是 TCP 状态机的状态, 而是"这个 socket 不是 TCP" —— 少了它们,
     * UDP 的 socket 会一律显示成 "?", 排查时像是读到了坏值 */
    case SOCK_UDP:         return "UDP";
    case SOCK_IPRAW:       return "IPRAW";
    case SOCK_MACRAW:      return "MACRAW";
    default:               return "?";
    }
}

/* 命令发出后等芯片把 Sn_CR 清零 (表示命令已受理)。
 * 只占几个 SPI 时钟周期, 不设超时 —— 芯片不理采命令属于硬件故障。 */
static void sock_wait_cmd_done(uint8_t sn)
{
    while (getSn_CR(sn)) { }
}

/**
 * @brief 带期限地等 socket 回到 CLOSED
 * @return 1 = 已 CLOSED, 0 = 期限内没等到
 * @note  之所以要自己等: ioLibrary 的 close()/disconnect() 内部都是
 *        `while (getSn_SR(sn) != SOCK_CLOSED);` —— **无超时**。对端不可达时
 *        (拔网线/对端消失) 芯片要耗完 TCP 重传才回 CLOSED, 那段期间整个网络
 *        任务就卡死在这里。实测撞到过: 日志在"异常路径结束"那行之后停住。
 */
static int sock_wait_closed(uint8_t sn, uint32_t timeout_ms)
{
    TickType_t t0    = xTaskGetTickCount();
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);

    while (getSn_SR(sn) != SOCK_CLOSED) {
        if ((xTaskGetTickCount() - t0) >= limit) {
            return 0;
        }
        vTaskDelay(1);
    }
    return 1;
}

/**
 * @brief 这个状态算不算"连接已经建起来了"
 * @note  这一族状态下的 socket 必须【先发 FIN 再关】—— 见 W5500_Sock_Close。
 *        SYNSENT/SYNRECV 不在其中: 连接都还没建起来, 没有对端要通知。
 */
static int is_connected_state(uint8_t sr)
{
    return sr == SOCK_ESTABLISHED || sr == SOCK_CLOSE_WAIT || sr == SOCK_FIN_WAIT
        || sr == SOCK_CLOSING || sr == SOCK_LAST_ACK;
}

/**
 * @brief 状态一变就补一行日志 (connect 与 wait_connected 共用)
 * @param prev 调用方的"上次状态", 初值必须给 0xFF (不是任何合法状态)
 * @note  只在【变化时】打印: 轮询是每毫秒一次, 每次打印会把串口淹掉,
 *        而 Sn_SR 的价值恰恰在"哪几步走到了、卡在哪一步"。
 */
static void sock_trace_state(uint8_t sn, uint8_t sr, uint8_t *prev, uint32_t ms)
{
    if (*prev == sr) {
        return;
    }
    if (*prev == 0xFF) {
        LOG_I("sn%d Sn_SR = %s", sn, sock_state_str(sr));
    } else {
        LOG_I("sn%d Sn_SR %s -> %s (%u ms)",
              sn, sock_state_str(*prev), sock_state_str(sr), (unsigned)ms);
    }
    *prev = sr;
}

/**
 * @brief  比较两份 netinfo 是否一致
 * @note   逐字段比, 【不】memcmp 整个结构体: 结构体里有填充字节, 而读回的那份
 *         是栈上未初始化的, 填充内容不确定 —— memcmp 会误报不一致。
 */
static int sock_netinfo_equal(const wiz_NetInfo *a, const wiz_NetInfo *b)
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

void W5500_Net_Apply(const wiz_NetInfo *cfg)
{
    wiz_NetInfo back;

    if (cfg == NULL) {
        return;
    }

    /* 上游签名是非 const 指针 (它只读不改), 这里投一次去掉 const */
    wizchip_setnetinfo((wiz_NetInfo *)cfg);
    wizchip_getnetinfo(&back);      /* 从寄存器读回 —— 顺便验证写入真的落地了 */

    LOG_I("mac %02X:%02X:%02X:%02X:%02X:%02X",
          back.mac[0], back.mac[1], back.mac[2],
          back.mac[3], back.mac[4], back.mac[5]);
    /* 标签跟着 .dhcp 字段走 —— 这个函数同样用于写入 DHCP 租约,
     * 那种场合打 "static" 会把人带偏 */
    LOG_I("%s ip %u.%u.%u.%u / %u.%u.%u.%u, gw %u.%u.%u.%u, dns %u.%u.%u.%u",
          (back.dhcp == NETINFO_DHCP) ? "dhcp" : "static",
          back.ip[0], back.ip[1], back.ip[2], back.ip[3],
          back.sn[0], back.sn[1], back.sn[2], back.sn[3],
          back.gw[0], back.gw[1], back.gw[2], back.gw[3],
          back.dns[0], back.dns[1], back.dns[2], back.dns[3]);

    if (!sock_netinfo_equal(cfg, &back)) {
        LOG_W("netinfo 读回与写入不一致 —— SPI 帧有错时寄存器写入会被静默丢弃");
    }
}

/* ================================================================
 * 中断状态
 *
 * 芯片的 IR / Sn_IR 都是【写 1 清位】: 想把某一位清掉, 就把它按原样写回去。
 * 少了写回这一步, INT 线会一直低, 任务的处理循环就永远退不出去。
 *
 * ⚠️ 但 Sn_IR 的写回必须【只写我们认领的位】—— 全清会把 ioLibrary 的
 *    SENDOK/TIMEOUT 一起抹掉, 见 W5500_Sock_IntClear 的说明。
 * ================================================================ */

void W5500_Int_Enable(void)
{
    /* 全局位只放行我们真有话可说的两个: IP 冲突 / UDP 不可达。
     * 其余 (PPPoE / magic packet) 本工程用不到, 放了也只会多出空唤醒。 */
    setIMR(IM_IR7 | IM_IR6);

    /* socket 汇总: 8 个全放行, 具体上报哪些事件由各 socket 的 Sn_IMR 决定 */
    setSIMR(0xFF);

    /* 所有 socket 的 Sn_IMR 打底: 只上报我们认领的三位 (CON / DISCON / RECV)。
     *
     * ⚠️ 芯片复位默认是 0x1F, 含 SENDOK/TIMEOUT —— 那两位是 ioLibrary 的账本,
     *    我们不清 (见 W5500_SOCK_IR_OWNED), 一旦漏进 SIR 就是 INT 线永远低、
     *    网络任务 2ms 空转出不去。
     *    放这里而不是各 Open 里: 库自己开的 socket (DHCP/DNS) 没人替它设;
     *    而 Sn_IMR 是纯寄存器, socket()/close() 都不碰, 设一次就一直继承。
     * ⚠️ 软复位 (wizchip_init / 库的 default_ip_conflict) 会把它打回 0x1F,
     *    那之后必须重新调用本函数。 */
    for (uint8_t sn = 0; sn < W5500_SOCK_COUNT; sn++) {
        setSn_IMR(sn, W5500_SOCK_IR_OWNED);
    }
}

uint8_t W5500_Sock_IntPending(void)
{
    return getSIR();
}

uint8_t W5500_Sock_IntClear(uint8_t sn)
{
    uint8_t ir;

    if (sn >= W5500_SOCK_COUNT) {
        return 0;
    }

    ir = getSn_IR(sn) & W5500_SOCK_IR_OWNED;    /* 只认领我们这几位 */
    if (ir != 0) {
        /* 写回 → 清掉; 没认领的位写的是 0, 不受影响 (SENDOK/TIMEOUT 要留给
         * ioLibrary 销账 —— 见头文件里为什么不能清) */
        setSn_IR(sn, ir);
    }
    return ir;
}

uint8_t W5500_IntClear(void)
{
    uint8_t ir = getIR();   /* 同上, getIR 也只读 */

    if (ir != 0) {
        setIR(ir);
    }
    return ir;
}

void W5500_IntSnapshot(uint8_t sn, W5500_IntSnapshot_t *out)
{
    if (out == NULL) {
        return;
    }

    out->ir     = getIR();
    out->sir    = getSIR();
    out->imr    = getIMR();
    out->simr   = getSIMR();
    out->sn_imr    = (sn < W5500_SOCK_COUNT) ? getSn_IMR(sn)    : 0;
    out->sn_ir     = (sn < W5500_SOCK_COUNT) ? getSn_IR(sn)     : 0;
    out->sn_sr     = (sn < W5500_SOCK_COUNT) ? getSn_SR(sn)     : 0;
    out->sn_rx_rsr = (sn < W5500_SOCK_COUNT) ? (uint16_t)getSn_RX_RSR(sn) : 0;
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

uint8_t W5500_Sock_State(uint8_t sn)
{
    if (sn >= W5500_SOCK_COUNT) {
        return SOCK_CLOSED;
    }
    return getSn_SR(sn);
}

const char *W5500_Sock_StateStr(uint8_t sn)
{
    if (sn >= W5500_SOCK_COUNT) {
        return "?";
    }
    return sock_state_str(getSn_SR(sn));
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

W5500_Sock_Status_t W5500_Sock_TcpOpen(uint8_t sn, uint16_t local_port)
{
    if (sn >= W5500_SOCK_COUNT) {
        return W5500_SOCK_ERR_PARAM;
    }

    /* 与 UdpOpen 一致的前置检查。少了它, socket() 内部会先 close(sn),
     * 对已连接的 socket 就是【静默关闭】—— 对端永远等不到 FIN, 会一直停在
     * ESTABLISHED 上 (同 W5500_Sock_Close 里记的那个实测坑)。 */
    if (getSn_SR(sn) != SOCK_CLOSED) {
        return W5500_SOCK_ERR_STATE;
    }

    /* socket() 要求 SIPR 非 0, 所以网络参数必须先配好。
     * flag 只开 NODELAY; ⚠️ 别加 SF_IO_NONBLOCK —— 这份 ioLibrary 的 recv()
     * 在非阻塞下不管有没有数据都返回 SOCK_BUSY, 一个字节都读不出来。 */
    if (socket(sn, Sn_MR_TCP, local_port, SF_TCP_NODELAY) != (int8_t)sn) {
        return W5500_SOCK_ERR_STATE;
    }

    /* Sn_IMR 由 W5500_Int_Enable() 统一打底, 这里不再重复 */

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
    sock_wait_cmd_done(sn);                /* CONNECT 命令被受理 → 芯片进 SYNSENT */

    LOG_I("sn%d 连接 %u.%u.%u.%u:%u", sn, ip[0], ip[1], ip[2], ip[3], port);

    TickType_t t0    = xTaskGetTickCount();
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    uint8_t    last  = 0xFF;

    for (;;) {
        uint8_t    sr      = getSn_SR(sn);
        TickType_t elapsed = xTaskGetTickCount() - t0;

        /* 状态一变就打 —— 握手卡在哪一步, 芯片自己写着 */
        sock_trace_state(sn, sr, &last, (uint32_t)pdTICKS_TO_MS(elapsed));

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
            LOG_W("sn%d 连接超时 %u ms, 最后状态 %s", sn, timeout_ms, sock_state_str(sr));
            W5500_Sock_Close(sn);       /* 留着 SYNSENT 半死不活更难查 */
            return W5500_SOCK_ERR_TIMEOUT;
        }

        vTaskDelay(1);                  /* 让出 CPU; 握手是毫秒级事件, 1ms 粒度足够 */
    }
}

int W5500_Sock_IsConnected(uint8_t sn)
{
    if (sn >= W5500_SOCK_COUNT) {
        return 0;
    }
    return getSn_SR(sn) == SOCK_ESTABLISHED;
}

W5500_Sock_Status_t W5500_Sock_Listen(uint8_t sn)
{
    if (sn >= W5500_SOCK_COUNT) {
        return W5500_SOCK_ERR_PARAM;
    }
    /* 与 Connect 同一套前置条件: 只认 TcpOpen 留下的 SOCK_INIT。
     * 本函数【不】自己开 socket —— 建 socket 是 TcpOpen 的事。 */
    if (getSn_SR(sn) != SOCK_INIT) {
        return W5500_SOCK_ERR_STATE;
    }
    if (listen(sn) != SOCK_OK) {
        return W5500_SOCK_ERR_STATE;
    }

    LOG_I("sn%d 已监听, Sn_SR = %s", sn, W5500_Sock_StateStr(sn));
    return W5500_SOCK_OK;
}

W5500_Sock_Status_t W5500_Sock_WaitConnected(uint8_t sn, uint32_t timeout_ms)
{
    if (sn >= W5500_SOCK_COUNT) {
        return W5500_SOCK_ERR_PARAM;
    }

    TickType_t t0    = xTaskGetTickCount();
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    uint8_t    last  = 0xFF;

    for (;;) {
        uint8_t    sr      = getSn_SR(sn);
        TickType_t elapsed = xTaskGetTickCount() - t0;

        /* 服务端的状态轨迹: LISTEN → SYNRECV → ESTABLISHED —— 谁连上来的、
         * 卡在哪一步, 这里一目了然 */
        sock_trace_state(sn, sr, &last, (uint32_t)pdTICKS_TO_MS(elapsed));

        if (sr == SOCK_ESTABLISHED) {
            LOG_I("sn%d 对端已连上 (%u ms)", sn, (unsigned)pdTICKS_TO_MS(elapsed));
            return W5500_SOCK_OK;
        }
        if (sr == SOCK_CLOSED) {
            LOG_W("sn%d 监听被关闭 (socket 没了, 要重新 Listen)", sn);
            return W5500_SOCK_ERR_CLOSED;
        }

        if (elapsed >= limit) {
            LOG_W("sn%d 等连接超时 %u ms, 最后状态 %s",
                  sn, timeout_ms, sock_state_str(sr));
            return W5500_SOCK_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
}

/* ================================================================
 * UDP
 *
 * 与 TCP 的区别落到代码上只有两点:
 *   ① 打开后没有 connect/listen —— Sn_SR 直接就是 SOCK_UDP, 立刻能收发
 *   ② 目的地址是【每次发送时】给的 (sendto), 不存在"连接"这个概念,
 *      所以同一个 socket 可以轮流跟不同对端说话
 * ================================================================ */

W5500_Sock_Status_t W5500_Sock_UdpOpen(uint8_t sn, uint16_t local_port)
{
    if (sn >= W5500_SOCK_COUNT) {
        return W5500_SOCK_ERR_PARAM;
    }
    if (getSn_SR(sn) != SOCK_CLOSED) {
        return W5500_SOCK_ERR_STATE;
    }
    /* UDP 的 flag 给 0: SF_TCP_NODELAY 在 UDP 下是 MMB (阻断组播), 语义不同 */
    if (socket(sn, Sn_MR_UDP, local_port, 0) != (int8_t)sn) {
        return W5500_SOCK_ERR_STATE;
    }
    /* Sn_IMR 同 TcpOpen: 已由 W5500_Int_Enable() 统一打底 */

    LOG_I("sn%d UDP 打开, 本地端口 %u", sn, local_port);
    return W5500_SOCK_OK;
}

W5500_Sock_Status_t W5500_Sock_UdpSendTo(uint8_t sn, const uint8_t *buf, uint16_t len,
                                         const uint8_t ip[4], uint16_t port,
                                         uint32_t timeout_ms, uint16_t *sent)
{
    if (sent != NULL) {
        *sent = 0;
    }
    if (sn >= W5500_SOCK_COUNT || buf == NULL || len == 0 || ip == NULL || port == 0) {
        return W5500_SOCK_ERR_PARAM;
    }

    TickType_t t0    = xTaskGetTickCount();
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);

    /* 同 TCP: 先带期限等发送缓冲 —— ioLibrary 的 sendto() 里也是无界自旋 */
    for (;;) {
        if (getSn_SR(sn) == SOCK_CLOSED) {
            return W5500_SOCK_ERR_CLOSED;
        }
        if ((uint16_t)getSn_TX_FSR(sn) >= len) {
            break;
        }
        if ((xTaskGetTickCount() - t0) >= limit) {
            return W5500_SOCK_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }

    int32_t n = sendto(sn, (uint8_t *)buf, len, (uint8_t *)ip, port);
    if (n <= 0) {
        return W5500_SOCK_ERR_STATE;
    }

    *sent = (uint16_t)n;
    return W5500_SOCK_OK;
}

W5500_Sock_Status_t W5500_Sock_UdpRecvFrom(uint8_t sn, uint8_t *buf, uint16_t len,
                                           uint32_t timeout_ms, uint16_t *received,
                                           uint8_t from_ip[4], uint16_t *from_port)
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
        /* 缓冲里有字节就算就绪 —— 包没收完整时 recvfrom() 内部会等它收完 */
        if ((uint16_t)getSn_RX_RSR(sn) != 0) {
            break;
        }
        if (getSn_SR(sn) == SOCK_CLOSED) {
            return W5500_SOCK_ERR_CLOSED;
        }
        if ((xTaskGetTickCount() - t0) >= limit) {
            return W5500_SOCK_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }

    int32_t n = recvfrom(sn, buf, len, from_ip, from_port);
    if (n <= 0) {
        return W5500_SOCK_ERR_STATE;
    }

    *received = (uint16_t)n;
    return W5500_SOCK_OK;
}

void W5500_Sock_Close(uint8_t sn)
{
    /* ★ 为什么自己发命令而不用 ioLibrary 的 disconnect(): 它发完 DISCON 后是
     *   `while (Sn_SR != SOCK_CLOSED)` 的【无 yield 忙等】, 对端不可达时芯片要
     *   先耗完 TCP 重传才回 CLOSED (按 _RTR_/_RCR_ 是几十秒),在RTOS环境下不建议用 */
    uint8_t sr;

    if (sn >= W5500_SOCK_COUNT) {
        return;
    }

    sr = getSn_SR(sn);

    /* ★ 已连接时走正常挥手 DISCON —— 这是【发 FIN 告诉对端】的那条路 */
    if (is_connected_state(sr)) {
        setSn_CR(sn, Sn_CR_DISCON);
        sock_wait_cmd_done(sn);

        /* ★ 自己带期限等, 等不到就【强制关闭】: 不能直接调 close(), 它内部是
         *   无超时忙等, 对端不可达时网络任务整个卡死 (见本地疑难问题记录 006)。 */
        if (sock_wait_closed(sn, close_timeout.close_ms)) {
            close(sn);
            return;
        }

        /* ★★ 重读一次状态再决定 —— 这两秒里芯片可能【自己】把连接建起来了
         *    (对端的 SYN-ACK 只是来晚了), 见本地疑难问题记录 007。 */
        sr = getSn_SR(sn);
        if (is_connected_state(sr)) {
            LOG_W("sn%d 超时放弃时芯片已进 %s, 先发 FIN 通知对端", sn, sock_state_str(sr));
            setSn_CR(sn, Sn_CR_DISCON);
            sock_wait_cmd_done(sn);
            if (sock_wait_closed(sn, close_timeout.fin_wait_ms)) {
                close(sn);
                return;
            }
        }
        /* 只在【确实还没回到 CLOSED】时才告警: 上面两次等待之间, 芯片可能自己关掉了 */
        if (getSn_SR(sn) != SOCK_CLOSED) {
            LOG_W("sn%d 等不到 CLOSED (对端不可达?), 发 CLOSE 强制关闭", sn);
        }
    }

    /* 走到这里有两种情况:
     *   ① 已连接但挥手没走完 (对端消失/不回应);
     *   ② 本来就是 LISTEN / UDP 这类【没有对端要通知】的状态 —— 芯片不会自己
     *      回 CLOSED, 等它两秒纯属浪费, 还会打一行"对端不可达?"的误导告警。
     * 两者都该直接强关: Sn_CR_CLOSE 是"立即关", 不等对端回应。 */
    if (getSn_SR(sn) != SOCK_CLOSED) {
        setSn_CR(sn, Sn_CR_CLOSE);
        sock_wait_cmd_done(sn);
        /* 兜底再等一下; 仍未 CLOSED 说明是芯片级故障, 下面 close() 会阻塞,
         * 那就该由断言通道响了 —— 但那是硬件坏了, 与本处逻辑无关 */
        (void)sock_wait_closed(sn, close_timeout.force_wait_ms);
    }

    /* 此时 Sn_SR 已是 CLOSED, close() 内部第一轮就返回; 它的作用是
     * 清掉 ioLibrary 的静态记录 (sock_is_sending / sock_io_mode 等) */
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

    /* 先等发送缓冲腾出 len 字节 —— 这段就是 ioLibrary 无界自旋的那个等待。
     * 缓冲够了再调 send(); 但它开头的 sock_is_sending 分支不受 io_mode 约束,
     * 阻塞模式下也会返回 SOCK_BUSY(0) —— 那是"还没就绪", 要重试不是报错。 */
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
            int32_t n = send(sn, (uint8_t *)buf, len);

            if (n != SOCK_BUSY) {
                if (n < 0) {
                    return W5500_SOCK_ERR_STATE;
                }
                *sent = (uint16_t)n;
                return W5500_SOCK_OK;
            }
        }

        if ((xTaskGetTickCount() - t0) >= limit) {
            LOG_W("sn%d 发送缓冲不够 (剩 %u 需 %u), 等满 %u ms 发不出去",
                  sn, fsr, len, timeout_ms);
            return W5500_SOCK_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
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
