/**
 * @file    bsp_usart.c
 * @brief   BSP USART 驱动实现 — DMA/IT 双模 RX+TX + IDLE 回调
 * @note    RX: HAL_UARTEx_ReceiveToIdle_IT / _DMA → 环形缓冲 (rx_count 累加)
 *              DMA_CIRCULAR: 永不停, 零间隙, 适合任意波特率
 *              IT: HAL 内部 RXNE ISR, IDLE/满时触发回调, 重装
 *          TX: IT 或 DMA 发送 (use_dma_tx 选择)
 *          USART5 无 DMA; UART4_TX DMA 与 SDIO 共享 IRQ, 暂跳过 ISR 注册.
 */

#include "bsp_usart.h"
#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

#include <string.h>

/* 本地延时封装 — 换平台只需改此行 */
static inline void delay_us(uint32_t us) { BSP_SYSTICK_DelayUS(us); }

/* ================================================================
 * 配置常量
 * ================================================================ */

#define USART_RX_BUF_SIZE  256U
#define USART_TX_BUF_SIZE  256U

/* ================================================================
 * 硬件映射表
 * ================================================================ */

typedef struct {
    USART_TypeDef        *instance;
    GPIO_TypeDef         *tx_port;
    uint16_t              tx_pin;
    GPIO_TypeDef         *rx_port;
    uint16_t              rx_pin;
    IRQn_Type             irq_n;
    uint8_t               irq_prio;         /* NVIC 优先级 (0=最高, 15=最低) */
    DMA_Channel_TypeDef  *dma_rx_ch;        /* RX DMA (NULL=无) */
    IRQn_Type             dma_rx_irq;
    uint8_t               dma_rx_prio;      /* DMA RX NVIC 优先级 */
    DMA_Channel_TypeDef  *dma_tx_ch;        /* TX DMA (NULL=无) */
    IRQn_Type             dma_tx_irq;
    uint8_t               dma_tx_prio;      /* DMA TX NVIC 优先级 */
} USART_HW_Map_t;

static const USART_HW_Map_t usart_hw_map[BSP_USART_COUNT] = {
    [BSP_USART1] = { .instance = USART1, .tx_port = GPIOA, .tx_pin = GPIO_PIN_9,
                     .rx_port = GPIOA, .rx_pin = GPIO_PIN_10, .irq_n = USART1_IRQn,
                     .irq_prio = 7,
                     .dma_rx_ch = DMA1_Channel5, .dma_rx_irq = DMA1_Channel5_IRQn,
                     .dma_rx_prio = 5,
                     .dma_tx_ch = DMA1_Channel4, .dma_tx_irq = DMA1_Channel4_IRQn,
                     .dma_tx_prio = 6 },
    [BSP_USART2] = { .instance = USART2, .tx_port = GPIOA, .tx_pin = GPIO_PIN_2,
                     .rx_port = GPIOA, .rx_pin = GPIO_PIN_3,  .irq_n = USART2_IRQn,
                     .irq_prio = 7,
                     .dma_rx_ch = DMA1_Channel6, .dma_rx_irq = DMA1_Channel6_IRQn,
                     .dma_rx_prio = 5,
                     .dma_tx_ch = DMA1_Channel7, .dma_tx_irq = DMA1_Channel7_IRQn,
                     .dma_tx_prio = 6 },
    [BSP_USART3] = { .instance = USART3, .tx_port = GPIOB, .tx_pin = GPIO_PIN_10,
                     .rx_port = GPIOB, .rx_pin = GPIO_PIN_11, .irq_n = USART3_IRQn,
                     .irq_prio = 7,
                     .dma_rx_ch = DMA1_Channel3, .dma_rx_irq = DMA1_Channel3_IRQn,
                     .dma_rx_prio = 5,
                     .dma_tx_ch = DMA1_Channel2, .dma_tx_irq = DMA1_Channel2_IRQn,
                     .dma_tx_prio = 6 },
    [BSP_USART4] = { .instance = UART4,  .tx_port = GPIOC, .tx_pin = GPIO_PIN_10,
                     .rx_port = GPIOC, .rx_pin = GPIO_PIN_11, .irq_n = UART4_IRQn,
                     .irq_prio = 7,
                     .dma_rx_ch = DMA2_Channel3, .dma_rx_irq = DMA2_Channel3_IRQn,
                     .dma_rx_prio = 5,
                     .dma_tx_ch = DMA2_Channel5, .dma_tx_irq = DMA2_Channel4_5_IRQn,
                     .dma_tx_prio = 6 },
    [BSP_USART5] = { .instance = UART5,  .tx_port = GPIOC, .tx_pin = GPIO_PIN_12,
                     .rx_port = GPIOD, .rx_pin = GPIO_PIN_2,  .irq_n = UART5_IRQn,
                     .irq_prio = 7,
                     .dma_rx_ch = NULL,   .dma_rx_irq = 0,
                     .dma_rx_prio = 0,
                     .dma_tx_ch = NULL,   .dma_tx_irq = 0,
                     .dma_tx_prio = 0 },
};

/* ================================================================
 * 运行时上下文
 * ================================================================ */

typedef struct {
    UART_HandleTypeDef  huart;
    DMA_HandleTypeDef   hdma_rx;
    DMA_HandleTypeDef   hdma_tx;
    uint8_t             initialized;
    uint8_t             use_dma_rx;
    uint8_t             use_dma_tx;

    /* RX 环形缓冲 */
    uint8_t             rx_buf[USART_RX_BUF_SIZE];
    volatile uint16_t   rx_count;      /* 累计写入绝对位置 (单调递增不重置) */
    volatile uint16_t   rx_read;       /* App 累计已读量 */
    volatile uint32_t   rx_lost;       /* 因应用读取不及时被覆盖丢弃的字节数，正常应为0，
                                        如果注册接收回调函数也基本不可能出现遗失 */

    /* TX */
    uint8_t             tx_buf[USART_TX_BUF_SIZE];
    volatile uint8_t    tx_busy;
    volatile uint8_t    tx_direct;

    /* 回调 */
    BSP_USART_TxCpltCallback_t   tx_cplt_cb;
    BSP_USART_RxIdleCallback_t   rx_idle_cb;
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
    for (int i = 0; i < BSP_USART_COUNT; i++)
        if (&usart_ctx[i].huart == huart) return &usart_ctx[i];
    return NULL;
}

static void usart_dma_clk_enable(DMA_Channel_TypeDef *dma_ch)
{
    if ((uint32_t)dma_ch >= DMA2_BASE)
        __HAL_RCC_DMA2_CLK_ENABLE();
    else
        __HAL_RCC_DMA1_CLK_ENABLE();
}

static void usart_rx_rearm(USART_Ctx_t *ctx)
{
    if (ctx->use_dma_rx)
        HAL_UARTEx_ReceiveToIdle_DMA(&ctx->huart, ctx->rx_buf, USART_RX_BUF_SIZE);
    else
        HAL_UARTEx_ReceiveToIdle_IT(&ctx->huart, ctx->rx_buf, USART_RX_BUF_SIZE);
}

/* ================================================================
 * HAL 回调
 * ================================================================ */

/**
 * @brief RX 事件回调 — DMA/IT 共用
 *        CIRCULAR:  rx_count = (prev & ~255) + Size, 统一处理 HT/TC/IDLE, 永不重装
 *        IT/NORMAL:  Size = 本帧字节数 → callback → 重装+复位
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    USART_Ctx_t *ctx = ctx_from_huart(huart);
    if (!ctx || Size == 0) return;

    uint8_t is_circ = (ctx->use_dma_rx
                       && ctx->hdma_rx.Init.Mode == DMA_CIRCULAR);

    if (is_circ) {
        /* 对齐到当前周期边界 + 本周期 HAL 报告值 = 绝对位置 */
        ctx->rx_count = (ctx->rx_count & ~(USART_RX_BUF_SIZE - 1U)) + Size;
    } else {
        ctx->rx_count = Size;
    }

    if (ctx->rx_idle_cb) {
        BSP_USART_Id_t id = (BSP_USART_Id_t)(ctx - usart_ctx);
        ctx->rx_idle_cb(id);
    }

    if (is_circ)
        return;
    if (huart->RxEventType == HAL_UART_RXEVENT_HT)
        return;

    ctx->rx_count = 0;
    ctx->rx_read  = 0;
    usart_rx_rearm(ctx);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    USART_Ctx_t *ctx = ctx_from_huart(huart);
    if (!ctx) return;

    ctx->tx_direct = 0;
    ctx->tx_busy   = 0;

    if (ctx->tx_cplt_cb) ctx->tx_cplt_cb();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    USART_Ctx_t *ctx = ctx_from_huart(huart);
    if (!ctx) return;

    (void)HAL_UART_AbortReceive_IT(huart);
    usart_rx_rearm(ctx);
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
 * ISR — USART
 * ================================================================ */

void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&usart_ctx[BSP_USART1].huart); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&usart_ctx[BSP_USART2].huart); }
void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&usart_ctx[BSP_USART3].huart); }
void UART4_IRQHandler(void)  { HAL_UART_IRQHandler(&usart_ctx[BSP_USART4].huart); }
void UART5_IRQHandler(void)  { HAL_UART_IRQHandler(&usart_ctx[BSP_USART5].huart); }

/* ================================================================
 * ISR — DMA RX
 * ================================================================ */

void DMA1_Channel3_IRQHandler(void) { HAL_DMA_IRQHandler(&usart_ctx[BSP_USART3].hdma_rx); }
void DMA1_Channel5_IRQHandler(void) { HAL_DMA_IRQHandler(&usart_ctx[BSP_USART1].hdma_rx); }
void DMA1_Channel6_IRQHandler(void) { HAL_DMA_IRQHandler(&usart_ctx[BSP_USART2].hdma_rx); }
void DMA2_Channel3_IRQHandler(void) { HAL_DMA_IRQHandler(&usart_ctx[BSP_USART4].hdma_rx); }

/* ================================================================
 * ISR — DMA TX
 * ================================================================ */

void DMA1_Channel2_IRQHandler(void) { HAL_DMA_IRQHandler(&usart_ctx[BSP_USART3].hdma_tx); }
void DMA1_Channel4_IRQHandler(void) { HAL_DMA_IRQHandler(&usart_ctx[BSP_USART1].hdma_tx); }
void DMA1_Channel7_IRQHandler(void) { HAL_DMA_IRQHandler(&usart_ctx[BSP_USART2].hdma_tx); }
/* UART4_TX = DMA2_Channel5, 与 SDIO(DMA2_Channel4) 共享 DMA2_Channel4_5_IRQHandler,
   此 IRQ 已在 bsp_sdcard.c 中定义, 在此不再重复注册. */

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

    HAL_NVIC_SetPriority(hw->irq_n, hw->irq_prio, 0);
    HAL_NVIC_EnableIRQ(hw->irq_n);

    ctx->tx_cplt_cb = config->tx_cplt_callback;
    ctx->rx_idle_cb = config->rx_idle_callback;

    /* ---- DMA RX ---- */
    ctx->use_dma_rx = (config->use_dma_rx && hw->dma_rx_ch != NULL) ? 1 : 0;
    if (ctx->use_dma_rx) {
        usart_dma_clk_enable(hw->dma_rx_ch);
        ctx->hdma_rx.Instance                 = hw->dma_rx_ch;
        ctx->hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
        ctx->hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
        ctx->hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
        ctx->hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        ctx->hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
        ctx->hdma_rx.Init.Mode                = DMA_CIRCULAR;
        ctx->hdma_rx.Init.Priority            = DMA_PRIORITY_MEDIUM;
        HAL_DMA_Init(&ctx->hdma_rx);
        __HAL_LINKDMA(huart, hdmarx, ctx->hdma_rx);
        HAL_NVIC_SetPriority(hw->dma_rx_irq, hw->dma_rx_prio, 0);
        HAL_NVIC_EnableIRQ(hw->dma_rx_irq);
    }

    /* ---- DMA TX ---- */
    ctx->use_dma_tx = (config->use_dma_tx && hw->dma_tx_ch != NULL) ? 1 : 0;
    if (ctx->use_dma_tx) {
        usart_dma_clk_enable(hw->dma_tx_ch);
        ctx->hdma_tx.Instance                 = hw->dma_tx_ch;
        ctx->hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
        ctx->hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
        ctx->hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;
        ctx->hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        ctx->hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
        ctx->hdma_tx.Init.Mode                = DMA_NORMAL;
        ctx->hdma_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
        HAL_DMA_Init(&ctx->hdma_tx);
        __HAL_LINKDMA(huart, hdmatx, ctx->hdma_tx);
        /* DMA_TX IRQ: UART4 与 SDIO 共享, 仅 USART1-3 在此注册 */
        if (hw->dma_tx_irq != DMA2_Channel4_5_IRQn) {
            HAL_NVIC_SetPriority(hw->dma_tx_irq, hw->dma_tx_prio, 0);
            HAL_NVIC_EnableIRQ(hw->dma_tx_irq);
        }
    }

    ctx->initialized = 1;
    usart_rx_rearm(ctx);
}

void BSP_USART_DeInit(BSP_USART_Id_t id)
{
    if (id >= BSP_USART_COUNT) return;
    USART_Ctx_t          *ctx = &usart_ctx[id];
    const USART_HW_Map_t *hw  = &usart_hw_map[id];
    if (!ctx->initialized) return;

    HAL_NVIC_DisableIRQ(hw->irq_n);
    if (ctx->use_dma_rx) {
        HAL_NVIC_DisableIRQ(hw->dma_rx_irq);
        (void)HAL_DMA_Abort(&ctx->hdma_rx);
        HAL_DMA_DeInit(&ctx->hdma_rx);
    }
    if (ctx->use_dma_tx && hw->dma_tx_irq != DMA2_Channel4_5_IRQn) {
        HAL_NVIC_DisableIRQ(hw->dma_tx_irq);
        (void)HAL_DMA_Abort(&ctx->hdma_tx);
        HAL_DMA_DeInit(&ctx->hdma_tx);
    }
    (void)HAL_UART_Abort(&ctx->huart);
    HAL_UART_DeInit(&ctx->huart);
    usart_deinit_gpio(hw);
    usart_disable_clk(hw->instance);
    memset(ctx, 0, sizeof(*ctx));
}

/* ----------------------------------------------------------------
 * 发送
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

    if (ctx->use_dma_tx)
        HAL_UART_Transmit_DMA(&ctx->huart, ctx->tx_buf, count);
    else
        HAL_UART_Transmit_IT(&ctx->huart, ctx->tx_buf, count);

    return count;
}

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

    uint32_t elapsed = 0;
    while (ctx->tx_busy) {
        if (timeout > 0 && elapsed >= timeout) return -1;
        delay_us(1000);
        elapsed++;
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

    /* 溢出时只报「满」 —— 不能让调用者拿到比缓冲还大的可读数 */
    uint16_t avail = (uint16_t)(ctx->rx_count - ctx->rx_read);
    return (avail > USART_RX_BUF_SIZE) ? USART_RX_BUF_SIZE : avail;
}

int BSP_USART_Read(BSP_USART_Id_t id, uint8_t *ch)
{
    if (id >= BSP_USART_COUNT || !ch) return -1;
    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized || ctx->rx_read >= ctx->rx_count) return -1;
    *ch = ctx->rx_buf[ctx->rx_read & (USART_RX_BUF_SIZE - 1)];
    ctx->rx_read++;
    return 0;
}

uint16_t BSP_USART_ReadBytes(BSP_USART_Id_t id, uint8_t *buf, uint16_t len)
{
    if (id >= BSP_USART_COUNT || !buf || !len) return 0;
    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return 0;

    uint16_t avail = (uint16_t)(ctx->rx_count - ctx->rx_read);

    /* 溢出检测 —— 应用落后超过一整圈, DMA 已经覆盖了未读数据.
     * 必须在这里重新对齐, 否则 avail 会大于缓冲大小, len 随之超过 SIZE,
     * 下面第二段 memcpy 的 (len - first_chunk) 会越界读 rx_buf.
     * 处理方式: 丢弃最旧的那一段, 只保留缓冲内的最后 SIZE 字节, 并记账.
     * (可被观测的丢失, 好过静默返回被覆盖过的旧数据) */
    if (avail > USART_RX_BUF_SIZE) {
        ctx->rx_lost += (uint32_t)(avail - USART_RX_BUF_SIZE);
        ctx->rx_read  = (uint16_t)(ctx->rx_count - USART_RX_BUF_SIZE);
        avail         = USART_RX_BUF_SIZE;
    }
    if (!avail) return 0;
    if (len > avail) len = avail;

    uint16_t tail_idx = ctx->rx_read & (USART_RX_BUF_SIZE - 1);
    uint16_t first_chunk = USART_RX_BUF_SIZE - tail_idx;
    if (first_chunk >= len) {
        memcpy(buf, &ctx->rx_buf[tail_idx], len);
    } else {
        memcpy(buf, &ctx->rx_buf[tail_idx], first_chunk);
        memcpy(buf + first_chunk, ctx->rx_buf, len - first_chunk);
    }
    ctx->rx_read += len;
    return len;
}

void BSP_USART_FlushRx(BSP_USART_Id_t id)
{
    if (id >= BSP_USART_COUNT) return;
    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return;
    ctx->rx_read = ctx->rx_count;
}

uint32_t BSP_USART_RxLost(BSP_USART_Id_t id)
{
    if (id >= BSP_USART_COUNT) return 0;
    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return 0;
    return ctx->rx_lost;
}

void BSP_USART_ClearRxLost(BSP_USART_Id_t id)
{
    if (id >= BSP_USART_COUNT) return;
    USART_Ctx_t *ctx = &usart_ctx[id];
    if (!ctx->initialized) return;
    ctx->rx_lost = 0;
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
