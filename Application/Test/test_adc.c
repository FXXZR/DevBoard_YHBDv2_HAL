/**
 * @file    test_adc.c
 * @brief   ADC 连续采样测试（串口打印电压值）
 * @note    PC1 (ADC1_CH11) 电压每 500ms 通过 printf 输出一次 (mV)。
 *          VREF = 3.3V, 12bit 分辨率 ≈ 0.8mV/LSB。
 */

#include "test.h"

void Test_ADC_Continuous(void)
{
    BSP_ADC_Init(BSP_ADC_1);

    printf("\r\n[ADC Test] PC1 voltage, 500ms interval\r\n");

    while (1) {
        uint16_t mv = BSP_ADC_Read_mV(BSP_ADC_1);
        printf("  ADC1_CH11 (PC1): %u mV\r\n", mv);
        BSP_SYSTICK_DelayMS(500);
    }
}
