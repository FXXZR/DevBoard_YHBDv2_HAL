/**
 * @file    bsp_key.h
 * @brief   BSP 按键驱动接口 —— 平台无关的按键扫描 API
 * @note    本文件只暴露抽象接口，不包含任何 HAL/寄存器依赖。
 *          更换 MCU / 按键引脚时只需修改对应的 .c 实现文件。
 *          按键扫描自带 20ms 软件消抖。
 */

#ifndef __BSP_KEY_H__
#define __BSP_KEY_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 按键编号枚举 —— 应用层通过逻辑编号控制，无需关心物理引脚
 * ================================================================ */
typedef enum {
    BSP_KEY_1 = 0,      /* 按键 1 (PA0) */
    BSP_KEY_2,          /* 按键 2 (PC13) */
    BSP_KEY_COUNT       /* 按键总数（自动计算） */
} BSP_KEY_Id_t;

/* ================================================================
 * 按键状态
 * ================================================================ */
typedef enum {
    BSP_KEY_RELEASED = 0,   /* 按键释放 */
    BSP_KEY_PRESSED,        /* 按键按下 */
} BSP_Key_State_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化所有按键 GPIO
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用
 */
void BSP_KEY_Init(void);

/**
 * @brief  扫描指定按键状态（含 20ms 软件消抖，阻塞）
 * @param  key  按键编号 (BSP_KEY_1 / BSP_KEY_2)
 * @return BSP_KEY_PRESSED = 按下, BSP_KEY_RELEASED = 释放
 * @note   按键按下后不等待释放，调用方需自行处理重复触发问题。
 *         消抖期间阻塞 ~20ms。
 */
BSP_Key_State_t BSP_KEY_Scan(BSP_KEY_Id_t key);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_KEY_H__ */
