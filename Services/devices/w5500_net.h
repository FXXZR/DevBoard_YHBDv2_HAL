/**
 * @file    w5500_net.h
 * @brief   W5500 网络层 —— 网络参数 + socket 封装
 * @note    分层位置:
 *
 *              Application/Tasks/task_net.c   ← 业务: 什么时候连、发什么
 *                    ↓
 *              Services/devices/w5500_net.c   ← 本模块: 网络参数 + socket 超时封装
 *                    ↓
 *              Services/devices/w5500_drv.c   ← 把芯片带到"可以开 socket"
 *                    ↓ 注册回调
 *              Middleware/ioLibrary           ← 协议栈 (不认识硬件)
 *                    ↓
 *              BSP/bsp_w5500_hw.c             ← 板级: CS/RESET/INT 引脚
 *
 *          与 w5500_drv 的分工: drv 管"芯片能不能用" (上电/复位/PHY/版本),
 *          本模块管"这张网怎么用" (地址参数、连接、收发超时)。
 *
 *          ⚠️ socket 层不可重入, 本模块的 API 只在网络任务里调用 —— 见
 *             [task_net.c](../../Application/Tasks/task_net.c)。
 */

#ifndef __W5500_NET_H__
#define __W5500_NET_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 常量
 * ================================================================ */

/** @brief socket 个数 —— 每个独立 2KB 收 + 2KB 发缓冲 (分配表见 w5500_drv.c) */
enum { W5500_SOCK_COUNT = 8 };

/* ================================================================
 * 状态码
 * ================================================================ */

/**
 * @brief socket 操作结果
 * @note  区分 TIMEOUT 与 CLOSED 是必要的: 前者是对端没应答 / 缓冲一直不空,
 *        后者是连接已经没了 —— 两者该做的处理完全不同 (重试 vs 重连)。
 */
typedef enum {
    W5500_SOCK_OK = 0,
    W5500_SOCK_ERR_PARAM,       /* socket 号越界 / 端口为 0 / 空指针 */
    W5500_SOCK_ERR_STATE,       /* 芯片不接受该命令 (socket 没 Open / 模式不对) */
    W5500_SOCK_ERR_TIMEOUT,     /* 期限内没等到状态变化或缓冲空间 */
    W5500_SOCK_ERR_CLOSED,      /* 连接被对端关闭或复位 */
} W5500_Sock_Status_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  写入静态网络参数 (默认路径 —— 板子直连 PC 网口的固定地址)
 * @note   参数表在 .c 内部, 换环境改那张表。
 *
 *         写完会读回校验并打日志: 不一致说明 SPI 帧出错、寄存器写入被静默
 *         丢弃。W5500 不会为此报任何错, 现象只是"ping 不通" —— 所以这一步
 *         是必要的, 不是装饰。
 *
 *         须在 W5500_Drv_Init() 之后调用 (它内部含一次软复位, 会清掉参数)。
 */
void W5500_Net_ConfigStatic(void);

/**
 * @brief  打开一个 socket 为 TCP 客户端 (本地端口给 0 = 随便选)
 * @return 状态码
 * @note   每个 socket 用之前都要 Open 一次; 关闭后要重新 Open。
 */
W5500_Sock_Status_t W5500_Sock_Open(uint8_t sn, uint16_t local_port);

/**
 * @brief  主动连接对端, 等握手完成
 * @param  sn         socket 号 (须已 Open)
 * @param  ip         对端 IP, 4 字节大端
 * @param  port       对端端口
 * @param  timeout_ms 握手期限
 * @return 状态码
 * @note   ★ 状态变迁会打进日志 (SYNSENT → ESTABLISHED / 超时) —— 这是硬件
 *         协议栈独有的排查入口: 握手卡在哪一步, 芯片自己就写着。
 *
 *         ★ 超时之后 socket 【已被关闭】, 重试要先 Open。留着它半死不活地
 *         挂在 SYNSENT 上, 下次 connect 只会返回"状态不对", 更难查。
 */
W5500_Sock_Status_t W5500_Sock_Connect(uint8_t sn, const uint8_t ip[4],
                                       uint16_t port, uint32_t timeout_ms);

/**
 * @brief  关闭 socket 并等它回到 CLOSED
 * @note   ⚠️ 内含【无超时】等待: ioLibrary 的 close() 里是
 *         `while (getSn_SR(sn) != SOCK_CLOSED);`。正常链路约 1 个 RTT,
 *         但对端消失时要等芯片把 TCP 重传次数耗尽 —— D3 拔网线那组实测这个数。
 */
void W5500_Sock_Close(uint8_t sn);

/**
 * @brief  发送, 缓冲不够就等到够或超时
 * @param  sent 出参: 实际发出的字节数 (失败时为 0)
 * @return 状态码
 * @note   ⚠️ 不能直接用 ioLibrary 的 send(): 它等发送缓冲是个【无界自旋】
 *         (`while (len > getSn_TX_FSR(sn));`), 对端把接收窗口关掉就永远转
 *         下去 —— 心跳灯照闪、系统看着还活着、功能全无。所以等待挪到这里,
 *         由本函数给期限; 等到之后 send() 第一轮就过, 不会卡。
 *
 *         对端已发 FIN (CLOSE_WAIT) 时仍可发送 —— TCP 半关闭允许这样。
 */
W5500_Sock_Status_t W5500_Sock_Send(uint8_t sn, const uint8_t *buf, uint16_t len,
                                    uint32_t timeout_ms, uint16_t *sent);

/**
 * @brief  接收, 没数据就等到有或超时
 * @param  received 出参: 实际收到的字节数 (失败时为 0)
 * @return W5500_SOCK_OK / ERR_TIMEOUT (期限内无数据) / ERR_CLOSED (对端关了)
 * @note   ⚠️ 同理不能直接用 ioLibrary 的 recv() —— 它在阻塞模式下同样是
 *         `while (recvsize == 0)` 的无界自旋。
 *
 *         ERR_CLOSED 时【不自动关 socket】: 半关闭场景下对端发完 FIN 还在
 *         收数据, 关不关由调用方决定 (要么 Close, 要么继续 Send 完再 Close)。
 */
W5500_Sock_Status_t W5500_Sock_Recv(uint8_t sn, uint8_t *buf, uint16_t len,
                                    uint32_t timeout_ms, uint16_t *received);

/** @brief 读 Sn_SR 并转成可读字符串 (供日志标注连接状态, 如 ESTABLISHED) */
const char *W5500_Sock_StateStr(uint8_t sn);

/** @brief 状态码 → 可读字符串 */
const char *W5500_Sock_StatusStr(W5500_Sock_Status_t st);

#ifdef __cplusplus
}
#endif

#endif /* __W5500_NET_H__ */
