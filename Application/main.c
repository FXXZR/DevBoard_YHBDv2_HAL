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
#include "w5500_drv.h"
#include "w5500_net.h"
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
     * LVGL 内存池 (LV_MEM_POOL_ALLOC) 位于外部 SRAM, 需先就绪 */
    BSP_SRAM_Init();
    LOG_I("external SRAM ready (LVGL mem pool)");

    /* ---- 启动 Banner ----
     * 调度器尚未启动, HAL_GetTick() 恒为 0, 所以这几行时间戳都是 0 */
    LOG_I("========== HAL_TEST_Board / STM32F103ZET6 ==========");
    LOG_I("sysclk 72MHz, log on USART1 @115200 (ring buffer + DMA)");
    LOG_D("如果你看到这行, 说明 DEBUG 级别已启用");

    /* ---- W5500 以太网通路自检 (八周计划 W1 交付物) ----
     * 复位时序 + 读 VERSIONR, 期望 0x04 —— 读回值本身就是通路测试,
     * 不需要示波器. 失败时按状态码区分"总线无响应"和"版本不对",
     * 两者的排查方向完全不同 (接线 vs 芯片型号).
     * 注: 内部含约 100ms 复位等待, 只在启动时走一次. */
    {
        W5500_Status_t w5500_st  = W5500_Drv_Init();
        uint8_t        w5500_ver = W5500_Drv_ReadVersion();

        if (w5500_st == W5500_OK) {
            /* PHYCFGR 打原始值: 此刻自协商还没完成 (要 1~2 秒),
             * 所以这里多半是 0xFA, 稍后由 vNetTask 报出 0xFF */
            LOG_I("W5500 ok, VERSIONR=0x%02X, PHYCFGR=0x%02X",
                  w5500_ver, W5500_Drv_ReadPhyCfg());

            /* 静态网络参数 —— 默认路径。DHCP 只做一遍 (否则每次实验 IP 都变,
             * 脚本和抓包过滤器都要跟着改), 走 task_net */
            W5500_Net_ConfigStatic();
        } else {
            LOG_E("W5500 init failed: %s (VERSIONR=0x%02X)",
                  W5500_Drv_StatusStr(w5500_st), w5500_ver);
        }
    }

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
