/**
 * @file    bsp_esp8266.h
 * @brief   BSP ESP8266 WiFi 模块驱动接口
 * @note    硬件连接:
 *              PG13 — WIFI_EN   (CHIP_EN,  高=使能,  低=禁用, 默认悬空)
 *              PG14 — WIFI_RST  (EX_RSTB,  高=正常,  低=复位)
 *              USART3 TX=PB10, RX=PB11  (APB1, 36MHz) — 串口转WiFi
 *              GPIO0 — 手动控制 (升级模式, 不与MCU连接)
 *
 *          本模块仅负责硬件抽象层:
 *            - GPIO 控制 (EN / RST)
 *            - USART3 收发封装 (基于 BSP_USART)
 *            - 硬件复位时序
 *
 *          AT 命令收发/解析属于协议层, 不在本模块范围内,
 *          应放在 Application/Communication/ 下实现.
 */

#ifndef __BSP_ESP8266_H__
#define __BSP_ESP8266_H__

#include <stdint.h>
#include "bsp_usart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 配置结构体
 * ================================================================ */

/**
 * @brief  ESP8266 初始化配置 (透传 USART3 参数)
 * @note   结构体成员与 BSP_USART_Config_t 一一对应,
 *         方便应用层直接使用相同的配置值.
 */
typedef struct {
    uint32_t                    baudrate;          /* 波特率 (默认 115200) */
    BSP_USART_WordLength_t      word_length;       /* 数据字长 */
    BSP_USART_StopBits_t        stop_bits;         /* 停止位   */
    BSP_USART_Parity_t          parity;            /* 奇偶校验 */
    BSP_USART_RxIdleCallback_t  rx_idle_callback;  /* RX IDLE 回调 (NULL = 不需要) */
} BSP_ESP8266_Config_t;

/* ================================================================
 * 初始化 / 去初始化
 * ================================================================ */

/**
 * @brief  简易初始化 ESP8266 (8-N-1, 仅指定波特率)
 * @param  baudrate 波特率 (ESP8266 AT 固件默认 115200)
 * @note   初始化后 EN=高, 模块使能但未复位.
 *         必须在 HAL_Init() + SystemClock_Config() 之后调用.
 */
void BSP_ESP8266_Init(uint32_t baudrate);

/**
 * @brief  完整初始化 ESP8266 (可指定全部串口参数)
 * @param  config ESP8266 配置指针
 */
void BSP_ESP8266_InitEx(const BSP_ESP8266_Config_t *config);

/**
 * @brief  关闭 ESP8266 (去初始化 USART3 + 释放 GPIO)
 */
void BSP_ESP8266_DeInit(void);

/* ================================================================
 * 硬件控制
 * ================================================================ */

/**
 * @brief  使能 ESP8266 (EN 拉高)
 * @note   默认 Init 后 EN 已拉高, 除非先调用了 Disable.
 */
void BSP_ESP8266_Enable(void);

/**
 * @brief  禁用 ESP8266 (EN 拉低, 模块进入最低功耗)
 */
void BSP_ESP8266_Disable(void);

/**
 * @brief  硬件复位 ESP8266
 * @note   时序: RST 拉低 ~10ms → 拉高 → 等待模块启动 (~200ms)
 *         复位后 ESP8266 固件重新启动, 约 200ms 后可响应 AT 命令.
 */
void BSP_ESP8266_Reset(void);

/**
 * @brief  查询模块是否已使能
 * @return 1 = 已使能, 0 = 已禁用
 */
uint8_t BSP_ESP8266_IsEnabled(void);

/* ================================================================
 * 通信 (封装 USART3)
 * ================================================================ */

/**
 * @brief  通过 USART3 向 ESP8266 发送数据 (环形缓冲, 非阻塞)
 * @param  data 待发送数据指针 (入队后即可释放/复用)
 * @param  len  数据长度 (字节)
 * @return 成功入队的字节数, 0 = 缓冲区满/未初始化
 * @note   返回 < len 说明缓冲区空间不足, 剩余部分需调用者续传.
 */
uint16_t BSP_ESP8266_Send(const uint8_t *data, uint16_t len);

/**
 * @brief  通过 USART3 向 ESP8266 发送字符串 (便捷封装)
 * @param  str 以 '\0' 结尾的 C 字符串
 * @return 成功入队的字节数 (不含 '\0'), 或 0
 */
uint16_t BSP_ESP8266_SendString(const char *str);

/**
 * @brief  等待 ESP8266 发送完成 (阻塞)
 * @param  timeout 超时时间 (毫秒), 0 = 死等
 * @return 0 = 发送完成, -1 = 超时
 */
int BSP_ESP8266_Flush(uint32_t timeout);

/**
 * @brief  查询发送缓冲还能入队多少字节
 */
uint16_t BSP_ESP8266_TxSpace(void);

/**
 * @brief  查询发送缓冲中尚未发出的字节数
 */
uint16_t BSP_ESP8266_TxPending(void);

/**
 * @brief  查询发送是否完全空闲 (上一条 AT 命令已完整发出)
 * @return 1 = 空闲, 0 = 仍有数据在途
 */
uint8_t BSP_ESP8266_TxIdle(void);

/**
 * @brief  查询 ESP8266 接收缓冲区中可读字节数
 * @return 可读字节数 (0 = 无数据)
 */
uint16_t BSP_ESP8266_Available(void);

/**
 * @brief  从 ESP8266 读取一个字节 (非阻塞)
 * @param  ch  输出参数, 读取到的字节
 * @return 0 = 成功, -1 = 缓冲区空
 */
int BSP_ESP8266_Read(uint8_t *ch);

/**
 * @brief  从 ESP8266 读取多个字节 (非阻塞)
 * @param  buf 接收缓冲区
 * @param  len 期望读取的字节数
 * @return 实际读取到的字节数 (可能小于 len)
 */
uint16_t BSP_ESP8266_ReadBytes(uint8_t *buf, uint16_t len);

/**
 * @brief  清空 ESP8266 接收缓冲区
 */
void BSP_ESP8266_FlushRx(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ESP8266_H__ */
