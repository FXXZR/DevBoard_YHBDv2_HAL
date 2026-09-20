/* ================================================================
 * FreeRTOS 移植钩子 — 桥接 FreeRTOS 与 STM32 HAL
 *
 *   vApplicationTickHook()         → HAL_IncTick()
 *   vApplicationMallocFailedHook() → 内存耗尽捕获
 *   vApplicationStackOverflowHook()→ 任务栈溢出捕获
 *
 * 这些函数在 FreeRTOSConfig.h 中通过 configUSE_xxx_HOOK 启用
 *
 * 后两个是 panic 场景, 统一走 BSP_Panic* 原语 (见 BSP/Inc/bsp_assert.h):
 *   - 输出轮询直写 USART1, 不经过 log 的环形缓冲 / DMA / 中断
 *     (故障时这些机制自己可能就不可用了)
 *   - Debug 停机等调试器 / Release 复位自恢复
 * 关键点: FreeRTOS 把溢出任务的【名字】递到手上, 一定要打出来 ——
 *         丢掉它就等于现场死机时手上零信息.
 * ================================================================ */

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include "lvgl.h"
#include "bsp_assert.h"
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
/* pvPortMalloc() 返回 NULL 时调用。                                     */
/* 本工程 configTOTAL_HEAP_SIZE 只有 16KB, 而 LVGL 吃内存 —— 这个钩子    */
/* 是会真的响的, 所以必须留下可诊断的信息, 不能静默死循环。              */
/*-----------------------------------------------------------------------*/
void vApplicationMallocFailedHook(void)
{
    BSP_PanicBegin();
    BSP_PanicPuts("\r\n*** PANIC  malloc failed (FreeRTOS heap exhausted,"
                  " see configTOTAL_HEAP_SIZE)\r\n");
    BSP_PanicEnd();
}

/*-----------------------------------------------------------------------*/
/* Stack Overflow Hook — 任务栈溢出时触发 (configCHECK_FOR_STACK_OVERFLOW) */
/*                                                                       */
/* pcTaskName 是 FreeRTOS 特意传进来的【溢出任务名】—— 唯一的现场线索,   */
/* 必须打出来, 否则现场死机时完全无从下手。                              */
/*-----------------------------------------------------------------------*/
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void) xTask;
    BSP_PanicBegin();
    BSP_PanicPuts("\r\n*** PANIC  stack overflow in task: ");
    BSP_PanicPuts(pcTaskName ? pcTaskName : "?");
    BSP_PanicPuts("\r\n");
    BSP_PanicEnd();
}

