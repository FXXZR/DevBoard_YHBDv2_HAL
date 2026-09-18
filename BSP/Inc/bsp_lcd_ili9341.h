/**
 * @file    bsp_lcd_ili9341.h
 * @brief   BSP LCD 驱动接口 (ILI9341/ST7789V via FSMC 8080 并口)
 * @note    使用 FSMC Bank1 Sector4 (NE4), 基地址 0x6C000000, 16 位数据总线.
 *          A23 → LCD DC 脚: 命令=0x6C000000, 数据=0x6D000000.
 *
 *          所有绘图 API 显式传入颜色参数 (RGB565).
 */

#ifndef __BSP_LCD_ILI9341_H__
#define __BSP_LCD_ILI9341_H__

#include <stdint.h>
#include "bsp_fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 屏幕物理参数
 * ================================================================ */

enum {
    BSP_LCD_WIDTH  = 240,      /* X 方向像素数 (短边) */
    BSP_LCD_HEIGHT = 320,      /* Y 方向像素数 (长边) */
};

/* ================================================================
 * 常用 RGB565 颜色
 * ================================================================ */

enum {
    BSP_LCD_COLOR_WHITE   = 0xFFFF,
    BSP_LCD_COLOR_BLACK   = 0x0000,
    BSP_LCD_COLOR_GREY    = 0xF7DE,
    BSP_LCD_COLOR_BLUE    = 0x001F,
    BSP_LCD_COLOR_BLUE2   = 0x051F,
    BSP_LCD_COLOR_RED     = 0xF800,
    BSP_LCD_COLOR_MAGENTA = 0xF81F,
    BSP_LCD_COLOR_GREEN   = 0x07E0,
    BSP_LCD_COLOR_CYAN    = 0x7FFF,
    BSP_LCD_COLOR_YELLOW  = 0xFFE0,
};

/* ================================================================
 * 扫描方向
 * ================================================================ */

typedef enum {
    BSP_LCD_SCAN_0 = 0,     /* 上→下, 左→右 (横屏, X=240 Y=320) */
    BSP_LCD_SCAN_1 = 1,     /* 上→下, 右→左 (竖屏, X=320 Y=240) */
    BSP_LCD_SCAN_2 = 2,     /* 下→上, 左→右 (横屏, X=240 Y=320) */
    BSP_LCD_SCAN_3 = 3,     /* 下→上, 右→左 (竖屏, X=320 Y=240) */
    BSP_LCD_SCAN_4 = 4,     /* 左→右, 上→下 (横屏, X=240 Y=320) */
    BSP_LCD_SCAN_5 = 5,     /* 右→左, 上→下 (竖屏, X=320 Y=240) */
    BSP_LCD_SCAN_6 = 6,     /* 左→右, 下→上 (横屏, X=240 Y=320) — 默认 */
    BSP_LCD_SCAN_7 = 7,     /* 右→左, 下→上 (竖屏, X=320 Y=240) */
} BSP_LCD_ScanMode_t;

/* LCD 控制器 ID */
typedef enum {
    BSP_LCD_ID_UNKNOWN = 0,
    BSP_LCD_ID_ILI9341 = 0x9341,
    BSP_LCD_ID_ST7789V = 0x8552,
} BSP_LCD_ID_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化 LCD (GPIO + FSMC + 寄存器序列 + 背光 + 默认扫描方向)
 */
void BSP_LCD_Init(void);

/**
 * @brief  读取 LCD 控制器 ID
 * @return BSP_LCD_ID_ILI9341 / BSP_LCD_ID_ST7789V / BSP_LCD_ID_UNKNOWN
 */
uint16_t BSP_LCD_ReadID(void);

/**
 * @brief  背光控制
 * @param  on  1=点亮, 0=熄灭
 */
void BSP_LCD_BackLight(uint8_t on);

/**
 * @brief  设置扫描方向
 * @param  mode  取值 0~7, 同时影响 X/Y 轴长
 */
void BSP_LCD_SetScanMode(BSP_LCD_ScanMode_t mode);

/* ---- 基本绘图 (全部显式传入颜色) ---- */

/** @brief 清屏 (填充全屏指定颜色) */
void BSP_LCD_Clear(uint16_t color);

/**
 * @brief  画点
 * @param  x,y   像素坐标 (受当前扫描方向影响)
 * @param  color 像素颜色 (RGB565)
 */
void BSP_LCD_SetPixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief  读点
 * @return 像素颜色 (RGB565)
 */
uint16_t BSP_LCD_GetPixel(uint16_t x, uint16_t y);

/**
 * @brief  画线 (Bresenham 算法)
 * @param  x1,y1  起点
 * @param  x2,y2  终点
 * @param  color  颜色 (RGB565)
 */
void BSP_LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/**
 * @brief  画矩形
 * @param  x,y     左上角坐标
 * @param  w,h     宽/高 (像素)
 * @param  color   颜色 (RGB565)
 * @param  filled  0=空心, 1=实心
 */
void BSP_LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color, uint8_t filled);

/**
 * @brief  画圆 (Bresenham 算法)
 * @param  cx,cy   圆心
 * @param  r       半径
 * @param  color   颜色 (RGB565)
 * @param  filled  0=空心, 1=实心
 */
void BSP_LCD_DrawCircle(uint16_t cx, uint16_t cy, uint16_t r,
                        uint16_t color, uint8_t filled);

/**
 * @brief  在指定位置填充一个矩形区域 (批量画点, 用于字符绘制等)
 * @note   内部先设置绘图窗口再连续写数据, 比逐点绘制快得多
 */
void BSP_LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* ---- 字符/字符串 ---- */

/**
 * @brief  绘制 ASCII 字符 (使用当前字体)
 * @param  x,y      起始坐标 (受扫描方向影响)
 * @param  ch       字符
 * @param  font     字体指针 (&Font8x16 / &Font16x24 / &Font24x32)
 * @param  fg_color 前景色 (RGB565)
 * @param  bg_color 背景色 (RGB565)
 */
void BSP_LCD_DrawChar(uint16_t x, uint16_t y, char ch, const sFONT *font,
                      uint16_t fg_color, uint16_t bg_color);

/**
 * @brief  绘制字符串 (自动换行)
 * @param  x,y      起始坐标
 * @param  str      字符串 (以 \0 结尾)
 * @param  font     字体指针
 * @param  fg_color 前景色
 * @param  bg_color 背景色
 */
void BSP_LCD_DrawString(uint16_t x, uint16_t y, const char *str, const sFONT *font,
                        uint16_t fg_color, uint16_t bg_color);

/**
 * @brief  批量写入像素数据 (GRAM 窗口)
 * @param  x,y   左上角坐标
 * @param  w,h   宽/高 (像素)
 * @param  data  像素数据缓冲区 (RGB565, 共 w*h 个)
 * @note   用于 LVGL 等上层库的 flush 回调 — 先开窗再连续写数据, 比逐点快得多
 */
void BSP_LCD_WritePixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         const uint16_t *data);

/**
 * @brief  批量写入像素数据 — DMA 异步版 (DMA2_Channel1, mem2mem → FSMC)
 * @param  x,y      左上角坐标
 * @param  w,h      宽/高 (像素)
 * @param  data     像素数据缓冲区 (RGB565, 共 w*h 个, 需保持有效直到完成回调)
 * @param  done_cb  传输完成回调 (中断上下文调用, 可为 NULL)
 * @param  arg      回调参数透传
 * @note   开窗由 CPU 完成, 像素搬运由 DMA 后台执行; 完成回调在 DMA 中断里触发.
 *         供 LVGL 双缓冲 flush 使用: DMA 搬运 A 时 CPU 渲染 B
 */
void BSP_LCD_WritePixels_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             const uint16_t *data,
                             void (*done_cb)(void *arg), void *arg);

/**
 * @brief  阻塞等待 DMA 刷屏完成 (轮询 TC 标志, 不依赖中断)
 * @note   用于诊断/同步场景; 正常流程应使用 done_cb 回调
 */
void BSP_LCD_DMA_WaitDone(void);

                        
#ifdef __cplusplus
}
#endif

#endif /* __BSP_LCD_ILI9341_H__ */
