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


/* Private function prototypes -----------------------------------------------*/
void Error_Handler(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

    HAL_Init();
    BSP_SYSTICK_DelayInit();

    BSP_LED_Init();
    BSP_USART_Init(BSP_USART1, 115200);  /* USART1: 8-N-1，给 printf 用 */

    /* Print startup banner via USART1 (printf redirected) */
    printf("\r\n========================================\r\n");
    printf("  HAL_TEST_Board — STM32F103ZET6\r\n");
    printf("  USART Driver Initialized (5ch, 115200)\r\n");
    printf("========================================\r\n\r\n");

    uint8_t ch;
    /* Infinite loop */
    while (1) {
        /* LED 交替闪烁 */
        if(HAL_GetTick() % 1000 < 500) {
            BSP_LED_On(BSP_LED_R);
            BSP_LED_Off(BSP_LED_G);
            BSP_LED_Off(BSP_LED_B);
        } else {
            BSP_LED_Off(BSP_LED_R);
            BSP_LED_On(BSP_LED_G);
            BSP_LED_On(BSP_LED_B);
        }

        /* USART1 回显：收到什么就发回什么 */
        
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
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1) {
    }
    /* USER CODE END Error_Handler_Debug */
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
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line
       number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
       line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
