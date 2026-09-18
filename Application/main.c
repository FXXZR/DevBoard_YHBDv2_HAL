/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include "stm32f1xx_hal.h"
#include "bsp_systick.h"
#include "bsp_led.h"
#include "bsp_usart.h"
#include "bsp_sram.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_init.h"


/* Private function prototypes -----------------------------------------------*/
void Error_Handler(void);

/* ================================================================
 * 主函数
 * ================================================================ */
int main(void) {

    /* ---- 硬件抽象层 + 系统时钟 ---- */
    HAL_Init();                    /* NVIC 分组, 不碰 SysTick */
    BSP_SYSTICK_DelayInit();       /* 时钟 PLL 72MHz, SysTick 计数器启动/中断关闭 */

    /* ---- BSP 外设驱动初始化 ---- */
    BSP_LED_Init();
    BSP_USART_Init(BSP_USART1, 115200);

    /* 外部 SRAM 初始化 — 必须在进入 RTOS 前完成
     * LVGL 内存池 (LV_MEM_POOL_ALLOC) 位于外部 SRAM, 需先就绪 */
    BSP_SRAM_Init();

    /* ---- 启动 Banner ---- */
    printf("\r\n========================================\r\n");
    printf("  HAL_TEST_Board — STM32F103ZET6\r\n");
    printf("========================================\r\n\r\n");

    /* ---- 启动前心跳: RGB 三色依次亮灭 ---- */
    BSP_LED_On(BSP_LED_R);   BSP_SYSTICK_DelayMS(200);  BSP_LED_Off(BSP_LED_R);
    BSP_LED_On(BSP_LED_G);   BSP_SYSTICK_DelayMS(200);  BSP_LED_Off(BSP_LED_G);
    BSP_LED_On(BSP_LED_B);   BSP_SYSTICK_DelayMS(200);  BSP_LED_Off(BSP_LED_B);

    /* ================================================================
     * FreeRTOS 启动
     * ================================================================ */
    printf("[RTOS] Starting scheduler...\r\n");
    task_init();                    /* 创建启动任务 (内部批量创建子任务) */
    vTaskStartScheduler();          /* 启动调度器 (永不返回) */

    /* ---- 不应到达此处 (调度器启动失败才会到达) ---- */
    printf("[RTOS] FATAL: Scheduler returned!\r\n");
    Error_Handler();
}



/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
    __disable_irq();
    while (1) {
        /* Red LED SOS — 用空循环粗略延时 (IRQ 已关, HAL_Delay 不可用) */
        for (volatile uint32_t i = 0; i < 2000000; i++) { __NOP(); }
        BSP_LED_Toggle(BSP_LED_R);
    }
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
    /* User can add his own implementation to report the file name and line
       number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
       line) */
}
#endif /* USE_FULL_ASSERT */
