/**
 * @file    bsp_i2c.c
 * @brief   BSP 硬件 I2C 驱动实现 —— 基于 STM32F1 HAL I2C 的阻塞式主机模式
 * @note    I2C1: PB6=SCL (AF4, 开漏), PB7=SDA (AF4, 开漏)
 *          APB1=36MHz
 *
 *          架构：
 *            遵循 bsp_usart 模式 — static const HW 映射表 + static ctx 数组。
 *            GPIO + 时钟配置在 Init 中内联完成，不依赖 HAL_I2C_MspInit 弱回调。
 *
 *          STM32F1 硬件 I2C 存在已知缺陷 (BUSY 锁死等)，
 *          推荐生产环境使用 bsp_soft_i2c 替代。
 */

#include "bsp_i2c.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 硬件映射表 (I2C1)
 * ================================================================ */

typedef struct {
    I2C_TypeDef  *instance;
    GPIO_TypeDef *scl_port;  uint16_t scl_pin;
    GPIO_TypeDef *sda_port;  uint16_t sda_pin;
} I2C_HW_Map_t;

static const I2C_HW_Map_t i2c_hw_map[BSP_I2C_BUS_COUNT] = {
    [BSP_I2C_BUS_1] = {
        .instance = I2C1,
        .scl_port = GPIOB, .scl_pin = GPIO_PIN_6,
        .sda_port = GPIOB, .sda_pin = GPIO_PIN_7,
    },
};

/* ================================================================
 * 运行时上下文
 * ================================================================ */

typedef struct {
    I2C_HandleTypeDef handle;
    uint8_t           initialized;
} I2C_Ctx_t;

static I2C_Ctx_t i2c_ctx[BSP_I2C_BUS_COUNT];

/* ================================================================
 * 内部辅助
 * ================================================================ */

static void i2c_enable_clk(const I2C_HW_Map_t *hw)
{
    (void)hw;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
}

static void i2c_init_gpio(const I2C_HW_Map_t *hw)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_AF_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = hw->scl_pin;
    HAL_GPIO_Init(hw->scl_port, &gpio);

    gpio.Pin = hw->sda_pin;
    HAL_GPIO_Init(hw->sda_port, &gpio);
}

static BSP_I2C_Status_t i2c_map_hal_status(HAL_StatusTypeDef s)
{
    switch (s) {
    case HAL_OK:      return BSP_I2C_OK;
    case HAL_BUSY:    return BSP_I2C_BUSY;
    case HAL_TIMEOUT: return BSP_I2C_TIMEOUT;
    default:          return BSP_I2C_ERROR;
    }
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_I2C_Init(BSP_I2C_Bus_Id_t id, uint32_t clock_speed)
{
    if (id >= BSP_I2C_BUS_COUNT) return;

    I2C_Ctx_t          *ctx = &i2c_ctx[id];
    const I2C_HW_Map_t *hw  = &i2c_hw_map[id];

    if (ctx->initialized) return;
    ctx->initialized = 1;

    /* 1. 时钟 + GPIO */
    i2c_enable_clk(hw);
    i2c_init_gpio(hw);

    /* 2. 复位外设 */
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();

    /* 3. HAL I2C 初始化 */
    ctx->handle.Instance             = hw->instance;
    ctx->handle.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    ctx->handle.Init.ClockSpeed      = clock_speed;
    ctx->handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    ctx->handle.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    ctx->handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    ctx->handle.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    ctx->handle.Init.OwnAddress1     = 0x00;
    ctx->handle.Init.OwnAddress2     = 0;

    HAL_I2C_Init(&ctx->handle);
}

BSP_I2C_Status_t BSP_I2C_MemWrite(BSP_I2C_Bus_Id_t id,
                                  uint8_t dev_addr, uint16_t mem_addr,
                                  uint8_t mem_addr_size,
                                  const uint8_t *data, uint16_t len,
                                  uint32_t timeout)
{
    if (id >= BSP_I2C_BUS_COUNT || !i2c_ctx[id].initialized) return BSP_I2C_ERROR;

    HAL_StatusTypeDef s = HAL_I2C_Mem_Write(
        &i2c_ctx[id].handle,
        (uint16_t)(dev_addr << 1),
        mem_addr,
        (mem_addr_size == 2) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT,
        (uint8_t *)data, len, timeout);

    return i2c_map_hal_status(s);
}

BSP_I2C_Status_t BSP_I2C_MemRead(BSP_I2C_Bus_Id_t id,
                                 uint8_t dev_addr, uint16_t mem_addr,
                                 uint8_t mem_addr_size,
                                 uint8_t *data, uint16_t len,
                                 uint32_t timeout)
{
    if (id >= BSP_I2C_BUS_COUNT || !i2c_ctx[id].initialized) return BSP_I2C_ERROR;

    HAL_StatusTypeDef s = HAL_I2C_Mem_Read(
        &i2c_ctx[id].handle,
        (uint16_t)(dev_addr << 1),
        mem_addr,
        (mem_addr_size == 2) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT,
        data, len, timeout);

    return i2c_map_hal_status(s);
}

BSP_I2C_Status_t BSP_I2C_IsDeviceReady(BSP_I2C_Bus_Id_t id,
                                       uint8_t dev_addr,
                                       uint32_t trials, uint32_t timeout)
{
    if (id >= BSP_I2C_BUS_COUNT || !i2c_ctx[id].initialized) return BSP_I2C_ERROR;

    HAL_StatusTypeDef s = HAL_I2C_IsDeviceReady(
        &i2c_ctx[id].handle,
        (uint16_t)(dev_addr << 1),
        trials, timeout);

    return i2c_map_hal_status(s);
}
