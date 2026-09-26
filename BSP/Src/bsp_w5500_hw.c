/**
 * @file    bsp_w5500_hw.c
 * @brief   W5500 板级硬件资源实现 —— CS / RESET / INT + SPI2
 * @note    只碰 GPIO 和 bsp_spi, 不认识 W5500 的任何寄存器。
 */

#include "bsp_w5500_hw.h"
#include "bsp_spi.h"
#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 板级资源
 * ================================================================ */

#define W5500_CS_PORT       GPIOG
#define W5500_CS_PIN        GPIO_PIN_9
#define W5500_RST_PORT      GPIOG
#define W5500_RST_PIN       GPIO_PIN_15
#define W5500_INT_PORT      GPIOG
#define W5500_INT_PIN       GPIO_PIN_8

/* 复位时序 —— 见 bsp_w5500_hw.h 的说明 */
#define W5500_RESET_LOW_MS      1
#define W5500_RESET_SETTLE_MS   100

/* 一条总线 —— SPI2 (PB13/14/15, 模式 3, 见 bsp_spi.c 的硬件映射表) */
#define W5500_SPI_BUS           BSP_SPI_BUS_2

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_W5500_HW_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* ---- GPIO: CS / RESET / INT 都在 GPIOG ----
     * 注: bsp_spi 只使能 SCK/MISO/MOSI 所在端口的时钟 (这里是 GPIOB),
     *     CS/RST/INT 所在的 GPIOG 必须由本模块自己开。 */
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* CS 与 RESET: 推挽输出, 初值都是"无效" —— 避免上电瞬间被误选中
     * 或产生一次无谓的复位。 */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin  = W5500_CS_PIN | W5500_RST_PIN;
    HAL_GPIO_Init(W5500_CS_PORT, &gpio);
    BSP_W5500_HW_CsHigh();
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_SET);

    /* INT: 输入上拉 (W5500 的 INT 是低有效输出, 空闲为高) */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = W5500_INT_PIN;
    HAL_GPIO_Init(W5500_INT_PORT, &gpio);

    /* ---- SPI2 ---- */
    /* APB1 = 36MHz, DIV_2 = 18MHz。
     * 注意和 SPI1 上 Flash 的 DIV_4 不是同一个枚举值, 却同样是 18MHz ——
     * 见 bsp_spi.h 里那段分频说明。 */
    BSP_SPI_Init(W5500_SPI_BUS, BSP_SPI_BAUD_DIV_2);
}

void BSP_W5500_HW_CsLow(void)
{
    /* 直写 BSRR/BRR 而非 HAL_GPIO_WritePin: CS 每帧两次, 且在 SCK 空闲沿
     * 插入, 走 HAL 的"查引脚号→算位→assert"链路没必要。 */
    W5500_CS_PORT->BRR = W5500_CS_PIN;
}

void BSP_W5500_HW_CsHigh(void)
{
    W5500_CS_PORT->BSRR = W5500_CS_PIN;
}

void BSP_W5500_HW_Reset(void)
{
    W5500_RST_PORT->BRR  = W5500_RST_PIN;
    BSP_SYSTICK_DelayMS(W5500_RESET_LOW_MS);
    W5500_RST_PORT->BSRR = W5500_RST_PIN;
    BSP_SYSTICK_DelayMS(W5500_RESET_SETTLE_MS);
}

uint8_t BSP_W5500_HW_IntLevel(void)
{
    return (HAL_GPIO_ReadPin(W5500_INT_PORT, W5500_INT_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}
