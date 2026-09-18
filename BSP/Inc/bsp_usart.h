/**
 * @file    bsp_usart.h
 * @brief   BSP USART 驱动接口 —— 平台无关的串口收发 API
 * @note    所有 5 路 USART 统一管理.
 *          RX: HAL_UARTEx_ReceiveToIdle_IT / _DMA → 线性缓冲 → IDLE 回调
 *          TX: 环形缓冲 + IT / DMA 分段续传, 非阻塞.
 *          USART5 硬件不支持 DMA, use_dma_* 设置被忽略.
 *          DMA 通道映射参见 bsp_usart.c 中 usart_hw_map[].
 *
 *          TX 写者模型 (改动前请先确认你要写的是哪一路):
 *            USART1  单写者  日志口 (LOG 模块独占; printf 已移除, 见 syscalls.c)
 *            USART2  单写者  RS485   (bsp_rs485 独占, 依赖 TX 空闲回调控制 DE)
 *            USART3  单写者  ESP8266 (bsp_esp8266 独占)
 *            USART4  空闲
 *            USART5  空闲
 *          应用层不应越过 bsp_rs485 / bsp_esp8266 直接调用本模块 —— 那些模块
 *          已把常用 API 做了薄封装, 绕过它会破坏它们对串口的所有权约定.
 *          环形缓冲天然支持多写者, 不会串数据; 但"部分入队"是正常返回,
 *          调用者必须处理 BSP_USART_Write() 返回值 < len 的情况.
 */

#ifndef __BSP_USART_H__
#define __BSP_USART_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 串口编号枚举
 * ================================================================ */
typedef enum {
    BSP_USART1 = 0,     /* USART1 — TX=PA9,  RX=PA10 (APB2, 72MHz) */
    BSP_USART2,         /* USART2 — TX=PA2,  RX=PA3  (APB1, 36MHz) */
    BSP_USART3,         /* USART3 — TX=PB10, RX=PB11 (APB1, 36MHz) */
    BSP_USART4,         /* UART4  — TX=PC10, RX=PC11 (APB1, 36MHz) */
    BSP_USART5,         /* UART5  — TX=PC12, RX=PD2  (APB1, 36MHz) */
    BSP_USART_COUNT
} BSP_USART_Id_t;

/* ================================================================
 * 串口配置枚举
 * ================================================================ */

/** @brief 数据字长 */
typedef enum {
    BSP_USART_WORD_8B = 0,  /* 8 位数据 */
    BSP_USART_WORD_9B,      /* 9 位数据 */
} BSP_USART_WordLength_t;

/** @brief 停止位 */
typedef enum {
    BSP_USART_STOP_1 = 0,   /* 1 个停止位 */
    BSP_USART_STOP_2,       /* 2 个停止位 */
} BSP_USART_StopBits_t;

/** @brief 奇偶校验 */
typedef enum {
    BSP_USART_PARITY_NONE = 0,  /* 无校验 */
    BSP_USART_PARITY_EVEN,      /* 偶校验 */
    BSP_USART_PARITY_ODD,       /* 奇校验 */
} BSP_USART_Parity_t;

/** @brief TX 空闲回调（NULL = 不回调），在 ISR 上下文中调用
 *  @note  语义是「环形缓冲彻底排空, 且没有正在进行的传输」, 而不是「一段发完」.
 *         DMA 分段续传时, 一段发完会立刻接着发下一段, 此时不触发回调 ——
 *         这样调用者(如 RS485 的 DE 方向控制)才能安全地认为总线已释放. */
typedef void (*BSP_USART_TxCpltCallback_t)(void);

/** @brief RX IDLE 帧结束回调（NULL = 不回调）
 *  @param id 触发回调的串口编号
 *  @note  在 ISR 上下文中执行，必须快速返回.
 *          回调内调用 BSP_USART_Available(id) 获取帧长,
 *          BSP_USART_ReadBytes(id, buf, len) 读取数据.
 *          回调返回后 BSP 自动重新装填接收缓冲. */
typedef void (*BSP_USART_RxIdleCallback_t)(BSP_USART_Id_t id);

/** @brief 串口初始化配置结构体 */
typedef struct {
    uint32_t                    baudrate;          /* 波特率 */
    BSP_USART_WordLength_t      word_length;       /* 数据字长 */
    BSP_USART_StopBits_t        stop_bits;         /* 停止位 */
    BSP_USART_Parity_t          parity;            /* 奇偶校验 */
    BSP_USART_TxCpltCallback_t  tx_cplt_callback;  /* TX 完成回调 (NULL = 无) */
    BSP_USART_RxIdleCallback_t  rx_idle_callback;  /* RX IDLE 回调 (NULL = 无) */
    uint8_t                     use_dma_rx;        /* 0=IT 接收, 1=DMA 接收 */
    uint8_t                     use_dma_tx;        /* 0=IT 发送, 1=DMA 发送 */

    /* TX 环形缓冲 — 必填. 谁是本路串口的使用者, 谁提供这块内存并按真实
     * 峰值用量分配 (日志口 512B / ESP8266 口 1KB / RS485 口 256B 之类).
     * tx_buf_size 必须是 2 的幂 (环形缓冲用掩码寻址, Cortex-M3 无硬件除法),
     * 且 <= 32768. 不满足时该路 TX 不可用 (只收不发). */
    uint8_t                    *tx_buf;
    uint16_t                    tx_buf_size;
} BSP_USART_Config_t;

/* ================================================================
 * 初始化 / 去初始化
 * ================================================================ */

/**
 * @brief  初始化串口 (全参数可配)
 * @param  id     串口编号
 * @param  config 配置指针 — 必须包含 TX 环形缓冲 (tx_buf / tx_buf_size)
 * @note   没有"简易初始化"重载: TX 环形缓冲是必经资源, 必须由使用者显式提供,
 *         否则无从得知该路的真实用量. 只想快速跑通时请老实填一个静态数组.
 */
void BSP_USART_InitEx(BSP_USART_Id_t id, const BSP_USART_Config_t *config);

/**
 * @brief  关闭并去初始化 (释放 GPIO/DMA, 关闭时钟, 清空缓冲)
 */
void BSP_USART_DeInit(BSP_USART_Id_t id);

/* ----------------------------------------------------------------
 * 发送 (环形缓冲 + IT / DMA 分段续传, 全程非阻塞)
 *
 * 语义对齐 POSIX write(): 返回实际入队字节数, 部分入队是正常情况.
 * 调用者不需要知道缓冲区多大 —— 满了用 TxSpace() 查询并续传即可.
 * ---------------------------------------------------------------- */

/**
 * @brief  发送数据 (拷入 TX 环形缓冲后立即返回)
 * @return 实际入队的字节数 (0 = 缓冲已满 / 未初始化)
 * @note   返回 < len 表示缓冲空间不足, 剩余部分需调用者择机续传:
 *         可从任务上下文和 ISR 上下文调用.
 */
uint16_t BSP_USART_Write(BSP_USART_Id_t id, const uint8_t *data, uint16_t len);

/**
 * @brief  查询 TX 环形缓冲还能入队多少字节
 * @note   适用于所发的数据必须要完整发出时使用, 用于确保数据完整传输.
 */
uint16_t BSP_USART_TxSpace(BSP_USART_Id_t id);

/**
 * @brief  查询 TX 环形缓冲中尚未发出的字节数
 */
uint16_t BSP_USART_TxPending(BSP_USART_Id_t id);

/**
 * @brief  查询 TX 是否完全空闲 (环形已排空 且 无进行中的传输)
 * @return 1 = 空闲, 0 = 仍有数据在途
 */
uint8_t BSP_USART_TxIdle(BSP_USART_Id_t id);

/**
 * @brief  零拷贝发送 (直接使用调用者缓冲区, 不拷贝)
 * @return len = 已启动, 0 = 环形非空 / 发送忙 / 未初始化
 * @note   【例外路径, 非默认选择】仅当环形为空且发送空闲时才能启动,
 *         以保证发送顺序. 调用者必须保证 buffer 在发送完成前保持有效且不被修改.
 *         适合生命周期确定的数据 (常量字符串, 长期存在的静态缓冲),
 *         不适合栈上的临时帧.不要为了省拷贝开销强行使用该api
 */
uint16_t BSP_USART_SendDirect(BSP_USART_Id_t id, const uint8_t *data, uint16_t len);

/**
 * @brief  发送字符串 (便捷封装)
 * @return 入队字节数 (不含 '\0')
 */
uint16_t BSP_USART_SendString(BSP_USART_Id_t id, const char *str);

/**
 * @brief  等待发送完成 (阻塞)
 * @param  timeout 超时 ms, 0 = 死等
 * @return 0 = 完成, -1 = 超时
 * @note   等待 TX 完全空闲 (环形排空 + 传输结束), 不只是某一段发完.
 */
int BSP_USART_Flush(BSP_USART_Id_t id, uint32_t timeout);

/**
 * @brief  发送数据 —— BSP_USART_Write 的旧名, 保留以兼容既有调用
 * @note   新代码请用 BSP_USART_Write.
 */
uint16_t BSP_USART_Send(BSP_USART_Id_t id, const uint8_t *data, uint16_t len);

/* ----------------------------------------------------------------
 * 接收 (线性缓冲 + 回调)
 * ---------------------------------------------------------------- */

/**
 * @brief  查询可读字节数
 */
uint16_t BSP_USART_Available(BSP_USART_Id_t id);

/**
 * @brief  读一个字节 (非阻塞)
 * @return 0 = 成功, -1 = 缓冲空
 */
int BSP_USART_Read(BSP_USART_Id_t id, uint8_t *ch);

/**
 * @brief  读多个字节 (非阻塞)
 * @return 实际读取到的字节数 (可能 < len)
 * @note   RX 是单级缓冲, 应用自己就是唯一的排空者. 若应用落后超过一整圈,
 *         DMA 已覆盖未读数据 —— 此时本函数会丢弃最旧的一段、只保留缓冲内
 *         最新的一批, 并把丢弃量记入 BSP_USART_RxLost(). 所以返回的数据
 *         可能"中间断了一截", 靠 RxLost 判断.
 *         建议用回调模式及时排空, 而不是靠轮询.
 */
uint16_t BSP_USART_ReadBytes(BSP_USART_Id_t id, uint8_t *buf, uint16_t len);

/**
 * @brief  清空接收缓冲 (丢弃所有未读数据)
 */
void BSP_USART_FlushRx(BSP_USART_Id_t id);

/**
 * @brief  查询因应用读取不及时而被覆盖丢弃的字节数
 * @return 累计丢弃字节数 (只增不减, 除非调用 BSP_USART_ClearRxLost)
 * @note   长期为 0 才说明接收路径没有丢数据. 持续增长意味着应用排空太慢,
 *         对策是提高读取频率、扩大 USART_RX_BUF_SIZE, 或改用回调模式.
 */
uint32_t BSP_USART_RxLost(BSP_USART_Id_t id);

/** @brief 清零丢弃计数 */
void BSP_USART_ClearRxLost(BSP_USART_Id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_USART_H__ */
