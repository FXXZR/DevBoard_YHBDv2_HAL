/* ================================================================
 * FreeRTOS 移植钩子 — 桥接 FreeRTOS 与 STM32 HAL
 *
 *   vApplicationTickHook()         → HAL_IncTick()
 *   vApplicationMallocFailedHook() → 内存耗尽捕获 (开发调试)
 *
 * 这些函数在 FreeRTOSConfig.h 中通过 configUSE_xxx_HOOK 启用
 * ================================================================ */

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include "lvgl.h"
/*-----------------------------------------------------------------------*/
/* Tick Hook — 每个 FreeRTOS tick 调用一次 (1ms)                         */
/*                                                                       */
/* FreeRTOS 接管了 SysTick_Handler, 此钩子负责驱动 HAL 的时基计数器      */
/* (uwTick), 使 HAL_Delay / HAL_GetTick / 超时机制正常工作。             */
/*-----------------------------------------------------------------------*/
void vApplicationTickHook(void)
{
    HAL_IncTick();
    lv_tick_inc(1);
}

/*-----------------------------------------------------------------------*/
/* Malloc Failed Hook — 堆分配失败时触发                                 */
/*                                                                       */
/* pvPortMalloc() 返回 NULL 时调用。调试阶段用于捕获内存泄漏/耗尽。      */
/*-----------------------------------------------------------------------*/
void vApplicationMallocFailedHook(void)
{
    __disable_irq();
    for (;;) { }
}

/*-----------------------------------------------------------------------*/
/* Stack Overflow Hook — 任务栈溢出时触发 (configCHECK_FOR_STACK_OVERFLOW) */
/*-----------------------------------------------------------------------*/
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void) xTask;
    (void) pcTaskName;
    __disable_irq();
    for (;;) { }
}

