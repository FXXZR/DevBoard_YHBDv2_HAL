/**
 * @file    test_key.c
 * @brief   按键扫描测试（LED 反馈 + 串口状态打印）
 * @note    KEY1 → 红 LED (按下亮), KEY2 → 绿 LED (按下亮)
 *          按键状态变化时 printf 输出。
 */

#include "test.h"

void Test_KEY_Scan(void)
{
    BSP_KEY_Init();

    uint8_t last1 = BSP_KEY_RELEASED;
    uint8_t last2 = BSP_KEY_RELEASED;

    printf("\r\n[KEY Scan Test] KEY1→LED_R, KEY2→LED_G\r\n");

    while (1) {
        uint8_t s1 = BSP_KEY_Scan(BSP_KEY_1);
        uint8_t s2 = BSP_KEY_Scan(BSP_KEY_2);

        /* 按键状态变化 → 串口打印 */
        if (s1 != last1) {
            printf("  KEY1: %s\r\n", s1 ? "PRESSED" : "released");
            last1 = s1;
        }
        if (s2 != last2) {
            printf("  KEY2: %s\r\n", s2 ? "PRESSED" : "released");
            last2 = s2;
        }

        /* 按键 → LED 反馈 */
        if (s1) BSP_LED_On(BSP_LED_R);  else BSP_LED_Off(BSP_LED_R);
        if (s2) BSP_LED_On(BSP_LED_G);  else BSP_LED_Off(BSP_LED_G);

        BSP_SYSTICK_DelayMS(50);  /* 50ms 扫描间隔 */
    }
}
