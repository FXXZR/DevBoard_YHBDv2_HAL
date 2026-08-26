/**
 * @file lvgl_port.h
 * @brief LVGL 板级移植层 — 显示 (ILI9341) + 触摸 (XPT2046) 统一挂接
 */
#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "lvgl.h"

/**
 * @brief  初始化 LVGL 显示 + 触摸驱动
 * @note   需在 lv_init() 之后调用
 */
void lvgl_port_init(void);

#endif /* LVGL_PORT_H */
