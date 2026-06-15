/**
 * @file    test_beep_key.c
 * @brief   蜂鸣器 + 按键联动测试
 * @note    KEY1 按一下 → 蜂鸣 200ms。
 *          演示 BSP_BEEP + BSP_KEY 的联合使用。
 */

#include "test.h"

void Test_BEEP_KeyTrigger(void)
{
    BSP_BEEP_Init();
    BSP_KEY_Init();

    printf("\r\n[BEEP+KEY Test] KEY1 按下 → 蜂鸣 200ms\r\n");

    while (1) {
        if (BSP_KEY_Scan(BSP_KEY_1) == BSP_KEY_PRESSED) {
            printf("  KEY1 pressed → BEEP ON 200ms\r\n");
            BSP_BEEP_On(BSP_BEEP_0);
            BSP_SYSTICK_DelayMS(200);
            BSP_BEEP_Off(BSP_BEEP_0);
        }
        BSP_SYSTICK_DelayMS(10);  /* 扫描间隔，防抖+降 CPU */
    }
}
