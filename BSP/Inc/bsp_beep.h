/**
 * @file    bsp_beep.h
 * @brief   BSP 蜂鸣器驱动接口 —— 平台无关的蜂鸣器控制 API
 * @note    本文件只暴露抽象接口，不包含任何 HAL/寄存器依赖。
 *          更换 MCU 时只需修改对应的 .c 实现文件。
 */

#ifndef __BSP_BEEP_H__
#define __BSP_BEEP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 蜂鸣器编号枚举 —— 应用层通过逻辑编号控制，无需关心物理引脚
 * ================================================================ */
typedef enum {
    BSP_BEEP_0 = 0,     /* 蜂鸣器 (PC0) */
    BSP_BEEP_COUNT      /* 蜂鸣器总数（自动计算） */
} BSP_BEEP_Id_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化蜂鸣器 GPIO
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用
 */
void BSP_BEEP_Init(void);

/**
 * @brief  蜂鸣器鸣响
 * @param  beep  蜂鸣器编号 (BSP_BEEP_0)
 */
void BSP_BEEP_On(BSP_BEEP_Id_t beep);

/**
 * @brief  蜂鸣器停止
 * @param  beep  蜂鸣器编号 (BSP_BEEP_0)
 */
void BSP_BEEP_Off(BSP_BEEP_Id_t beep);

/**
 * @brief  翻转蜂鸣器状态
 * @param  beep  蜂鸣器编号 (BSP_BEEP_0)
 */
void BSP_BEEP_Toggle(BSP_BEEP_Id_t beep);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BEEP_H__ */
