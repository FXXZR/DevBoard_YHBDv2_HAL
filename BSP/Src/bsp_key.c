/**
 * @file    bsp_key.c
 * @brief   BSP 按键驱动实现 —— 基于 STM32 HAL 的 GPIO 输入 + 软件消抖
 * @note    KEY1=PA0, KEY2=PC13, 无上下拉 (依赖板级外部电阻)。
 *          本板按键按下为高电平 (KEY_ON=GPIO_PIN_SET)。
 *          消抖方案: 连续两次读取 (间隔 20ms) 均有效 → 确认按下。
 *          更换 MCU 或按键引脚时只需修改 key_hw_map[] 映射表。
 */

#include "bsp_key.h"
#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 硬件映射表
 * ================================================================ */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} KEY_HW_Map_t;

static const KEY_HW_Map_t key_hw_map[BSP_KEY_COUNT] = {
    [BSP_KEY_1] = { .port = GPIOA, .pin = GPIO_PIN_0  },
    [BSP_KEY_2] = { .port = GPIOC, .pin = GPIO_PIN_13 },
};

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief  初始化所有按键 GPIO
 */
void BSP_KEY_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    for (int i = 0; i < BSP_KEY_COUNT; i++) {
        gpio.Pin = key_hw_map[i].pin;
        HAL_GPIO_Init(key_hw_map[i].port, &gpio);
    }
}

/**
 * @brief  扫描指定按键状态（含 20ms 软件消抖，阻塞）
 * @param  key  按键编号
 * @return BSP_KEY_PRESSED / BSP_KEY_RELEASED
 */
BSP_Key_State_t BSP_KEY_Scan(BSP_KEY_Id_t key)
{
    if (key >= BSP_KEY_COUNT) return BSP_KEY_RELEASED;

    const KEY_HW_Map_t *hw = &key_hw_map[key];

    /* 第一次读取：检测是否按下 */
    if (HAL_GPIO_ReadPin(hw->port, hw->pin) != GPIO_PIN_SET) {
        return BSP_KEY_RELEASED;
    }

    /* 消抖延时 20ms */
    BSP_SYSTICK_DelayMS(20);

    /* 第二次读取：确认是否仍然按下 */
    if (HAL_GPIO_ReadPin(hw->port, hw->pin) == GPIO_PIN_SET) {
        return BSP_KEY_PRESSED;
    }

    return BSP_KEY_RELEASED;
}
