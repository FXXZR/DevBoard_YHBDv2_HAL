/**
 * @file    test_dma_echo.c
 * @brief   USART1 DMA 收发回显测试
 * @note    USART1: use_dma_rx=1 + use_dma_tx=1.
 *          RX 半满时提前发送前 128B, IDLE 时发送剩余部分.
 *          帧长 > 256B 时靠半满中断分段回显.
 */

#include "test.h"
#include <string.h>

#define BUF_SIZE  256U

/* ---- 文件级: DMA TX 忙时暂存 ---- */
static uint8_t  pending_buf[BUF_SIZE];
static uint16_t pending_len = 0;

/**
 * @brief  USART1 IDLE/半满回调: 读数据 → DMA 回显
 */
static void on_echo_rx(BSP_USART_Id_t id)
{
    uint8_t  buf[BUF_SIZE];
    uint16_t len = BSP_USART_Available(id);
    if (len == 0) return;
    if (len > BUF_SIZE) len = BUF_SIZE;
    len = BSP_USART_ReadBytes(id, buf, len);
    if (len == 0) return;

    /* 尝试 DMA 发送; 若 TX 忙 (上一半满数据还在发送中) 则暂存 */
    if (BSP_USART_Send(BSP_USART1, buf, len) == 0) {
        if (len <= BUF_SIZE) {
            memcpy(pending_buf, buf, len);
            pending_len = len;
        }
    }
}

/**
 * @brief  USART1 TX 完成回调: 发送暂存数据
 */
static void on_echo_tx_done(void)
{
    if (pending_len > 0) {
        BSP_USART_Send(BSP_USART1, pending_buf, pending_len);
        pending_len = 0;
    }
}

/**
 * @brief  USART1 DMA 收发回显测试
 */
void Test_DMA_Echo(void)
{
    BSP_USART_Config_t cfg = {
        .baudrate         = 115200,
        .word_length      = BSP_USART_WORD_8B,
        .stop_bits        = BSP_USART_STOP_1,
        .parity           = BSP_USART_PARITY_NONE,
        .rx_idle_callback = on_echo_rx,
        .tx_cplt_callback = on_echo_tx_done,
        .use_dma_rx       = 1,
        .use_dma_tx       = 1,
    };
    BSP_USART_InitEx(BSP_USART1, &cfg);

    printf("\r\n=== DMA Echo Test (USART1, 115200 baud) ===\r\n");
    printf("RX:DMA  TX:DMA  Half:early-send\r\n\r\n");

    while (1) { /* 回调驱动 */ }
}
