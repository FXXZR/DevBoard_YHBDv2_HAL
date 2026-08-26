/**
 * @file    task_init.c
 * @brief   FreeRTOS 启动任务 — 批量创建子任务后自删除
 */
#include "task_init.h"
#include "task_led.h"
#include "task_test.h"
#include "task_disp.h"
#include <stddef.h>   /* NULL */

/* ================================================================
 * 全局状态
 * ================================================================ */

volatile SysState_t g_sys_state = SYS_STATE_INIT;

/* ================================================================
 * 子任务批量定义表 (static const — 编译期确定, 不占 RAM)
 * ================================================================ */

static const TaskDef_t task_list[] = {
    {
        .name        = "led",
        .func        = vLedTask,
        .stack_depth = 256,
        .priority    = 2,
        .p_handle    = NULL,
        .pv_params   = NULL,
    },
    /* 暂时屏蔽: 与 LVGL disp 任务争抢 LCD 控制权 (验证显示时先注释) */
    /*
    {
        .name        = "test",
        .func        = vTestTask,
        .stack_depth = 1024,
        .priority    = 1,
        .p_handle    = NULL,
        .pv_params   = NULL,
    },
    */
    {
        .name        = "disp",
        .func        = vDispTask,
        .stack_depth = 1024,           /* LVGL 渲染需要较大栈 */
        .priority    = 1,
        .p_handle    = NULL,
        .pv_params   = NULL,
    },
};

/* ================================================================
 * 启动任务 — 批量创建所有子任务 → 自删除
 * ================================================================ */

static void vStartTask(void *pvParameters)
{
    (void) pvParameters;

    for (size_t i = 0; i < sizeof(task_list) / sizeof(task_list[0]); i++) {
        const TaskDef_t *def = &task_list[i];

        BaseType_t ret = xTaskCreate(def->func,
                                     def->name,
                                     def->stack_depth,
                                     def->pv_params,
                                     def->priority,
                                     def->p_handle);

        configASSERT(ret == pdPASS);
    }

    /* 全部子任务已创建, 启动任务自删除 */
    vTaskDelete(NULL);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void task_init(void)
{
    /* 创建启动任务 (高优先级, 确保其他任务创建完才让出 CPU) */
    xTaskCreate(vStartTask, "start", 256, NULL,
                configMAX_PRIORITIES - 2, NULL);
}
