/**
 * @file    task_led.c
 * @brief   LED 心跳任务 — 根据系统状态切换闪烁模式
 *
 * 状态 → 模式映射:
 *   SYS_STATE_INIT   Red 100ms 快闪 (系统启动)
 *   SYS_STATE_IDLE   Green 心跳 (500ms on / 1500ms off)
 *   SYS_STATE_ACTIVE Blue 双闪 (100ms-100ms-800ms 循环)
 *   SYS_STATE_ERROR  Red 50ms 极快闪
 *
 * 裸机版 main.c while(1) LED 逻辑已迁移至此, 通过 vTaskDelay 实现非阻塞。
 */

#include "task_init.h"
#include "bsp_led.h"
#include "FreeRTOS.h"
#include "task.h"

/* ================================================================
 * LED 任务
 * ================================================================ */

void vLedTask(void *pvParameters)
{
    (void) pvParameters;

    /* 切换到 INIT 模式 (避免裸机遗留的 LED 状态) */
    BSP_LED_Off(BSP_LED_R);
    BSP_LED_Off(BSP_LED_G);
    BSP_LED_Off(BSP_LED_B);

    for (;;) {
        SysState_t state = g_sys_state;   /* 快照, 避免模式切换中途状态变化 */

        switch (state) {

        case SYS_STATE_INIT:
            /* Red 100ms 快闪 */
            BSP_LED_Toggle(BSP_LED_R);
            BSP_LED_Off(BSP_LED_G);
            BSP_LED_Off(BSP_LED_B);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case SYS_STATE_IDLE:
            /* Green 心跳: 500ms on, 1500ms off */
            BSP_LED_Off(BSP_LED_R);
            BSP_LED_On(BSP_LED_G);
            BSP_LED_Off(BSP_LED_B);
            vTaskDelay(pdMS_TO_TICKS(500));
            BSP_LED_Off(BSP_LED_G);
            vTaskDelay(pdMS_TO_TICKS(1500));
            break;

        case SYS_STATE_ACTIVE:
            /* Blue 双闪: 100ms-100ms-800ms */
            BSP_LED_Off(BSP_LED_R);
            BSP_LED_Off(BSP_LED_G);
            BSP_LED_On(BSP_LED_B);
            vTaskDelay(pdMS_TO_TICKS(100));
            BSP_LED_Off(BSP_LED_B);
            vTaskDelay(pdMS_TO_TICKS(100));
            BSP_LED_On(BSP_LED_B);
            vTaskDelay(pdMS_TO_TICKS(100));
            BSP_LED_Off(BSP_LED_B);
            vTaskDelay(pdMS_TO_TICKS(800));
            break;

        case SYS_STATE_ERROR:
            /* Red 50ms 极快闪 */
            BSP_LED_Toggle(BSP_LED_R);
            BSP_LED_Off(BSP_LED_G);
            BSP_LED_Off(BSP_LED_B);
            vTaskDelay(pdMS_TO_TICKS(50));
            break;

        default:
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}
