/**
 * @file    bsp_systick.h
 * @brief   BSP SysTick 延时与系统时钟接口
 * @note    统一管理系统时钟配置和 us/ms 级硬件阻塞延时。
 *
 *          延时方案：
 *            us: SysTick->VAL 轮询，利用 HAL 已配置的 1ms 自动重载计数器
 *            ms: HAL_GetTick() 轮询
 *            两者均不关中断、不额外占用定时器外设。
 *            SysTick 中断不受影响，RTOS 心跳可安全复用。
 *
 *          可移植性：
 *            SysTick 是 Cortex-M 全系列 (M0~M7) 标配，本方案通用。
 *            更换 HAL 库或裸机时，只需调整 SysTick 重装值。
 */

#ifndef __BSP_SYSTICK_H__
#define __BSP_SYSTICK_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  SysTick 模块初始化（含系统时钟配置）
 * @note   HSE 8MHz → PLL ×9 → SYSCLK 72MHz
 *         AHB ÷1 = 72MHz, APB2 ÷1 = 72MHz, APB1 ÷2 = 36MHz
 *         必须在 HAL_Init() 之后、任何外设初始化之前调用。
 *         仅需调用一次。
 */
void BSP_SYSTICK_DelayInit(void);

/**
 * @brief  微秒级硬件阻塞延时
 * @param  us  微秒数
 * @note   基于 SysTick->VAL 轮询（HAL 1ms 自动重载计数器），不关中断。
 *          精度：72MHz → ~13.9ns 分辨率，< 1us 误差（主要为调用开销）。
 *          无理论上限，但建议 < 10^6 us，更长延时请使用 BSP_SYSTICK_DelayMS。
 */
void BSP_SYSTICK_DelayUS(uint32_t us);

/**
 * @brief  毫秒级硬件阻塞延时
 * @param  ms  毫秒数
 * @note   基于 HAL_GetTick() 轮询，依赖 SysTick 1ms 中断。
 *          不关中断，ISR 在延时期间正常响应。
 */
void BSP_SYSTICK_DelayMS(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SYSTICK_H__ */
