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
 *          本模块管"这张网怎么用" (写入地址参数、连接、收发超时)。
 *
 *          ⚠️ socket 层不可重入, 本模块的 API 只在网络任务里调用 —— 见
 *             [task_net.c](../../Application/Tasks/task_net.c)。
 */

#ifndef __W5500_NET_H__
#define __W5500_NET_H__

#include <stdint.h>

#include "wizchip_conf.h"   /* wiz_NetInfo —— 本模块对外收的网络参数类型 */

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
 * 中断状态 (INT 引脚 → 任务)
 *
 * 分工: ISR 只负责把任务叫醒 (见 BSP_W5500_HW_IntCallback_t), 读这些寄存器
 * 必须走 SPI, 只能在任务里做 —— 所以下面这组 API 都是"任务侧"的。
 *
 * 典型用法 (完整实现见 task_net.c 的 net_socket_service):
 *
 *     while (INT 仍为低) {
 *         sir = W5500_Sock_IntPending();       // 哪些 socket 有事
 *         ir  = W5500_IntClear();              // 全局的 (IP 冲突 / UDP 不可达)
 *         for (每个置位的 sn) {
 *             flags = W5500_Sock_IntClear(sn); // 取走并清掉, 否则 INT 一直低
 *             if (flags & W5500_SOCK_IR_RECV) { ...收数据... }
 *         }
 *     }
 * ================================================================ */

/**
 * @brief socket 中断原因 (芯片 Sn_IR 的位)
 * @note  这是【芯片的定义】不是我们的抽象 —— 换协议栈时这里会变。
 *        Sn_IR 是"写 1 清位", 所以读出来必须原样写回去才算清掉。
 */
typedef enum {
    W5500_SOCK_IR_CON     = 0x01,   /* 连接建立 (客户端握手完成 / 服务端被连上) */
    W5500_SOCK_IR_DISCON  = 0x02,   /* 对端断开 (FIN 或 RST) */
    W5500_SOCK_IR_RECV    = 0x04,   /* 收到数据 —— 收包路径只关心这一位 */
    W5500_SOCK_IR_TIMEOUT = 0x08,   /* 重传超时 (对端无应答) */
    W5500_SOCK_IR_SENDOK  = 0x10,   /* 发送完成 */
} W5500_Sock_Ir_t;

/** @brief 全局中断原因 (芯片 IR 的位) */
typedef enum {
    W5500_IR_CONFLICT = 0x80,       /* IP 冲突: 网内有别的设备用了同一个地址 */
    W5500_IR_UNREACH  = 0x40,       /* ICMP 端口不可达 (UDP 发出去没人收) */
} W5500_Ir_t;

/**
 * @brief 本模块【认领】的 socket 中断位 —— 其余 (SENDOK/TIMEOUT) 归 ioLibrary
 * @note  这两个动作是配套的, 缺一不可:
 *          屏蔽  把它们挡在 SIR/INT 之外 (见 W5500_Sock_IntMask)
 *          不清  把它们留在 Sn_IR 里给库销账 (见 W5500_Sock_IntClear)
 *        只屏蔽不清 = 安全; 只不清不屏蔽 = INT 线永远低。
 */
enum {
    W5500_SOCK_IR_OWNED = W5500_SOCK_IR_CON | W5500_SOCK_IR_DISCON | W5500_SOCK_IR_RECV,
};

/**
 * @brief  打开芯片的中断上报 (写 IMR / SIMR)
 * @note   ⚠️ 必须显式调用, 没有别的地方会替你开。
 *
 *         W5500 三个屏蔽寄存器的复位默认值【不一样】, 这是最容易踩的一脚:
 *             Sn_IMR  = 0x1F  全开   ← 所以看着像"中断本来就是开的"
 *             IMR     = 0x00  全关   ← 全局中断, 一直关着
 *             SIMR    = 0x00  全关   ← socket 中断汇总, 也一直关着
 *         于是"EXTI 配好了、引脚量着是高、Sn_IR 也确实置位了, 但 INT 永远
 *         不拉低" —— 实测就卡在这里, 现象是收包路径完全静默。
 *
 *         ★ 它与 BSP 的 BSP_W5500_HW_EnableInt() 是同一个开关的两头:
 *             本函数  让【芯片】愿意把事件报出来
 *             BSP 那个 让【MCU】愿意听这个引脚
 *           缺任何一头都是静默的, 而且两边的失败现象一模一样。
 *
 *         须在 W5500_Drv_Init() 之后调用 (初始化里有软复位, 会把这些位清回 0)。
 */
void W5500_Int_Enable(void);

/**
 * @brief  读 SIR —— 哪些 socket 有挂起的中断 (位 n ↔ socket n)
 * @note   读【不】清除。它同时是 INT 引脚的电平来源: SIR 非 0 → INT 被拉低。
 *         所以"任务处理到 SIR 为 0"与"INT 回到高"本是同一件事, 差别只在
 *         芯片的传播延迟 —— 也就是处理循环收尾必须重查电平的原因。
 */
uint8_t W5500_Sock_IntPending(void);

/**
 * @brief  取走并清除一个 socket 中【本模块认领的】中断标志
 * @return 取走的位 (W5500_SOCK_IR_OWNED 的组合), 0 = 本来就没有
 * @note   ⚠️ 认领的这三位必须清: 只要它们还挂在 Sn_IR 里, INT 线就一直保持低,
 *         任务的处理循环会退不出去 (电平永远"还有事")。
 *
 *         ★ 如果使用中断且用了官方的IO库，要避开SENDOK和TIMEOUT中断，
 *         这两个中断被IO库内部使用，如果自己清零的话会导致库的运行不正常；
 */
uint8_t W5500_Sock_IntClear(uint8_t sn);

/**
 * @brief  取走并清除全局中断标志 (IP 冲突 / UDP 不可达)
 * @return 清除前的位 (W5500_IR_x 的组合), 0 = 没有
 * @note   同 Sn_IR: 不清的话 INT 线一直低。
 */
uint8_t W5500_IntClear(void);

/**
 * @brief 中断寄存器快照 (只读, 不改变任何状态) —— 排查"INT 引脚不动"的第一站
 * @param sn   想附加查看的 socket 号 (越界则 sn_imr/sn_ir 置 0)
 * @param out  出参
 * @note  这条通路的两大失败分支靠这组值就能分开:
 *
 *          芯片没报   SIR 一直是 0        → 查 SIMR / Sn_IMR (屏蔽位) 与使能流程
 *          线没到位   SIR 有值但引脚是高  → 芯片报了但没送到 MCU: 查 INT 接线、
 *                                          引脚复用、EXTI 配置
 *
 *        屏蔽位 (IMR/SIMR/Sn_IMR) 复位默认全 1, 本工程从不改它们 ——
 *        所以正常情况下它们都该是 0xFF。
 */
typedef struct {
    uint8_t  ir;        /* 全局中断标志 */
    uint8_t  sir;       /* 各 socket 的中断标志 (位 n ↔ socket n) */
    uint8_t  imr;       /* 全局中断屏蔽 */
    uint8_t  simr;      /* socket 中断屏蔽 */
    uint8_t  sn_imr;    /* sn 的中断屏蔽 */
    uint8_t  sn_ir;     /* sn 的中断标志 */
    uint8_t  sn_sr;     /* sn 的状态 (UDP 打开后应常年是 SOCK_UDP) */
    uint16_t sn_rx_rsr; /* sn 收包缓冲里躺着多少字节 —— "包到底到没到"的权威 */
} W5500_IntSnapshot_t;

void W5500_IntSnapshot(uint8_t sn, W5500_IntSnapshot_t *out);

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  写入静态网络参数
 * @param  cfg  参数表 (为 NULL 时直接返回) —— 直接收 ioLibrary 的 wiz_NetInfo
 * @note   参数是【部署数据】(哪台设备用哪个地址): 表放应用层 (见 task_net.c),
 *         本模块只负责"写进去"。将来接 NVM/SD 读配置时换的是表, 这个 API 不动。
 *
 *         为什么不自己镜像一个结构体: wiz_NetInfo 是 POD 领域数据, 不是 W5500
 *         的实现细节 (该挡在服务边界外的是行为语义 —— 比如 ioLibrary 那些无超时
 *         的 send/recv)。镜像会漏字段, dhcp 就是第一个。
 *
 *         写完会读回校验并打日志: 不一致说明 SPI 帧出错、寄存器写入被静默
 *         丢弃。W5500 不会为此报任何错, 现象只是"ping 不通" —— 所以这一步
 *         是必要的, 不是装饰。
 *
 *         须在 W5500_Drv_Init() 之后调用 (它内部含一次软复位, 会清掉参数)。
 *         本函数只管"写一次"; 动态地址 (DHCP) 是另一条路 —— 要走
 *         DHCP_init() + 在任务里周期性跑 DHCP_run(), 不是换个字段值就行。
 */
void W5500_Net_Apply(const wiz_NetInfo *cfg);

/**
 * @brief  打开一个 socket 为 TCP 模式 (本地端口给 0 = 由协议栈分配)
 * @return 状态码
 * @note   只建 socket, 【不】决定角色: 客户端接着调 Connect, 服务端接着调
 *         Listen —— 两者都只认本函数留下的 SOCK_INIT, 且各自都不再开 socket。
 *         要 UDP 模式用 W5500_Sock_UdpOpen。
 *
 *         socket 须处于 CLOSED, 否则返回 ERR_STATE。少了这一检查的话, 芯片
 *         的 OPEN 会先静默关掉已连接的 socket (不发 FIN), 对端会一直以为连接
 *         还在 —— 就是 W5500_Sock_Close 里记的那个实测坑。关连接请显式调
 *         W5500_Sock_Close。
 *
 * @param  local_port  0 = 由 ioLibrary 自动分配 (从 49152 起递增), 不是"端口 0"
 *
 * @note   ★ flag 只开 SF_TCP_NODELAY: 收到数据立即回 ACK, 不等 _RTR_ (默认
 *         200ms)。实测关着它, 板子不主动回 ACK, 对端每轮要补发一个 MSS 的
 *         重复段来"催"——上行 40% 的字节是重复段; 开着后 ACK 1.46ms 就回。
 */
W5500_Sock_Status_t W5500_Sock_TcpOpen(uint8_t sn, uint16_t local_port);

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
 * @brief  关闭 socket: 已连接先发 FIN, 再带期限等它回到 CLOSED
 * @note   等不到就发 CLOSE 命令强制关闭 —— Sn_CR_CLOSE 是"立即关", 不等
 *         对端回应。所以本函数【有界】: 对端不可达(拔网线)时最多阻塞
 *         W5500_CLOSE_TIMEOUT_MS 而不是无限期。
 *
 *         这一条是实测逼出来的: ioLibrary 的 close()/disconnect() 里都是
 *         无超时等待, 对端消失时网络任务会整个卡在关连接上 (日志直接停住)。
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

/**
 * @brief  让一个已打开的 socket 进入监听 (LISTEN)
 * @note   ★ 须先调 W5500_Sock_TcpOpen(sn, 端口) 把它带到 SOCK_INIT, 与
 *         Connect 的前置条件完全一致 —— 本函数【不】自己开 socket。
 *         本地端口因此归属 TcpOpen, 所以这里不收端口参数。
 */
W5500_Sock_Status_t W5500_Sock_Listen(uint8_t sn);

/**
 * @brief  等服务端 socket 被连上 (LISTEN → SYNRECV → ESTABLISHED)
 * @note   服务端没有"connect"这个动作, 只能等 —— 所以是轮询 Sn_SR 而不是
 *         发命令。状态变迁同样打进日志。
 */
W5500_Sock_Status_t W5500_Sock_WaitConnected(uint8_t sn, uint32_t timeout_ms);

/** @brief socket 当前是否已连接 (非阻塞查 Sn_SR, 用于收包循环里判断对端还在不在) */
int W5500_Sock_IsConnected(uint8_t sn);

/**
 * @brief  打开 socket 为 UDP
 * @note   UDP 是无连接的, 打开后立刻可收发, 没有 connect/listen 那一步。
 */
W5500_Sock_Status_t W5500_Sock_UdpOpen(uint8_t sn, uint16_t local_port);

/**
 * @brief  发一个 UDP 包
 * @param  ip/port  目的地址 —— 每次调用都要给, 芯片是发送前才把它写进
 *                  socket 寄存器。所以同一个 socket 可以轮流发给不同对端
 *                  (这正是 UDP 与 TCP 的差别: 没有"连接"可复用)。
 */
W5500_Sock_Status_t W5500_Sock_UdpSendTo(uint8_t sn, const uint8_t *buf, uint16_t len,
                                         const uint8_t ip[4], uint16_t port,
                                         uint32_t timeout_ms, uint16_t *sent);

/**
 * @brief  收一个 UDP 包
 * @param  from_ip/from_port 出参: 这包是谁发的 —— UDP 没有连接, 只能从包头取
 * @note   ⚠️ buf 必须装得下整个包。截断读会让剩下的部分留在芯片缓冲里, 而
 *         本封装的等待条件 (RX_RSR != 0) 看不出来 —— 下次调用会白等到超时。
 */
W5500_Sock_Status_t W5500_Sock_UdpRecvFrom(uint8_t sn, uint8_t *buf, uint16_t len,
                                           uint32_t timeout_ms, uint16_t *received,
                                           uint8_t from_ip[4], uint16_t *from_port);

/**
 * @brief  读 Sn_SR 原始值 (芯片的 TCP 状态机)
 * @return SOCK_CLOSED / SOCK_ESTABLISHED / SOCK_CLOSE_WAIT ... 见 "socket.h"
 * @note   要按状态做分支判断时用它; 只为了打日志用 StateStr 更方便。
 *         值来自芯片, 不是我们的抽象 —— 换协议栈时这里会变。
 */
uint8_t W5500_Sock_State(uint8_t sn);

/** @brief 读 Sn_SR 并转成可读字符串 (供日志标注连接状态, 如 ESTABLISHED) */
const char *W5500_Sock_StateStr(uint8_t sn);

/** @brief 状态码 → 可读字符串 */
const char *W5500_Sock_StatusStr(W5500_Sock_Status_t st);

#ifdef __cplusplus
}
#endif

#endif /* __W5500_NET_H__ */
