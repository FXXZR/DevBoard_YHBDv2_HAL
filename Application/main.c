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
#include "test.h"


/* Private function prototypes -----------------------------------------------*/
void Error_Handler(void);

/* ================================================================
 * 主函数
 * ================================================================ */
int main(void) {

    /* ---- 硬件抽象层 + 系统时钟 ---- */
    HAL_Init();
    BSP_SYSTICK_DelayInit();

    /* ---- BSP 外设驱动初始化 ---- */
    BSP_LED_Init();
    BSP_USART_Init(BSP_USART1, 115200);

    /* ---- 启动 Banner ---- */
    printf("\r\n========================================\r\n");
    printf("  HAL_TEST_Board — STM32F103ZET6\r\n");
    printf("  BSP: LED | USART | BEEP | KEY | ADC | RTC\r\n");
    printf("========================================\r\n\r\n");

    /* ================================================================
     * 测试入口：取消注释要运行的测试，其他保持注释
     * ================================================================ */
    // Test_BEEP_KeyTrigger();   /* KEY1 按下 → 蜂鸣 200ms */
    // Test_KEY_Scan();          /* KEY1/2 → LED + printf 状态 */
    // Test_ADC_Continuous();    /* PC1 电压每 500ms printf */
    // Test_EEPROM_ReadWrite();   /* AT24C02 全片写入 + 读出校验 */
    // Test_SPI_FLASH_ReadWrite(); /* W25Q64 全片写入 + 读出校验 */

    Test_SDCARD_FatFS();        /* SD Card + FatFS 读写验证 */

    Test_RTC_Interactive();      /* RTC 时间戳 + 串口命令交互 */
    /* ---- 不会执行到这里 (测试函数内部有死循环) ---- */
    while (1) {
        /* 回退: LED 交替闪烁 + USART1 回显 */
        if(HAL_GetTick() % 1000 < 500) {
            BSP_LED_On(BSP_LED_R);
            BSP_LED_Off(BSP_LED_G);
            BSP_LED_Off(BSP_LED_B);
        } else {
            BSP_LED_Off(BSP_LED_R);
            BSP_LED_On(BSP_LED_G);
            BSP_LED_On(BSP_LED_B);
        }

        uint8_t ch;
        if(BSP_USART_Read(BSP_USART1, &ch) == 0) {
            BSP_USART_Send(BSP_USART1, &ch, 1);
            BSP_USART_Flush(BSP_USART1, 10);
        }
    }
}



/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
    __disable_irq();
    while (1) {
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
