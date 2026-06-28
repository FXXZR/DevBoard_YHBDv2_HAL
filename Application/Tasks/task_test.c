/**
 * @file    task_test.c
 * @brief   FreeRTOS 测试任务 — 运行 LCD 综合显示测试
 *
 * 任务运行完一次 Test_LCD_Display() 后进入 IDLE 状态,
 * 可通过修改 g_sys_state 触发重新运行。
 */

#include "task_init.h"
#include "test.h"
#include "FreeRTOS.h"
#include "task.h"

void vTestTask(void *pvParameters)
{
    (void) pvParameters;

    /* 等待硬件完全就绪 (LED task 先跑, INIT 模式结束) */
    vTaskDelay(pdMS_TO_TICKS(500));

    for (;;) {
        /* 标记系统进入活跃状态 */
        g_sys_state = SYS_STATE_ACTIVE;

        /* 运行 LCD 综合显示测试 (含触摸画板 demo, KEY1 退出) */
        Test_LCD_Display();

        /* 测试结束 → 空闲 */
        g_sys_state = SYS_STATE_IDLE;

        /* 空闲 3s 后重新运行 (避免触摸画板退出后立即再次进入) */
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
