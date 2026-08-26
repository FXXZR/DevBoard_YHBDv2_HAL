/**
 * @file    task_disp.c
 * @brief   LVGL 显示验证任务
 *
 * 初始化 LVGL + 显示驱动, 创建简单控件, 周期驱动 lv_timer_handler()
 * 当前仅验证显示通路 (不接触摸输入)
 */
#include <stdio.h>
#include "task_disp.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "bsp_xpt2046.h"
#include "FreeRTOS.h"
#include "task.h"

/* 按钮点击事件回调 */
static void btn_event_cb(lv_event_t *e)
{
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    lv_label_set_text(label, "Touch OK!");
}

void vDispTask(void *pvParameters)
{
    (void) pvParameters;

    /* 等待硬件完全就绪 (LED task 先跑, INIT 模式结束) */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 1. LVGL 内核初始化 */
    lv_init();

    /* 2. 挂接显示和触摸驱动 */
    lvgl_port_init();

    /* 3. 触摸校准 — 出厂默认系数不适用当前屏, 强制校准
     *    (阻塞: 依次按屏幕 4 个十字参考点, 失败则退回默认系数) */
    if (BSP_XPT2046_TouchCalibrate()) {
        printf("[Disp] Touch calibration OK\r\n");
    } else {
        printf("[Disp] Touch calibration failed, using defaults\r\n");
    }

    /* 4. 创建简单控件 */
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "LVGL OK!");
    lv_obj_center(label);

    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_set_pos(btn, 80, 200);
    lv_obj_set_size(btn, 80, 40);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, label);
    /* 4. LVGL 心跳 — 处理绘制/动画/事件 */
    for (;;) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
