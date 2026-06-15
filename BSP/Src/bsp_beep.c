/**
 * @file    bsp_beep.c
 * @brief   BSP 蜂鸣器驱动实现 —— 基于 STM32 HAL 的 GPIO 控制
 * @note    PC0, 推挽输出, active-high (SET = 鸣响, RESET = 停止)。
 *          更换 MCU 或引脚时只需修改 beep_hw_map[] 映射表。
 */

#include "bsp_beep.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 硬件映射表
 * ================================================================ */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} BEEP_HW_Map_t;

static const BEEP_HW_Map_t beep_hw_map[BSP_BEEP_COUNT] = {
    [BSP_BEEP_0] = { .port = GPIOC, .pin = GPIO_PIN_0 },
};

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief  初始化蜂鸣器 GPIO
 */
void BSP_BEEP_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = beep_hw_map[BSP_BEEP_0].pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(beep_hw_map[BSP_BEEP_0].port, &gpio);

    /* 初始状态：停止 */
    HAL_GPIO_WritePin(beep_hw_map[BSP_BEEP_0].port,
                      beep_hw_map[BSP_BEEP_0].pin, GPIO_PIN_RESET);
}

void BSP_BEEP_On(BSP_BEEP_Id_t beep)
{
    if (beep >= BSP_BEEP_COUNT) return;
    HAL_GPIO_WritePin(beep_hw_map[beep].port, beep_hw_map[beep].pin, GPIO_PIN_SET);
}

void BSP_BEEP_Off(BSP_BEEP_Id_t beep)
{
    if (beep >= BSP_BEEP_COUNT) return;
    HAL_GPIO_WritePin(beep_hw_map[beep].port, beep_hw_map[beep].pin, GPIO_PIN_RESET);
}

void BSP_BEEP_Toggle(BSP_BEEP_Id_t beep)
{
    if (beep >= BSP_BEEP_COUNT) return;
    HAL_GPIO_TogglePin(beep_hw_map[beep].port, beep_hw_map[beep].pin);
}
