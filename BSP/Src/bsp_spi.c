/**
 * @file    bsp_spi.c
 * @brief   BSP 硬件 SPI 驱动实现 —— 基于 STM32F1 HAL SPI 的主机模式
 * @note    多总线: SPI1 (PA5/PA6/PA7, APB2=72MHz) 与 SPI2 (PB13/14/15, APB1=36MHz)
 *          三个引脚均配为 AF 推挽。
 *          CS (NSS) 由上层模块软件管理 (NSS_SOFT 模式)。
 *          注: 同一分频值在两条总线上实际波特率差一倍, 见 bsp_spi.h 的说明。
 *
 *          架构：
 *            遵循 bsp_usart 模式 — static const HW 映射表 + static ctx 数组。
 *            GPIO + 时钟配置在 Init 中内联完成。
 *
 *          P2 改进: Transfer/TransferBuffer 暴露超时参数，不再写死。
 */

#include "bsp_spi.h"
#include "bsp_assert.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 硬件映射表 (SPI1 / SPI2) — 加新总线只需在此补一行
 * ================================================================ */

/* SPI 模式 (CPOL/CPHA) 放在映射表里而不是 Init 参数里:
 *   模式是【总线级的硬件属性】, 由挂在这条总线上的从机决定 ——
 *   和 SCK/MISO/MOSI 是同一类东西. 分频值则相反, 是应用层根据
 *   实际速率需要做的选择, 所以留在 Init 参数里.
 *
 *   注意 SPI 模式是【每条总线一档】, 不做分时切换: 同一条总线上
 *   挂的从机必须同模式 (F103 有 3 条 SPI, 够分组). */
typedef struct {
    SPI_TypeDef  *instance;
    GPIO_TypeDef *sck_port;  uint16_t sck_pin;
    GPIO_TypeDef *miso_port; uint16_t miso_pin;
    GPIO_TypeDef *mosi_port; uint16_t mosi_pin;
    uint32_t      clk_polarity;   /* SPI_POLARITY_LOW = 空闲低 (模式 0/1)  */
    uint32_t      clk_phase;      /* SPI_PHASE_1EDGE   = 第1边沿采样 (模式 0/2) */
} SPI_HW_Map_t;

static const SPI_HW_Map_t spi_hw_map[BSP_SPI_BUS_COUNT] = {
    [BSP_SPI_BUS_1] = {
        .instance  = SPI1,
        .sck_port  = GPIOA, .sck_pin  = GPIO_PIN_5,
        .miso_port = GPIOA, .miso_pin = GPIO_PIN_6,
        .mosi_port = GPIOA, .mosi_pin = GPIO_PIN_7,
        /* 模式 0 —— W25Q64 (见 bsp_spi_flash.c) */
        .clk_polarity = SPI_POLARITY_LOW,
        .clk_phase    = SPI_PHASE_1EDGE,
    },
    [BSP_SPI_BUS_2] = {
        .instance  = SPI2,
        .sck_port  = GPIOB, .sck_pin  = GPIO_PIN_13,
        .miso_port = GPIOB, .miso_pin = GPIO_PIN_14,
        .mosi_port = GPIOB, .mosi_pin = GPIO_PIN_15,
        /* 模式 3 —— W5500 (见 bsp_w5500.c).
         * W5500 手册称模式 0/3 都支持, 这里对齐野火官方例程用的模式 3,
         * 便于首轮排查时排除"模式"这个变量. */
        .clk_polarity = SPI_POLARITY_HIGH,
        .clk_phase    = SPI_PHASE_2EDGE,
    },
};

/* ================================================================
 * 内部常量 — BaudPrescaler enum → HAL 值映射
 * ================================================================ */

static const uint32_t kPrescalerToHal[] = {
    [BSP_SPI_BAUD_DIV_2]   = SPI_BAUDRATEPRESCALER_2,
    [BSP_SPI_BAUD_DIV_4]   = SPI_BAUDRATEPRESCALER_4,
    [BSP_SPI_BAUD_DIV_8]   = SPI_BAUDRATEPRESCALER_8,
    [BSP_SPI_BAUD_DIV_16]  = SPI_BAUDRATEPRESCALER_16,
    [BSP_SPI_BAUD_DIV_32]  = SPI_BAUDRATEPRESCALER_32,
    [BSP_SPI_BAUD_DIV_64]  = SPI_BAUDRATEPRESCALER_64,
    [BSP_SPI_BAUD_DIV_128] = SPI_BAUDRATEPRESCALER_128,
    [BSP_SPI_BAUD_DIV_256] = SPI_BAUDRATEPRESCALER_256,
};

/* ================================================================
 * 运行时上下文
 * ================================================================ */

typedef struct {
    SPI_HandleTypeDef handle;
    uint8_t           initialized;
} SPI_Ctx_t;

static SPI_Ctx_t spi_ctx[BSP_SPI_BUS_COUNT];

/* ================================================================
 * 内部辅助
 * ================================================================ */

static void spi_enable_port_clk(GPIO_TypeDef *port)
{
    if      (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
}

/* 时钟使能由映射表驱动 —— 加新总线只需补一行映射, 不必改这里 */
static void spi_enable_clk(const SPI_HW_Map_t *hw)
{
    spi_enable_port_clk(hw->sck_port);
    spi_enable_port_clk(hw->miso_port);
    spi_enable_port_clk(hw->mosi_port);

    if      (hw->instance == SPI1) __HAL_RCC_SPI1_CLK_ENABLE();
    else if (hw->instance == SPI2) __HAL_RCC_SPI2_CLK_ENABLE();
    else if (hw->instance == SPI3) __HAL_RCC_SPI3_CLK_ENABLE();
}

static void spi_init_gpio(const SPI_HW_Map_t *hw)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* SCK */
    gpio.Pin  = hw->sck_pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(hw->sck_port, &gpio);

    /* MISO */
    gpio.Pin  = hw->miso_pin;
    HAL_GPIO_Init(hw->miso_port, &gpio);

    /* MOSI */
    gpio.Pin  = hw->mosi_pin;
    HAL_GPIO_Init(hw->mosi_port, &gpio);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_SPI_Init(BSP_SPI_Bus_Id_t id, BSP_SPI_BaudPrescaler_t prescaler)
{
    /* 参数契约 —— 只可能来自代码写错, 外部世界无法触发, 所以用断言而不是
     * 静默 return: 传错枚举时"什么都不发生", 会让你去查接线/查从机,
     * 折腾半天才发现是总线号写错了. */
    BSP_ASSERT(id < BSP_SPI_BUS_COUNT);
    BSP_ASSERT(prescaler <= BSP_SPI_BAUD_DIV_256);

    SPI_Ctx_t          *ctx = &spi_ctx[id];
    const SPI_HW_Map_t *hw  = &spi_hw_map[id];

    if (ctx->initialized) return;
    ctx->initialized = 1;

    /* 1. 时钟 + GPIO */
    spi_enable_clk(hw);
    spi_init_gpio(hw);

    /* 2. HAL SPI 初始化 */
    ctx->handle.Instance               = hw->instance;
    ctx->handle.Init.BaudRatePrescaler = kPrescalerToHal[prescaler];
    ctx->handle.Init.Direction         = SPI_DIRECTION_2LINES;
    ctx->handle.Init.CLKPhase          = hw->clk_phase;      /* 每条总线固定 */
    ctx->handle.Init.CLKPolarity       = hw->clk_polarity;   /* 见映射表注释 */
    ctx->handle.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    ctx->handle.Init.CRCPolynomial     = 7;
    ctx->handle.Init.DataSize          = SPI_DATASIZE_8BIT;
    ctx->handle.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    ctx->handle.Init.NSS               = SPI_NSS_SOFT;
    ctx->handle.Init.TIMode            = SPI_TIMODE_DISABLE;
    ctx->handle.Init.Mode              = SPI_MODE_MASTER;

    HAL_SPI_Init(&ctx->handle);
    __HAL_SPI_ENABLE(&ctx->handle);
}

uint8_t BSP_SPI_Transfer(BSP_SPI_Bus_Id_t id, uint8_t data, uint32_t timeout)
{
    BSP_ASSERT(id < BSP_SPI_BUS_COUNT);
    BSP_ASSERT(spi_ctx[id].initialized);   /* 忘了 BSP_SPI_Init */
    BSP_ASSERT(__get_IPSR() == 0);         /* ISR 里不做阻塞式传输 */

    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&spi_ctx[id].handle, &data, &rx, 1, timeout);
    return rx;
}

BSP_SPI_Status_t BSP_SPI_TransferBuffer(BSP_SPI_Bus_Id_t id,
                                        const uint8_t *tx, uint8_t *rx, uint16_t len,
                                        uint32_t timeout)
{
    HAL_StatusTypeDef hal_st;

    BSP_ASSERT(id < BSP_SPI_BUS_COUNT);
    BSP_ASSERT(spi_ctx[id].initialized);
    BSP_ASSERT(__get_IPSR() == 0);         /* ISR 里不做阻塞式传输 */
    BSP_ASSERT(tx != NULL);
    BSP_ASSERT(len > 0);                   /* 零长度"传输"几乎总是算错了长度 */

    if (rx) {
        hal_st = HAL_SPI_TransmitReceive(&spi_ctx[id].handle,
                                         (uint8_t *)tx, rx, len, timeout);
    } else {
        hal_st = HAL_SPI_Transmit(&spi_ctx[id].handle, (uint8_t *)tx, len, timeout);
    }

    return (hal_st == HAL_OK) ? BSP_SPI_OK : BSP_SPI_ERR_TIMEOUT;
}
