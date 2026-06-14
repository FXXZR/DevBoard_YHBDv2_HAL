/**
 * @file    bsp_led.h
 * @brief   BSP LED 驱动接口 —— 平台无关的 LED 控制 API
 * @note    本文件只暴露抽象接口，不包含任何 HAL/寄存器依赖。
 *          更换 MCU 时只需修改对应的 .c 实现文件。
 */

#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * LED 编号枚举 —— 应用层通过逻辑编号控制 LED，无需关心物理引脚
 * ================================================================ */
typedef enum {
    BSP_LED_R = 0,      /* 红色 LED */
    BSP_LED_G,          /* 绿色 LED */
    BSP_LED_B,          /* 蓝色 LED */
    BSP_LED_COUNT       /* LED 总数（自动计算） */
} BSP_LED_Id_t;

/* ================================================================
 * API 函数
 * ================================================================ */

/**
 * @brief  初始化所有 LED 对应的 GPIO
 * @note   必须在 main 中 HAL_Init() + SystemClock_Config() 之后调用
 */
void BSP_LED_Init(void);

/**
 * @brief  点亮指定 LED
 * @param  led  LED 编号 (BSP_LED_R / G / B)
 */
void BSP_LED_On(BSP_LED_Id_t led);

/**
 * @brief  熄灭指定 LED
 * @param  led  LED 编号 (BSP_LED_R / G / B)
 */
void BSP_LED_Off(BSP_LED_Id_t led);

/**
 * @brief  翻转指定 LED 状态
 * @param  led  LED 编号 (BSP_LED_R / G / B)
 */
void BSP_LED_Toggle(BSP_LED_Id_t led);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LED_H__ */
