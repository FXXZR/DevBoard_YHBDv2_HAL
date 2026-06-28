/**
 * @file    test_lcd.c
 * @brief   LCD + 触摸综合显示测试
 * @note    流程: LCD 初始化 → 显示颜色测试 → 几何图形 → 字符串 → 触摸演示.
 *          触摸屏为可选功能 (需 XPT2046 已连接且初始化).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "test.h"

/* ================================================================
 * 内部辅助
 * ================================================================ */

static void test_lcd_color_bars(void)
{
    const uint16_t colors[] = {
        BSP_LCD_COLOR_RED, BSP_LCD_COLOR_GREEN, BSP_LCD_COLOR_BLUE,
        BSP_LCD_COLOR_CYAN, BSP_LCD_COLOR_MAGENTA, BSP_LCD_COLOR_YELLOW,
        BSP_LCD_COLOR_WHITE, BSP_LCD_COLOR_BLACK
    };
    const char *names[] = {
        "RED", "GREEN", "BLUE", "CYAN", "MAGENTA", "YELLOW", "WHITE", "BLACK"
    };

    uint16_t bar_w = BSP_LCD_WIDTH / 8;
    uint16_t i;

    for (i = 0; i < 8; i++) {
        BSP_LCD_FillRect(i * bar_w, 0, bar_w, 40, colors[i]);
        if (bar_w >= 48) {
            BSP_LCD_DrawString(i * bar_w + 4, 12, names[i], &Font8x16,
                               colors[i] == BSP_LCD_COLOR_BLACK
                                   ? BSP_LCD_COLOR_WHITE : BSP_LCD_COLOR_BLACK,
                               colors[i]);
        }
    }
}

static void test_lcd_shapes(void)
{
    /* 十字线 */
    BSP_LCD_DrawLine(0, 120, 239, 120, BSP_LCD_COLOR_GREY);
    BSP_LCD_DrawLine(120, 0, 120, 319, BSP_LCD_COLOR_GREY);

    /* 空心矩形 */
    BSP_LCD_DrawRect(20, 60, 60, 40, BSP_LCD_COLOR_CYAN, 0);
    BSP_LCD_DrawRect(160, 60, 60, 40, BSP_LCD_COLOR_MAGENTA, 1);

    /* 圆 */
    BSP_LCD_DrawCircle(60, 180, 30, BSP_LCD_COLOR_GREEN, 0);
    BSP_LCD_DrawCircle(180, 180, 30, BSP_LCD_COLOR_BLUE, 1);

    /* 对角线 */
    BSP_LCD_DrawLine(0, 0, 239, 319, BSP_LCD_COLOR_RED);
    BSP_LCD_DrawLine(239, 0, 0, 319, BSP_LCD_COLOR_RED);
}

static void test_lcd_fonts(void)
{
    BSP_LCD_DrawString(0, 0, "Font8x16: ABC abc 123 !@#",
                       &Font8x16, BSP_LCD_COLOR_WHITE, BSP_LCD_COLOR_BLACK);

    BSP_LCD_DrawString(0, 20, "Font16x24: ABC abc",
                       &Font16x24, BSP_LCD_COLOR_GREEN, BSP_LCD_COLOR_BLACK);

    BSP_LCD_DrawString(0, 50, "Font24x32: ABC",
                       &Font24x32, BSP_LCD_COLOR_CYAN, BSP_LCD_COLOR_BLACK);
}

static void test_lcd_touch_demo(void)
{
    /* 初始化触摸 */
    printf("[LCD Test] Initializing XPT2046 touch...\r\n");
    BSP_XPT2046_Init();

    /* 强制校准 */
    printf("[LCD Test] Starting touch calibration...\r\n");
    BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);
    BSP_LCD_DrawString(10, 10, "Touch Calibration Required",
                       &Font16x24, BSP_LCD_COLOR_YELLOW, BSP_LCD_COLOR_BLACK);
    BSP_LCD_DrawString(10, 40, "Tap the 4 cross points...",
                       &Font8x16, BSP_LCD_COLOR_WHITE, BSP_LCD_COLOR_BLACK);
    BSP_SYSTICK_DelayMS(1500);

    if (!BSP_XPT2046_TouchCalibrate()) {
        printf("[LCD Test] Calibration failed, using defaults\r\n");
    } else {
        printf("[LCD Test] Calibration OK\r\n");
    }

    /* 进入触摸画板 */
    BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);
    BSP_LCD_DrawString(10, 10, "Touch Demo - tap screen",
                       &Font8x16, BSP_LCD_COLOR_WHITE, BSP_LCD_COLOR_BLACK);
    BSP_LCD_DrawString(10, 40, "Press KEY1 to exit",
                       &Font8x16, BSP_LCD_COLOR_GREY, BSP_LCD_COLOR_BLACK);

    BSP_XPT2046_Coordinate_t coord;
    char buf[32];
    uint16_t last_x = 0xFFFF, last_y = 0xFFFF;  /* 上一帧坐标 */

    printf("[LCD Test] Touch demo running. Tap screen to draw, press KEY1 to exit.\r\n");

    for (;;) {
        /* KEY1 退出 */
        if (BSP_KEY_Scan(BSP_KEY_1) == 1) break;

        if (BSP_XPT2046_TouchFlag) {
            if (BSP_XPT2046_GetTouchPoint(&coord, NULL)) {
                /* 显示坐标 */
                if (coord.x != last_x || coord.y != last_y) {
                    snprintf(buf, sizeof(buf), "X:%3u Y:%3u    ", coord.x, coord.y);
                    BSP_LCD_FillRect(10, 70, 200, 24, BSP_LCD_COLOR_BLACK);
                    BSP_LCD_DrawString(10, 70, buf, &Font16x24,
                                       BSP_LCD_COLOR_YELLOW, BSP_LCD_COLOR_BLACK);

                    /* 画点 (带拖尾效果) */
                    BSP_LCD_SetPixel(coord.x, coord.y, BSP_LCD_COLOR_WHITE);
                    if (last_x != 0xFFFF) {
                        BSP_LCD_DrawLine(last_x, last_y, coord.x, coord.y, BSP_LCD_COLOR_WHITE);
                    }

                    last_x = coord.x;
                    last_y = coord.y;
                }
                BSP_XPT2046_TouchFlag = 0;
            }
        }
    }

    printf("[LCD Test] Touch demo exited.\r\n");
}

/* ================================================================
 * 测试入口
 * ================================================================ */

void Test_LCD_Display(void)
{
    uint16_t id;

    /* 1. 初始化 LCD */
    printf("\r\n[LCD Test] Initializing LCD (FSMC Bank4)...\r\n");
    BSP_LCD_Init();

    /* 2. 读取 LCD ID */
    id = BSP_LCD_ReadID();
    printf("[LCD Test] Controller ID: 0x%04X (%s)\r\n",
           id,
           id == BSP_LCD_ID_ILI9341  ? "ILI9341" :
           id == BSP_LCD_ID_ST7789V  ? "ST7789V" : "UNKNOWN");

    /* 3. 颜色条测试 */
    printf("[LCD Test] Color bar test...\r\n");
    test_lcd_color_bars();
    BSP_SYSTICK_DelayMS(1500);

    /* 4. 几何图形测试 */
    printf("[LCD Test] Shape drawing test...\r\n");
    BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);
    test_lcd_shapes();
    BSP_SYSTICK_DelayMS(2000);

    /* 5. 字体测试 */
    printf("[LCD Test] Font rendering test...\r\n");
    BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);
    test_lcd_fonts();
    BSP_SYSTICK_DelayMS(2000);

    printf("[LCD Test] Basic display tests completed.\r\n");

    /* 6. 触摸演示 */
    test_lcd_touch_demo();

    /* 7. 结束 */
    BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);
    BSP_LCD_DrawString(10, 140, "Test Complete!",
                       &Font16x24, BSP_LCD_COLOR_GREEN, BSP_LCD_COLOR_BLACK);
    BSP_SYSTICK_DelayMS(1000);
    BSP_LCD_BackLight(0);
    printf("[LCD Test] All tests done.\r\n");
}
