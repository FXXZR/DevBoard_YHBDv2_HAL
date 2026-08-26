
/**
 * @file lvgl_port.c
 * @brief LVGL 板级移植层 — ILI9341 显示 + XPT2046 触摸统一挂接
 *
 * 显示参数: 240x320, RGB565, FSMC 8080 并口 (由 BSP_LCD 封装)
 * 缓冲策略: 单缓冲 10 行 (240x10x2B = 4.8KB, 片内 SRAM)
 *
 * 触摸事件流:
 *   EXTI 边沿置位 TouchFlag (按下瞬间) + IRQ 电平持续检测 (按住期间)
 *   → BSP_XPT2046_GetTouchPoint() 获取校准后像素坐标 → 喂给 LVGL
 */
#include "lvgl_port.h"
#include "bsp_lcd_ili9341.h"
#include "bsp_xpt2046.h"

/* ================================================================
 * 静态原型
 * ================================================================ */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                       lv_color_t *color_p);
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

/* ================================================================
 * 公开 API
 * ================================================================ */

void lvgl_port_init(void)
{
    /* ---- 1. LCD 初始化 (幂等) ---- */
    BSP_LCD_Init();

    /* ---- 2. 显示缓冲区 — 单缓冲 10 行 ---- */
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf_1[240 * 10];

    lv_disp_draw_buf_init(&draw_buf, buf_1, NULL, 240 * 10);

    /* ---- 3. 注册显示驱动 ---- */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res  = 240;
    disp_drv.ver_res  = 320;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);

    /* ---- 4. 触摸屏初始化 (依赖 LCD 已初始化) ---- */
    BSP_XPT2046_Init();

    /* ---- 5. 注册指针型输入设备 ---- */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type     = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb  = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

/* ================================================================
 * 静态函数 — 显示
 * ================================================================ */

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                       lv_color_t *color_p)
{
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;

    /* 直接传递像素缓冲区 — RGB565 与 LVGL 颜色深度一致, 零拷贝 */
    BSP_LCD_WritePixels(area->x1, area->y1, w, h,
                        (const uint16_t *)color_p);

    lv_disp_flush_ready(disp_drv);
}

/* ================================================================
 * 静态函数 — 触摸
 * ================================================================ */

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void) indev_drv;

    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    /* 按下检测: 边沿标志 (按下瞬间) + 电平查询 (持续按住)
     * 只靠 TouchFlag 会在按住期间报 REL, 导致 LVGL 无法完成一次完整 press */
    if (BSP_XPT2046_TouchFlag || BSP_XPT2046_IsPressed()) {
        BSP_XPT2046_TouchFlag = 0;

        BSP_XPT2046_Coordinate_t coord;
        if (BSP_XPT2046_GetTouchPoint(&coord, NULL)) {
            last_x = coord.x;
            last_y = coord.y;
            data->state = LV_INDEV_STATE_PR;            /* 按下 */
        }
        else {
            data->state = LV_INDEV_STATE_REL;           /* 采样中已松开 */
        }
    }
    else {
        data->state = LV_INDEV_STATE_REL;               /* 释放 */
    }

    data->point.x = last_x;                 /* 释放时也保留最后坐标 */
    data->point.y = last_y;
}
