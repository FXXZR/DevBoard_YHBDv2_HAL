/**
 * @file    bsp_rs485.c
 * @brief   BSP RS485 驱动实现 —— SIT3088EESA + USART2, 软件控制 DE/RE
 * @note    DE (Driver Enable) 和 RE (Receiver Enable) 直连到 PC2.
 *          PC2 高 → 发送模式 (DE=1, RE=1)
 *          PC2 低 → 接收模式 (DE=0, RE=0)
 *
 *          本模块通过 BSP_USART_InitEx 注册 TX 完成回调,
 *          在回调中自动切回接收模式, 实现透明的方向控制.
 */

#include "bsp_rs485.h"
#include "bsp_usart.h"
#include "stm32f1xx_hal.h"

#include <string.h>

/* ================================================================
 * 硬件映射
 * ================================================================ */

/* SIT3088EESA: DE (Pin4) + RE (Pin3) 直连到 PC2 */
#define RS485_DE_RE_PORT        GPIOC
#define RS485_DE_RE_PIN         GPIO_PIN_2
#define RS485_USART_ID          BSP_USART2

/* ================================================================
 * 运行时上下文
 * ================================================================ */

static volatile uint8_t rs485_initialized = 0;

/* TX 环形缓冲 —— USART2 的使用者就是本模块, 所以由本模块提供.
 * Modbus RTU 最大帧 256 字节, 开 256B 可整帧入队, 无需调用者续传 */
static uint8_t rs485_tx_buf[256];

/* ================================================================
 * TX 发送完成回调 (ISR 上下文)
 * ================================================================ */

/**
 * @brief  USART2 TX 完成回调 —— 切回接收模式
 * @note   在 HAL_UART_TxCpltCallback 中调用, 处于 ISR 上下文.
 *         仅操作 GPIO, 耗时极短.
 */
static void rs485_tx_cplt_cb(void)
{
    HAL_GPIO_WritePin(RS485_DE_RE_PORT, RS485_DE_RE_PIN, GPIO_PIN_RESET);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief  简易初始化 RS485 (8-N-1, 仅指定波特率)
 */
void BSP_RS485_Init(uint32_t baudrate)
{
    BSP_RS485_Config_t cfg = {
        .baudrate    = baudrate,
        .word_length = BSP_USART_WORD_8B,
        .stop_bits   = BSP_USART_STOP_1,
        .parity      = BSP_USART_PARITY_NONE,
    };
    BSP_RS485_InitEx(&cfg);
}

/**
 * @brief  完整初始化 RS485 (可指定全部串口参数)
 * @note   1. 初始化 PC2 为推挽输出, 默认低电平 (接收模式)
 *         2. 通过 BSP_USART_InitEx 初始化 USART2 并注册 TX 完成回调
 */
void BSP_RS485_InitEx(const BSP_RS485_Config_t *config)
{
    if (!config) return;

    if (rs485_initialized) {
        BSP_RS485_DeInit();
    }

    /* ---- PC2: DE + RE 方向控制 ---- */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = RS485_DE_RE_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RS485_DE_RE_PORT, &gpio);

    /* 默认接收模式 */
    HAL_GPIO_WritePin(RS485_DE_RE_PORT, RS485_DE_RE_PIN, GPIO_PIN_RESET);

    /* ---- USART2: 中断收发 + TX 完成回调 ---- */
    BSP_USART_Config_t usart_cfg = {
        .baudrate         = config->baudrate,
        .word_length      = config->word_length,
        .stop_bits        = config->stop_bits,
        .parity           = config->parity,
        .tx_cplt_callback = rs485_tx_cplt_cb,
        .rx_idle_callback = config->rx_idle_callback,
        .tx_buf           = rs485_tx_buf,
        .tx_buf_size      = sizeof(rs485_tx_buf),
    };
    BSP_USART_InitEx(RS485_USART_ID, &usart_cfg);

    rs485_initialized = 1;
}

/**
 * @brief  关闭 RS485 收发器
 */
void BSP_RS485_DeInit(void)
{
    if (!rs485_initialized) return;

    BSP_USART_DeInit(RS485_USART_ID);
    HAL_GPIO_DeInit(RS485_DE_RE_PORT, RS485_DE_RE_PIN);
    rs485_initialized = 0;
}

/* ----------------------------------------------------------------
 * 发送
 * ---------------------------------------------------------------- */

/**
 * @brief  通过 RS485 发送数据 (环形缓冲 + DMA, 非阻塞)
 * @return 实际入队的字节数; < len 表示缓冲空间不足, 剩余部分由调用者续传
 * @note   发送前拉高 DE/RE, 环形彻底排空后由 ISR 回调自动拉低.
 *
 *         方向控制不能只看返回值: 环形缓冲满时 ret 可能为 0, 但那恰恰说明
 *         还有数据在途 —— 此时拉低 DE 会把在途数据的尾部切掉.
 *         所以用 TxIdle() 判断, 它同时覆盖「环形空」和「无在途传输」两个条件.
 */
uint16_t BSP_RS485_Send(const uint8_t *data, uint16_t len)
{
    if (!rs485_initialized || !data || !len) return 0;

    /* 进入发送模式 */
    HAL_GPIO_WritePin(RS485_DE_RE_PORT, RS485_DE_RE_PIN, GPIO_PIN_SET);

    uint16_t ret = BSP_USART_Write(RS485_USART_ID, data, len);

    if (BSP_USART_TxIdle(RS485_USART_ID)) {
        /* 总线上确实什么都没有 —— 立即收回方向, 避免占着总线 */
        HAL_GPIO_WritePin(RS485_DE_RE_PORT, RS485_DE_RE_PIN, GPIO_PIN_RESET);
    }
    /* 否则: TX 空闲回调 rs485_tx_cplt_cb() 会在环形排空后切回接收模式 */

    return ret;
}

/**
 * @brief  通过 RS485 发送字符串
 */
uint16_t BSP_RS485_SendString(const char *str)
{
    if (!str) return 0;
    return BSP_RS485_Send((const uint8_t *)str, (uint16_t)strlen(str));
}

/**
 * @brief  等待 RS485 发送完成 (阻塞)
 */
int BSP_RS485_Flush(uint32_t timeout)
{
    if (!rs485_initialized) return -1;
    return BSP_USART_Flush(RS485_USART_ID, timeout);
}

/**
 * @brief  查询 RS485 发送缓冲还能入队多少字节
 * @note   应用层通过本模块访问 USART2, 不直接调用 BSP_USART_* ——
 *         否则会绕过本模块对串口的所有权约定 (DE 方向控制).
 */
uint16_t BSP_RS485_TxSpace(void)
{
    return BSP_USART_TxSpace(RS485_USART_ID);
}

/**
 * @brief  查询 RS485 发送缓冲中尚未发出的字节数
 */
uint16_t BSP_RS485_TxPending(void)
{
    return BSP_USART_TxPending(RS485_USART_ID);
}

/**
 * @brief  查询 RS485 发送是否完全空闲
 * @return 1 = 空闲 (可安全发起下一帧), 0 = 仍有数据在途
 * @note   Modbus RTU 是请求-应答式的, 从机应答完才轮到下一帧,
 *         判断"上一帧是否真的发完"应该用这个而不是自己数时间.
 */
uint8_t BSP_RS485_TxIdle(void)
{
    return BSP_USART_TxIdle(RS485_USART_ID);
}

/* ----------------------------------------------------------------
 * 接收
 * ---------------------------------------------------------------- */

/**
 * @brief  查询 RS485 接收缓冲区中可读字节数
 */
uint16_t BSP_RS485_Available(void)
{
    if (!rs485_initialized) return 0;
    return BSP_USART_Available(RS485_USART_ID);
}

/**
 * @brief  从 RS485 读取一个字节 (非阻塞)
 */
int BSP_RS485_Read(uint8_t *ch)
{
    if (!rs485_initialized) return -1;
    return BSP_USART_Read(RS485_USART_ID, ch);
}

/**
 * @brief  从 RS485 读取多个字节 (非阻塞)
 */
uint16_t BSP_RS485_ReadBytes(uint8_t *buf, uint16_t len)
{
    if (!rs485_initialized) return 0;
    return BSP_USART_ReadBytes(RS485_USART_ID, buf, len);
}

/**
 * @brief  清空 RS485 接收缓冲区
 */
void BSP_RS485_FlushRx(void)
{
    if (!rs485_initialized) return;
    BSP_USART_FlushRx(RS485_USART_ID);
}
