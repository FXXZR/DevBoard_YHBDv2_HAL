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

#define LOG_TAG "MAIN"
#include "log.h"

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
 * USART1 = 日志口
 * 环形缓冲由调用者提供 —— 这里按日志口的真实用量给 512B, 不走内部默认的 256B.
 * 大小必须是 2 的幂 (环形缓冲用掩码寻址, Cortex-M3 没有硬件除法).
 * ================================================================ */
static uint8_t s_log_tx_buf[512];

/* ================================================================
 * 主函数
 * ================================================================ */
int main(void) {

    /* ---- 硬件抽象层 + 系统时钟 ---- */
    HAL_Init();                    /* NVIC 分组, 不碰 SysTick */
    BSP_SYSTICK_DelayInit();       /* 时钟 PLL 72MHz, SysTick 计数器启动/中断关闭 */

    /* ---- BSP 外设驱动初始化 ---- */
    BSP_LED_Init();

    /* USART1 = 日志口: 环形缓冲 + DMA 发送, 入队即返回, 全程非阻塞
     * (printf 与 LOG 共用这一个环形缓冲, 不再互相抢 gState 丢数据) */
    BSP_USART_Config_t log_uart_cfg = {
        .baudrate    = 115200,
        .word_length = BSP_USART_WORD_8B,
        .stop_bits   = BSP_USART_STOP_1,
        .parity      = BSP_USART_PARITY_NONE,
        .use_dma_tx  = 1,
        .tx_buf      = s_log_tx_buf,
        .tx_buf_size = sizeof(s_log_tx_buf),
    };
    BSP_USART_InitEx(BSP_USART1, &log_uart_cfg);
    LOG_Init(BSP_USART1);

    /* 外部 SRAM 初始化 — 必须在进入 RTOS 前完成
     * LVGL 内存池 (LV_MEM_POOL_ALLOC) 位于外部 SRAM, 需先就绪
     * 注: 屏幕成像方向会随固件布局漂移, 详见 bsp_lcd_ili9341.h 枚举注释 */
    BSP_SRAM_Init();
    LOG_I("external SRAM ready (LVGL mem pool)");

    /* ---- 启动 Banner ----
     * 调度器尚未启动, HAL_GetTick() 恒为 0, 所以这几行时间戳都是 0 */
    LOG_I("========== HAL_TEST_Board / STM32F103ZET6 ==========");
    LOG_I("sysclk 72MHz, log on USART1 @115200 (ring buffer + DMA)");
    LOG_D("如果你看到这行, 说明 DEBUG 级别已启用");

    /* ---- 启动前心跳: RGB 三色依次亮灭 ---- */
    BSP_LED_On(BSP_LED_R);   BSP_SYSTICK_DelayMS(200);  BSP_LED_Off(BSP_LED_R);
    BSP_LED_On(BSP_LED_G);   BSP_SYSTICK_DelayMS(200);  BSP_LED_Off(BSP_LED_G);
    BSP_LED_On(BSP_LED_B);   BSP_SYSTICK_DelayMS(200);  BSP_LED_Off(BSP_LED_B);

    /* ================================================================
     * FreeRTOS 启动
     * ================================================================ */
    LOG_I("starting FreeRTOS scheduler");
    task_init();                    /* 创建启动任务 (内部批量创建子任务) */
    vTaskStartScheduler();          /* 启动调度器 (永不返回) */

    /* ---- 不应到达此处 (调度器启动失败才会到达) ---- */
    LOG_E("FATAL: scheduler returned");
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
