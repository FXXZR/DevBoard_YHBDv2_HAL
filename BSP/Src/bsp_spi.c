/**
 * @file    bsp_spi.c
 * @brief   BSP 硬件 SPI 驱动实现 —— 基于 STM32F1 HAL SPI 的主机模式
 * @note    SPI1: PA5=SCK (AF PP), PA6=MISO (AF PP, pull-up), PA7=MOSI (AF PP)
 *          CS (NSS) 由上层模块软件管理 (NSS_SOFT 模式)。
 *
 *          架构：
 *            遵循 bsp_usart 模式 — static const HW 映射表 + static ctx 数组。
 *            GPIO + 时钟配置在 Init 中内联完成。
 *
 *          P2 改进: Transfer/TransferBuffer 暴露超时参数，不再写死。
 */

#include "bsp_spi.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 硬件映射表 (SPI1)
 * ================================================================ */

typedef struct {
    SPI_TypeDef  *instance;
    GPIO_TypeDef *sck_port;  uint16_t sck_pin;
    GPIO_TypeDef *miso_port; uint16_t miso_pin;
    GPIO_TypeDef *mosi_port; uint16_t mosi_pin;
} SPI_HW_Map_t;

static const SPI_HW_Map_t spi_hw_map[BSP_SPI_BUS_COUNT] = {
    [BSP_SPI_BUS_1] = {
        .instance  = SPI1,
        .sck_port  = GPIOA, .sck_pin  = GPIO_PIN_5,
        .miso_port = GPIOA, .miso_pin = GPIO_PIN_6,
        .mosi_port = GPIOA, .mosi_pin = GPIO_PIN_7,
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

static void spi_enable_clk(const SPI_HW_Map_t *hw)
{
    (void)hw;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
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
    if (id >= BSP_SPI_BUS_COUNT || prescaler > BSP_SPI_BAUD_DIV_256) return;

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
    ctx->handle.Init.CLKPhase          = SPI_PHASE_1EDGE;
    ctx->handle.Init.CLKPolarity       = SPI_POLARITY_LOW;
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
    if (id >= BSP_SPI_BUS_COUNT || !spi_ctx[id].initialized) return 0xFF;

    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&spi_ctx[id].handle, &data, &rx, 1, timeout);
    return rx;
}

void BSP_SPI_TransferBuffer(BSP_SPI_Bus_Id_t id,
                            const uint8_t *tx, uint8_t *rx, uint16_t len,
                            uint32_t timeout)
{
    if (id >= BSP_SPI_BUS_COUNT || !spi_ctx[id].initialized || !tx || len == 0) return;

    if (rx) {
        HAL_SPI_TransmitReceive(&spi_ctx[id].handle,
                                (uint8_t *)tx, rx, len, timeout);
    } else {
        HAL_SPI_Transmit(&spi_ctx[id].handle, (uint8_t *)tx, len, timeout);
    }
}
