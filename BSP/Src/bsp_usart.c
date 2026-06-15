/**
 * @file    bsp_usart.c
 * @brief   BSP USART 驱动实现 —— 基于 STM32 HAL 的中断串口收发
 * @note    全 5 路串口统一管理。
 *          RX: 中断单字节接收 → 环形缓冲区
 *          TX: 两种模式 —
 *              Send:       用户数据 → memcpy → 内部线性缓冲 → HAL_UART_Transmit_IT
 *              SendDirect: 用户数据 → 直接交付 HAL_IT（零拷贝，用户保证生命周期）
 *          更换 MCU 或引脚时，只需修改 usart_hw_map[] 映射表。
 *
 *          HAL 回调通过弱符号覆盖注册（非函数指针）。
 */

#include "bsp_usart.h"
#include "stm32f1xx_hal.h"

#include <string.h>

/* ================================================================
 * 配置常量
 * ================================================================ */

#define USART_RX_BUF_SIZE  256U  /* RX 环形缓冲区大小 (2 的幂) */
#define USART_TX_BUF_SIZE  256U  /* TX 线性缓冲区大小           */

_Static_assert((USART_RX_BUF_SIZE & (USART_RX_BUF_SIZE - 1)) == 0,
               "USART_RX_BUF_SIZE must be power of 2");

#define RX_BUF_MASK (USART_RX_BUF_SIZE - 1U)

/* ================================================================
 * 硬件映射表
 * ================================================================ */

typedef struct {
    USART_TypeDef *instance;
    GPIO_TypeDef  *tx_port;
    uint16_t       tx_pin;
    GPIO_TypeDef  *rx_port;
    uint16_t       rx_pin;
    IRQn_Type      irq_n;
} USART_HW_Map_t;

static const USART_HW_Map_t usart_hw_map[BSP_USART_COUNT] = {
    [BSP_USART1] = { .instance = USART1, .tx_port = GPIOA, .tx_pin = GPIO_PIN_9,
                     .rx_port = GPIOA, .rx_pin = GPIO_PIN_10, .irq_n = USART1_IRQn },
    [BSP_USART2] = { .instance = USART2, .tx_port = GPIOA, .tx_pin = GPIO_PIN_2,
                     .rx_port = GPIOA, .rx_pin = GPIO_PIN_3,  .irq_n = USART2_IRQn },
    [BSP_USART3] = { .instance = USART3, .tx_port = GPIOB, .tx_pin = GPIO_PIN_10,
                     .rx_port = GPIOB, .rx_pin = GPIO_PIN_11, .irq_n = USART3_IRQn },
    [BSP_USART4] = { .instance = UART4,  .tx_port = GPIOC, .tx_pin = GPIO_PIN_10,
                     .rx_port = GPIOC, .rx_pin = GPIO_PIN_11, .irq_n = UART4_IRQn },
    [BSP_USART5] = { .instance = UART5,  .tx_port = GPIOC, .tx_pin = GPIO_PIN_12,
                     .rx_port = GPIOD, .rx_pin = GPIO_PIN_2,  .irq_n = UART5_IRQn },
};

/* ================================================================
 * 运行时上下文
 * ================================================================ */

typedef struct {
    UART_HandleTypeDef huart;
    uint8_t             initialized;

    /* RX 环形缓冲区 — ISR (head) / App (tail), uint16_t 自然溢出回绕 */
    uint8_t         rx_buf[USART_RX_BUF_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    uint8_t         rx_byte;

    /* TX 线性缓冲区 — 拷入后直接交 HAL_UART_Transmit_IT 一次性发出 */
    uint8_t         tx_buf[USART_TX_BUF_SIZE];
    volatile uint8_t  tx_busy;       /* 发送进行中 */
    volatile uint8_t  tx_direct;     /* 当前是 SendDirect 模式 */
} USART_Ctx_t;

static USART_Ctx_t usart_ctx[BSP_USART_COUNT];

/* ================================================================
 * 内部辅助
 * ================================================================ */

static uint32_t conv_word_length(BSP_USART_WordLength_t wl)
{
    return (wl == BSP_USART_WORD_9B) ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
}

static uint32_t conv_stop_bits(BSP_USART_StopBits_t sb)
{
    return (sb == BSP_USART_STOP_2) ? UART_STOPBITS_2 : UART_STOPBITS_1;
}

static uint32_t conv_parity(BSP_USART_Parity_t par)
{
    if (par == BSP_USART_PARITY_EVEN) return UART_PARITY_EVEN;
    if (par == BSP_USART_PARITY_ODD)  return UART_PARITY_ODD;
    return UART_PARITY_NONE;
}

static USART_Ctx_t *ctx_from_huart(UART_HandleTypeDef *huart)
{
    for (int i = 0; i < BSP_USART_COUNT; i++) {
        if (&usart_ctx[i].huart == huart) return &usart_ctx[i];
    }
    return NULL;
}

/* ================================================================
 * HAL 回调
 * ================================================================ */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    USART_Ctx_t *ctx = ctx_from_huart(huart);
    if (!ctx) return;

    uint16_t head = ctx->rx_head;
    if ((uint16_t)(head - ctx->rx_tail) <= RX_BUF_MASK) {
        ctx->rx_buf[head & RX_BUF_MASK] = ctx->rx_byte;
        ctx->rx_head = head + 1;
    }
    HAL_UART_Receive_IT(huart, &ctx->rx_byte, 1);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    USART_Ctx_t *ctx = ctx_from_huart(huart);
    if (!ctx) return;

    /* 无论 Send 还是 SendDirect，发完就清标志 */
    ctx->tx_direct = 0;
    ctx->tx_busy   = 0;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    USART_Ctx_t *ctx = ctx_from_huart(huart);
    if (!ctx) return;

    (void)HAL_UART_AbortReceive_IT(huart);
    HAL_UART_Receive_IT(huart, &ctx->rx_byte, 1);
}

/* ================================================================
 * 时钟 / GPIO
 * ================================================================ */

static void usart_enable_clk(USART_TypeDef *inst)
{
    if (inst == USART1) { __HAL_RCC_USART1_CLK_ENABLE(); __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (inst == USART2) { __HAL_RCC_USART2_CLK_ENABLE(); __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (inst == USART3) { __HAL_RCC_USART3_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (inst == UART4)  { __HAL_RCC_UART4_CLK_ENABLE();  __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (inst == UART5)  { __HAL_RCC_UART5_CLK_ENABLE();
                               __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE(); }
}

static void usart_disable_clk(USART_TypeDef *inst)
{
    if (inst == USART1)      { __HAL_RCC_USART1_CLK_DISABLE(); }
    else if (inst == USART2) { __HAL_RCC_USART2_CLK_DISABLE(); }
    else if (inst == USART3) { __HAL_RCC_USART3_CLK_DISABLE(); }
    else if (inst == UART4)  { __HAL_RCC_UART4_CLK_DISABLE();  }
    else if (inst == UART5)  { __HAL_RCC_UART5_CLK_DISABLE();  }
}

static void usart_init_gpio(const USART_HW_Map_t *hw)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = hw->tx_pin;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(hw->tx_port, &gpio);

    gpio.Pin  = hw->rx_pin;
    gpio.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(hw->rx_port, &gpio);
}

static void usart_deinit_gpio(const USART_HW_Map_t *hw)
{
    HAL_GPIO_DeInit(hw->tx_port, hw->tx_pin);
    HAL_GPIO_DeInit(hw->rx_port, hw->rx_pin);
}

/* ================================================================
 * USART ISR (覆盖 startup 的弱 Default_Handler)
 * ================================================================ */

void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&usart_ctx[BSP_USART1].huart); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&usart_ctx[BSP_USART2].huart); }
void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&usart_ctx[BSP_USART3].huart); }
void UART4_IRQHandler(void)  { HAL_UART_IRQHandler(&usart_ctx[BSP_USART4].huart); }
void UART5_IRQHandler(void)  { HAL_UART_IRQHandler(&usart_ctx[BSP_USART5].huart); }

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_USART_Init(BSP_USART_Id_t id, uint32_t baudrate)
{
    BSP_USART_Config_t cfg = {
        .baudrate = baudrate, .word_length = BSP_USART_WORD_8B,
        .stop_bits = BSP_USART_STOP_1, .parity = BSP_USART_PARITY_NONE,
    };
    BSP_USART_InitEx(id, &cfg);
}

void BSP_USART_InitEx(BSP_USART_Id_t id, const BSP_USART_Config_t *config)
{
    if (id >= BSP_USART_COUNT || !config) return;

    const USART_HW_Map_t *hw  = &usart_hw_map[id];
    USART_Ctx_t          *ctx = &usart_ctx[id];

    if (ctx->initialized) BSP_USART_DeInit(id);
    memset(ctx, 0, sizeof(*ctx));

    usart_enable_clk(hw->instance);
    usart_init_gpio(hw);

    UART_HandleTypeDef *huart = &ctx->huart;
    huart->Instance          = hw->instance;
    huart->Init.BaudRate     = config->baudrate;
    huart->Init.WordLength   = conv_word_length(config->word_length);
    huart->Init.StopBits     = conv_stop_bits(config->stop_bits);
    huart->Init.Parity       = conv_parity(config->parity);
    huart->Init.Mode         = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(huart);

    HAL_NVIC_SetPriority(hw->irq_n, 1, 0);
    HAL_NVIC_EnableIRQ(hw->irq_n);

    ctx->initialized = 1;
    HAL_UART_Receive_IT(huart, &ctx->rx_byte, 1);
}

void BSP_USART_DeInit(BSP_USART_Id_t id)
{
    if (id >= BSP_USART_COUNT) return;
    USART_Ctx_t          *ctx = &usart_ctx[id];
    const USART_HW_Map_t *hw  = &usart_hw_map[id];
    if (!ctx->initialized) return;

    HAL_NVIC_DisableIRQ(hw->irq_n);
    (void)HAL_UART_Abort(&ctx->huart);
    HAL_UART_DeInit(&ctx->huart);
    usart_deinit_gpio(hw);
    usart_disable_clk(hw->instance);
    memset(ctx, 0, sizeof(*ctx));
}

/* ----------------------------------------------------------------
 * 发送 — Send: 拷入线性缓冲，一次交给 HAL
 * ---------------------------------------------------------------- */

uint16_t BSP_USART_Send(BSP_USART_Id_t id, const uint8_t *data, uint16_t len)
{
    if (id >= BSP_USART_COUNT || !data || !len) return 0;

    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized || ctx->tx_busy) return 0;

    uint16_t count = (len <= USART_TX_BUF_SIZE) ? len : USART_TX_BUF_SIZE;
    memcpy(ctx->tx_buf, data, count);

    ctx->tx_direct = 0;
    ctx->tx_busy   = 1;
    HAL_UART_Transmit_IT(&ctx->huart, ctx->tx_buf, count);
    return count;
}

/* ----------------------------------------------------------------
 * 发送 — SendDirect: 零拷贝，直接交用户 buffer
 * ---------------------------------------------------------------- */

uint16_t BSP_USART_SendDirect(BSP_USART_Id_t id, const uint8_t *data, uint16_t len)
{
    if (id >= BSP_USART_COUNT || !data || !len) return 0;

    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized || ctx->tx_busy) return 0;

    ctx->tx_direct = 1;
    ctx->tx_busy   = 1;
    HAL_UART_Transmit_IT(&ctx->huart, (uint8_t *)data, len);
    return len;
}

uint16_t BSP_USART_SendString(BSP_USART_Id_t id, const char *str)
{
    if (!str) return 0;
    return BSP_USART_Send(id, (const uint8_t *)str, (uint16_t)strlen(str));
}

int BSP_USART_Flush(BSP_USART_Id_t id, uint32_t timeout)
{
    if (id >= BSP_USART_COUNT) return -1;

    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return -1;

    uint32_t tickstart = HAL_GetTick();
    while (ctx->tx_busy) {
        if (timeout > 0 && (HAL_GetTick() - tickstart) >= timeout) return -1;
    }
    return 0;
}

/* ----------------------------------------------------------------
 * 接收
 * ---------------------------------------------------------------- */

uint16_t BSP_USART_Available(BSP_USART_Id_t id)
{
    if (id >= BSP_USART_COUNT) return 0;
    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return 0;
    return (uint16_t)(ctx->rx_head - ctx->rx_tail);
}

int BSP_USART_Read(BSP_USART_Id_t id, uint8_t *ch)
{
    if (id >= BSP_USART_COUNT || !ch) return -1;

    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return -1;

    uint16_t tail = ctx->rx_tail;
    if (tail == ctx->rx_head) return -1;

    *ch = ctx->rx_buf[tail & RX_BUF_MASK];
    ctx->rx_tail = tail + 1;
    return 0;
}

uint16_t BSP_USART_ReadBytes(BSP_USART_Id_t id, uint8_t *buf, uint16_t len)
{
    if (id >= BSP_USART_COUNT || !buf || !len) return 0;

    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return 0;

    uint16_t tail  = ctx->rx_tail;
    uint16_t avail = (uint16_t)(ctx->rx_head - tail);
    if (!avail) return 0;
    if (len > avail) len = avail;

    uint16_t tail_idx    = tail & RX_BUF_MASK;
    uint16_t first_chunk = USART_RX_BUF_SIZE - tail_idx;

    if (first_chunk >= len) {
        memcpy(buf, &ctx->rx_buf[tail_idx], len);
    } else {
        memcpy(buf, &ctx->rx_buf[tail_idx], first_chunk);
        memcpy(buf + first_chunk, ctx->rx_buf, len - first_chunk);
    }

    ctx->rx_tail = tail + len;
    return len;
}

void BSP_USART_FlushRx(BSP_USART_Id_t id)
{
    if (id >= BSP_USART_COUNT) return;
    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return;
    ctx->rx_tail = ctx->rx_head;
}

/* ================================================================
 * printf 重定向 — __io_putchar (USART1 阻塞发送)
 * ================================================================ */

int __io_putchar(int ch)
{
    if (ch == '\n') {
        uint8_t cr = '\r';
        HAL_UART_Transmit(&usart_ctx[BSP_USART1].huart, &cr, 1, HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(&usart_ctx[BSP_USART1].huart, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
