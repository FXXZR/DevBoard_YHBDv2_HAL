/*
 * FreeRTOS V11.3.0 — Project Configuration for STM32F103ZET6 (YHBDv2)
 *
 * 此文件被 FreeRTOS.h 直接 #include，属于项目定制配置
 * 类似 FatFs 的 ffconf.h — 不修改 FreeRTOS 源码，所有配置在此集中管理
 *
 * 启用 FreeRTOS 后:
 *   - stm32f1xx_it.c 中的 SysTick/PendSV/SVC 已注释, 由 FreeRTOS port.c 接管
 *   - HAL_IncTick() 由 vApplicationTickHook() 在 FreeRTOS tick 中调用
 *   - 所有可调用 FromISR 的 BSP 中断优先级已调整至 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*---------------------------------------------------------------------------*/
/* Hardware specific configuration                                           */
/*---------------------------------------------------------------------------*/

#define configCPU_CLOCK_HZ                      ( 72000000UL )  /* HCLK = 72 MHz */
#define configTICK_RATE_HZ                      ( 1000 )        /* 1 ms tick  */

#define configMAX_PRIORITIES                    ( 7 )
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 128 )
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 16 * 1024 ) )
#define configMAX_TASK_NAME_LEN                 ( 16 )

#define configUSE_TRACE_FACILITY                1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/*---------------------------------------------------------------------------*/
/* Kernel features                                                           */
/*---------------------------------------------------------------------------*/

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               8
#define configCHECK_FOR_STACK_OVERFLOW          2     /* Method 2: canary */

/* Software timers */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/* Memory allocation */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* Hook functions */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1   /* → vApplicationTickHook() 调用 HAL_IncTick() */
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/*---------------------------------------------------------------------------*/
/* Cortex-M3 interrupt priority (STM32: 4-bit preemption priority)           */
/*---------------------------------------------------------------------------*/

#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS   __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS   4
#endif

/* 最低优先级 (掩码: 0xF) → 用于内核中断 (PendSV/SysTick) */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         0xF

/* FreeRTOS API 函数可调用的最高中断优先级 (值越大优先级越低)              */
/* 5 意味着优先级 5~15 的中断可以安全调用 FreeRTOS API (FromISR 版本)      */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

/* 写入 NVIC 寄存器时需要左移 (STM32 只用高 4 位)                          */
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/*---------------------------------------------------------------------------*/
/* ISR handler name mapping — FreeRTOS port layer → STM32 vector table       */
/*---------------------------------------------------------------------------*/

#define xPortSysTickHandler     SysTick_Handler
#define vPortSVCHandler         SVC_Handler
#define xPortPendSVHandler      PendSV_Handler

/*---------------------------------------------------------------------------*/
/* Assert                                                                    */
/*---------------------------------------------------------------------------*/

/* 收编到 BSP 的零依赖 panic 通道 —— 见 BSP/Inc/bsp_assert.h
 *
 *   相比原始的 if(...) { taskDISABLE_INTERRUPTS(); for(;;); }:
 *     - 带上 __FILE__ / __LINE__ / 失败的表达式原文, 能直接定位
 *     - 输出走轮询直写, 不经过 log 的环形缓冲 (缓冲满时那条最关键的信息
 *       恰恰最容易被丢掉), 也不经过 DMA
 *     - 用 __disable_irq() 全冻结, 而不是只抬 BASEPRI (后者会漏掉
 *       NVIC 优先级 0~4 的高优先级中断)
 *     - 带 do{}while(0) 包裹, 消除悬空 else 的隐患
 *     - Debug 停机等调试器 / Release 复位自恢复
 *
 * 注: 这个 include 是本工程里 Middleware → BSP 的唯一一条编译期依赖边,
 *     见 Middleware/FreeRTOS/CMakeLists.txt 中的 include 路径说明. */
#include "bsp_assert.h"

#define configASSERT( x )   BSP_ASSERT( x )

/*---------------------------------------------------------------------------*/
/* Optional API functions — 1 = 包含, 按需启用                               */
/*---------------------------------------------------------------------------*/

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xSemaphoreGetMutexHolder        1

#endif /* FREERTOS_CONFIG_H */
