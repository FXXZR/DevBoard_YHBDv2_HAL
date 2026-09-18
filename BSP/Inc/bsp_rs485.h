/**
 * @file    bsp_rs485.h
 * @brief   BSP RS485 驱动接口 —— SIT3088EESA 半双工 RS485 收发器
 * @note    硬件连接:
 *              USART2  — TX=PA2, RX=PA3  (APB1, 36MHz)
 *              PC2     — DE + RE 直连     (高=发送, 低=接收)
 *
 *          本模块是对 BSP_USART 的简单包装:
 *            - 初始化 PC2 为推挽输出, 默认低电平 (接收模式)
 *            - 通过 BSP_USART_InitEx 配置 USART2 + TX 完成回调
 *            - 发送前自动拉高 DE/RE, 发送完成后回调拉低 DE/RE
 *
 *          应用层通过本模块访问 RS485, 不需要直接操作 USART2.
 */

#ifndef __BSP_RS485_H__
#define __BSP_RS485_H__

#include <stdint.h>
#include "bsp_usart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 配置结构体
 * ================================================================ */

/**
 * @brief  RS485 初始化配置 (透传 USART 参数)
 * @note   结构体成员顺序与 BSP_USART_Config_t 一一对应,
 *         方便应用层直接使用相同的配置值.
 */
typedef struct {
    uint32_t                    baudrate;         /* 波特率 (如 9600, 115200) */
    BSP_USART_WordLength_t      word_length;      /* 数据字长 */
    BSP_USART_StopBits_t        stop_bits;        /* 停止位   */
    BSP_USART_Parity_t          parity;           /* 奇偶校验 */
    BSP_USART_RxIdleCallback_t  rx_idle_callback; /* RX IDLE 回调 (NULL = 不需要) */
} BSP_RS485_Config_t;

/* ================================================================
 * 初始化 / 去初始化
 * ================================================================ */

/**
 * @brief  简易初始化 RS485 (8-N-1, 仅指定波特率)
 * @param  baudrate 波特率 (如 9600, 115200)
 * @note   必须在 HAL_Init() + SystemClock_Config() 之后调用.
 */
void BSP_RS485_Init(uint32_t baudrate);

/**
 * @brief  完整初始化 RS485 (可指定全部串口参数)
 * @param  config RS485 配置指针
 * @note   必须在 HAL_Init() + SystemClock_Config() 之后调用.
 */
void BSP_RS485_InitEx(const BSP_RS485_Config_t *config);

/**
 * @brief  关闭 RS485 收发器 (去初始化 USART2 + 释放 PC2)
 */
void BSP_RS485_DeInit(void);

/* ================================================================
 * 发送 (半双工: 自动切换 DE/RE)
 * ================================================================ */

/**
 * @brief  通过 RS485 发送数据 (环形缓冲, 非阻塞)
 * @param  data 待发送数据指针 (入队后即可释放/复用)
 * @param  len  数据长度 (字节)
 * @return 成功入队的字节数, 0 = 缓冲区满/未初始化
 * @note   发送前自动拉高 DE/RE (发送模式),
 *         环形彻底排空后自动拉低 DE/RE (接收模式).
 *         返回 < len 说明缓冲区空间不足 —— 剩余部分需调用者续传,
 *         此时 DE 保持发送模式, 不会切掉在途数据的尾部.
 */
uint16_t BSP_RS485_Send(const uint8_t *data, uint16_t len);

/**
 * @brief  通过 RS485 发送字符串 (便捷封装)
 * @param  str 以 '\0' 结尾的 C 字符串
 * @return 成功入队的字节数 (不含 '\0'), 或 0
 */
uint16_t BSP_RS485_SendString(const char *str);

/**
 * @brief  等待 RS485 发送完成 (阻塞)
 * @param  timeout 超时时间 (毫秒), 0 = 死等
 * @return 0 = 发送完成, -1 = 超时
 */
int BSP_RS485_Flush(uint32_t timeout);

/**
 * @brief  查询 RS485 发送缓冲还能入队多少字节
 */
uint16_t BSP_RS485_TxSpace(void);

/**
 * @brief  查询 RS485 发送缓冲中尚未发出的字节数
 */
uint16_t BSP_RS485_TxPending(void);

/**
 * @brief  查询 RS485 发送是否完全空闲 (可安全发起下一帧)
 * @return 1 = 空闲, 0 = 仍有数据在途
 */
uint8_t BSP_RS485_TxIdle(void);

/* ================================================================
 * 接收
 * ================================================================ */

/**
 * @brief  查询 RS485 接收缓冲区中可读字节数
 * @return 可读字节数 (0 = 无数据)
 */
uint16_t BSP_RS485_Available(void);

/**
 * @brief  从 RS485 读取一个字节 (非阻塞)
 * @param  ch  输出参数, 读取到的字节
 * @return 0 = 成功, -1 = 缓冲区空
 */
int BSP_RS485_Read(uint8_t *ch);

/**
 * @brief  从 RS485 读取多个字节 (非阻塞)
 * @param  buf 接收缓冲区
 * @param  len 期望读取的字节数
 * @return 实际读取到的字节数 (可能小于 len)
 */
uint16_t BSP_RS485_ReadBytes(uint8_t *buf, uint16_t len);

/**
 * @brief  清空 RS485 接收缓冲区
 */
void BSP_RS485_FlushRx(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RS485_H__ */
