/**
 * @file    test_rs485.c
 * @brief   RS485 透传测试 —— USART1 ↔ RS485 双向桥接 (IDLE 回调驱动)
 */

#include "test.h"
#include "bsp_rs485.h"
#include <string.h>

#define BAUD      9600U
#define BUF_SIZE  256U

/* ---- 文件级状态 (主函数与回调共享) ---- */
static uint8_t fwd_buf[BUF_SIZE];

/**
 * @brief  USART1 IDLE 回调: 读 PC 终端数据 → 转发到 RS485
 */
static void on_usart1_rx(BSP_USART_Id_t id)
{
    uint16_t len = BSP_USART_Available(id);
    if (len == 0) return;
    if (len > BUF_SIZE) len = BUF_SIZE;
    len = BSP_USART_ReadBytes(id, fwd_buf, len);
    if (len > 0) {
        BSP_RS485_Send(fwd_buf, len);
    }
}

/**
 * @brief  RS485 (USART2) IDLE 回调: 读 RS485 → 转发到 PC 终端
 */
static void on_rs485_rx(BSP_USART_Id_t id)
{
    (void)id;
    uint16_t len = BSP_RS485_Available();
    if (len == 0) return;
    if (len > BUF_SIZE) len = BUF_SIZE;
    len = BSP_RS485_ReadBytes(fwd_buf, len);
    if (len > 0) {
        BSP_USART_Send(BSP_USART1, fwd_buf, len);
    }
}

/**
 * @brief  RS485 ↔ USART1 透传测试
 */
void Test_RS485_Passthrough(void)
{
    /* ---- USART1: 注册 IDLE 回调 ---- */
    BSP_USART_Config_t usart1_cfg = {
        .baudrate         = BAUD,
        .word_length      = BSP_USART_WORD_8B,
        .stop_bits        = BSP_USART_STOP_1,
        .parity           = BSP_USART_PARITY_NONE,
        .rx_idle_callback = on_usart1_rx,
    };
    BSP_USART_InitEx(BSP_USART1, &usart1_cfg);

    /* ---- RS485: 注册 IDLE 回调 ---- */
    BSP_RS485_Config_t rs485_cfg = {
        .baudrate         = BAUD,
        .word_length      = BSP_USART_WORD_8B,
        .stop_bits        = BSP_USART_STOP_1,
        .parity           = BSP_USART_PARITY_NONE,
        .rx_idle_callback = on_rs485_rx,
    };
    BSP_RS485_InitEx(&rs485_cfg);

    printf("\r\n=== RS485 Passthrough (%u baud) ===\r\n", BAUD);

    while (1) {
        /* 全部工作由 IDLE 回调在 ISR 中完成 */
    }
}
