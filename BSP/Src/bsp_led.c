/**
 * @file    bsp_led.c
 * @brief   BSP LED 驱动实现 —— 基于 STM32 HAL 的 LED 控制
 * @note    这是整个项目中唯一直接操作 GPIO 寄存器的"板级文件"。
 *          更换 MCU 或引脚时，只需修改本文件中的 led_hw_map[] 映射表。
 */

#include "bsp_led.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 硬件映射表 —— 将逻辑 LED 编号映射到物理 GPIO 端口和引脚
 *
 * 换板子/换引脚时，只改这个表即可，API 完全不变！
 * ================================================================ */
typedef struct {
    GPIO_TypeDef *port;     /* GPIO 端口基地址，如 GPIOB */
    uint16_t      pin;      /* GPIO 引脚，如 GPIO_PIN_0   */
} LED_HW_Map_t;

static const LED_HW_Map_t led_hw_map[BSP_LED_COUNT] = {
    [BSP_LED_R] = { .port = GPIOB, .pin = GPIO_PIN_5 },
    [BSP_LED_G] = { .port = GPIOB, .pin = GPIO_PIN_0 },
    [BSP_LED_B] = { .port = GPIOB, .pin = GPIO_PIN_1 },
};

/* ================================================================
 * 公开 API 实现
 * ================================================================ */

/**
 * @brief  初始化所有 LED 对应的 GPIO 为推挽输出
 */
void BSP_LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIO 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 配置所有 LED 引脚 */
    for (uint8_t i = 0; i < BSP_LED_COUNT; i++) {
        GPIO_InitStruct.Pin   = led_hw_map[i].pin;
        GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull  = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(led_hw_map[i].port, &GPIO_InitStruct);
        /* 默认全部 LED 熄灭 */
        HAL_GPIO_WritePin(led_hw_map[i].port, led_hw_map[i].pin, GPIO_PIN_SET);
    }
}

/**
 * @brief  点亮指定 LED
 */
void BSP_LED_On(BSP_LED_Id_t led)
{
    if (led >= BSP_LED_COUNT) return;
    HAL_GPIO_WritePin(led_hw_map[led].port, led_hw_map[led].pin, GPIO_PIN_RESET);
}

/**
 * @brief  熄灭指定 LED
 */
void BSP_LED_Off(BSP_LED_Id_t led)
{
    if (led >= BSP_LED_COUNT) return;
    HAL_GPIO_WritePin(led_hw_map[led].port, led_hw_map[led].pin, GPIO_PIN_SET);
}

/**
 * @brief  翻转指定 LED 状态
 */
void BSP_LED_Toggle(BSP_LED_Id_t led)
{
    if (led >= BSP_LED_COUNT) return;
    HAL_GPIO_TogglePin(led_hw_map[led].port, led_hw_map[led].pin);
}
