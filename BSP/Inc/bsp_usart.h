/**
 * @file    bsp_usart.h
 * @brief   BSP USART 驱动接口 —— 平台无关的串口收发 API
 * @note    所有 5 路 USART 统一管理.
 *          RX: HAL_UARTEx_ReceiveToIdle_IT / _DMA → 线性缓冲 → IDLE 回调
 *          TX: IT / DMA 发送, 线性缓冲, SendDirect 零拷贝.
 *          USART5 硬件不支持 DMA, use_dma_* 设置被忽略.
 *          DMA 通道映射参见 bsp_usart.c 中 usart_hw_map[].
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

/** @brief TX 发送完成回调（NULL = 不回调），在 ISR 上下文中调用 */
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
} BSP_USART_Config_t;

/* ================================================================
 * 初始化 / 去初始化
 * ================================================================ */

/**
 * @brief  简易初始化 (8-N-1, IT 收发, 无回调)
 * @param  id       串口编号
 * @param  baudrate 波特率
 */
void BSP_USART_Init(BSP_USART_Id_t id, uint32_t baudrate);

/**
 * @brief  完整初始化 (全参数可配)
 * @param  id     串口编号
 * @param  config 配置指针 (包含 DMA 开关 + 回调)
 */
void BSP_USART_InitEx(BSP_USART_Id_t id, const BSP_USART_Config_t *config);

/**
 * @brief  关闭并去初始化 (释放 GPIO/DMA, 关闭时钟, 清空缓冲)
 */
void BSP_USART_DeInit(BSP_USART_Id_t id);

/* ----------------------------------------------------------------
 * 发送 (IT 或 DMA, 非阻塞)
 * ---------------------------------------------------------------- */

/**
 * @brief  发送数据 (拷入内部 TX 缓冲后立即返回)
 * @return 实际入队的字节数, 0 = 忙/未初始化
 * @note   use_dma_tx=1 时通过 DMA 发送, =0 时通过 IT 发送.
 *         返回 < len 说明内部缓冲区空间不足.
 */
uint16_t BSP_USART_Send(BSP_USART_Id_t id, const uint8_t *data, uint16_t len);

/**
 * @brief  零拷贝发送 (直接使用用户缓冲区, 不拷贝)
 * @return len = 已启动, 0 = 忙/未初始化
 * @note   用户必须保证 buffer 生命周期直到 BSP_USART_Flush 返回 0.
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
 */
int BSP_USART_Flush(BSP_USART_Id_t id, uint32_t timeout);

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
