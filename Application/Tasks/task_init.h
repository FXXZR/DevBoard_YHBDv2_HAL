/**
 * @file    task_init.h
 * @brief   FreeRTOS 任务入口模块 — 共享类型与接口
 *
 * 启动任务通过 static const TaskDef_t 数组批量创建子任务后自删除。
 * 系统状态枚举用于跨任务状态同步（LED 模式、蜂鸣反馈等）。
 */

#ifndef __TASK_INIT_H__
#define __TASK_INIT_H__

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 系统状态枚举
 * ================================================================ */

/** @brief 系统运行状态 — 影响 LED/BEEP 行为 */
typedef enum {
    SYS_STATE_INIT  = 0,    /* 启动中:   Red 快闪 */
    SYS_STATE_IDLE,         /* 空闲:     Green 心跳 */
    SYS_STATE_ACTIVE,       /* 活跃运行: Blue 呼吸 */
    SYS_STATE_ERROR,        /* 错误:     Red 极快闪 */
} SysState_t;

/* 全局系统状态 (LED task 依据此状态切换模式) */
extern volatile SysState_t g_sys_state;

/* ================================================================
 * 任务定义结构体
 * ================================================================ */

/** @brief 任务批量创建定义 */
typedef struct {
    const char      *name;          /* 任务名称 (调试) */
    TaskFunction_t   func;          /* 任务函数入口 */
    configSTACK_DEPTH_TYPE stack_depth;  /* 栈深度 (words) */
    UBaseType_t      priority;      /* 优先级 (0=最低) */
    TaskHandle_t    *p_handle;      /* 任务句柄存储 (NULL=不保存) */
    void            *pv_params;     /* 任务参数 (NULL=无) */
} TaskDef_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  创建启动任务 — 批量初始化所有子任务
 * @note   调用一次后不要再调 (内部自删除)
 *         需在 vTaskStartScheduler() 之前调用
 */
void task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_INIT_H__ */
