/**
 * @file    bsp_usart.h
 * @brief   BSP USART 驱动接口 —— 平台无关的串口收发 API
 * @note    本文件只暴露抽象接口，不包含任何 HAL/寄存器依赖。
 *          所有 5 路 USART 统一管理，中断收发，环形缓冲。
 */

#ifndef __BSP_USART_H__
#define __BSP_USART_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 串口编号枚举 —— 应用层通过逻辑编号控制，无需关心物理引脚
 * ================================================================ */
typedef enum {
    BSP_USART1 = 0,     /* USART1 — TX=PA9,  RX=PA10 (APB2, 72MHz) */
    BSP_USART2,         /* USART2 — TX=PA2,  RX=PA3  (APB1, 36MHz) */
    BSP_USART3,         /* USART3 — TX=PB10, RX=PB11 (APB1, 36MHz) */
    BSP_USART4,         /* UART4  — TX=PC10, RX=PC11 (APB1, 36MHz) */
    BSP_USART5,         /* UART5  — TX=PC12, RX=PD2  (APB1, 36MHz) */
    BSP_USART_COUNT     /* 串口总数（自动计算） */
} BSP_USART_Id_t;

/* ================================================================
 * 串口配置枚举（平台无关，对应但不依赖 HAL）
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

/** @brief 串口初始化配置结构体（InitEx 使用） */
typedef struct {
    uint32_t               baudrate;     /* 波特率 (如 9600, 115200, 921600) */
    BSP_USART_WordLength_t word_length;  /* 数据字长 */
    BSP_USART_StopBits_t   stop_bits;    /* 停止位   */
    BSP_USART_Parity_t     parity;       /* 奇偶校验 */
} BSP_USART_Config_t;

/* ================================================================
 * 初始化 / 去初始化
 * ================================================================ */

/**
 * @brief  简易初始化指定串口（8-N-1，仅指定波特率）
 * @param  id       串口编号 (BSP_USART1 ~ BSP_USART5)
 * @param  baudrate 波特率 (如 9600, 115200, 921600)
 * @note   必须在 HAL_Init() + SystemClock_Config() 之后调用
 */
void BSP_USART_Init(BSP_USART_Id_t id, uint32_t baudrate);

/**
 * @brief  完整初始化指定串口（可指定全部串口参数）
 * @param  id     串口编号 (BSP_USART1 ~ BSP_USART5)
 * @param  config 串口配置指针
 * @note   必须在 HAL_Init() + SystemClock_Config() 之后调用
 */
void BSP_USART_InitEx(BSP_USART_Id_t id, const BSP_USART_Config_t *config);

/**
 * @brief  关闭并去初始化指定串口（释放 GPIO，关闭时钟，清空缓冲区）
 * @param  id  串口编号
 */
void BSP_USART_DeInit(BSP_USART_Id_t id);

/* ----------------------------------------------------------------
 * 发送 (中断方式, 非阻塞)
 * ---------------------------------------------------------------- */

/**
 * @brief  中断发送数据（拷贝到内部 TX 环形缓冲后立即返回）
 * @param  id   串口编号
 * @param  data 待发送数据指针（调用后即可释放/复用）
 * @param  len  数据长度（字节）
 * @return 成功入队的字节数，0 = 缓冲区满/忙/未初始化
 * @note   返回 < len 说明内部缓冲区空间不足，调用方需自行分段 + 等待
 */
uint16_t BSP_USART_Send(BSP_USART_Id_t id, const uint8_t *data, uint16_t len);

/**
 * @brief  中断发送数据（零拷贝，直接使用用户缓冲区，不拷贝）
 * @param  id   串口编号
 * @param  data 待发送数据指针（必须保持有效直到 BSP_USART_Flush 返回 0！）
 * @param  len  数据长度（字节），无上限
 * @return len = 发送已启动, 0 = 串口忙/未初始化
 * @note   适合发送长数据/大包, 省一次拷贝; 代价是调用方保证 buffer 生命周期
 */
uint16_t BSP_USART_SendDirect(BSP_USART_Id_t id, const uint8_t *data, uint16_t len);

/**
 * @brief  发送字符串（调用 BSP_USART_Send 的便捷封装）
 * @param  id  串口编号
 * @param  str 以 '\0' 结尾的 C 字符串（调用后即可释放）
 * @return 成功入队的字节数（不含 '\0'），或 0
 */
uint16_t BSP_USART_SendString(BSP_USART_Id_t id, const char *str);

/**
 * @brief  等待发送完成（阻塞，直到内部 TX 环形缓冲排空 + 最后帧发送完毕）
 * @param  id       串口编号
 * @param  timeout  超时时间（毫秒），0 = 死等
 * @return 0 = 发送完成, 非 0 = 超时
 */
int BSP_USART_Flush(BSP_USART_Id_t id, uint32_t timeout);

/* ----------------------------------------------------------------
 * 接收 (中断方式, 环形缓冲)
 * ---------------------------------------------------------------- */

/**
 * @brief  查询接收缓冲区中可读字节数
 * @param  id  串口编号
 * @return 可读字节数（0 = 无数据）
 */
uint16_t BSP_USART_Available(BSP_USART_Id_t id);

/**
 * @brief  读取一个字节（非阻塞）
 * @param  id  串口编号
 * @param  ch  输出参数，读取到的字节
 * @return 0 = 成功, -1 = 缓冲区空
 */
int BSP_USART_Read(BSP_USART_Id_t id, uint8_t *ch);

/**
 * @brief  读取多个字节（非阻塞，返回实际读取到的数量）
 * @param  id  串口编号
 * @param  buf 接收缓冲区
 * @param  len 期望读取的字节数
 * @return 实际读取到的字节数（可能小于 len）
 */
uint16_t BSP_USART_ReadBytes(BSP_USART_Id_t id, uint8_t *buf, uint16_t len);

/**
 * @brief  清空指定串口的接收缓冲区
 * @param  id  串口编号
 */
void BSP_USART_FlushRx(BSP_USART_Id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_USART_H__ */
