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

/* 编码 = mode<<5 写入 MADCTL (0x36): MY(bit7)=行反向, MX(bit6)=列反向, MV(bit5)=行列互换
 * 偶数模式=横屏 (X=240 Y=320), 奇数模式=竖屏 (X=320 Y=240)
 *
 * ==== 屏幕成像方向会随固件布局漂移 (2026-09 重测, 改动默认值前必读) ====
 * 现象: 同一份源码, 只要二进制的体积/布局变一点, 成像方向就可能整体 180° 翻转.
 *       实测: 关掉 21 个未使用的 LVGL 模块 (306584 B) → 用 SCAN_6 时翻转;
 *             把它们重新打开 (375044 B) → 同样的 SCAN_6 就是正向.
 *       逐步开模块二分后, 临界点夹在 317924 B 与 328504 B 之间 (约 10.5 KB).
 *       注意 LCD 驱动的机器码在所有这些版本里逐字节相同, 只是落在了不同 Flash 地址.
 *
 * 已排除 (逐条实测, 不要再重复走):
 *   - MADCTL 内容被改写: 不存在. 写 0xC0 读回 0xC0, 但面板不响应读命令时
 *     读到的是总线残留; 已用"故意留 0x00EF 残留再读回"判别法确认读回可信.
 *     在【确认翻转】的 build 上读回 MADCTL 同样是 0xC0 —— 与不翻转时完全一致.
 *   - 面板状态差异: 0x09/0x0A/0x0C 等可读寄存器在两个档位里逐字节相同
 *     (0x0A=0x9C 说明确实已显示开启、退出睡眠).
 *   - 长延时不足: 复位低电平/释放后/sleep-out 后全改为真实毫秒 (10/120/120ms,
 *     手册要求) 后仍然翻转.
 *   - FSMC 时序参数: DATAST 4→8 和 ADDSET 1→15 都试过, 两次 FLASH 与基准一字不差,
 *     仍然翻转. 这一类实验在设计上就不可能影响本现象 —— FSMC 的 NWE/NOE/CS 波形
 *     由硬件从 HCLK 分频生成, 与代码布局无关. 不必再试.
 *   - DMA 刷屏路径: 校准十字出现在 lv_timer_handler() 首次调用之前, flush 一次都没跑过.
 *   - 两个初始化序列的方向类寄存器完全相同 (0x36=0x00, 0xB6=0x0A/0xA2), 枚举值两版一致.
 *   - BSP_SRAM_Init() 单独不足以触发: f385c75 + 它 = 正向.
 *
 * 已知定位精度: 对翻转档 (317924 B) 与正向档 (328504 B) 做 nm 比对, 整个 LCD 驱动
 *   被整体平移 0x34 = 52 字节 (多出来的 10.5 KB 里只有这 52 字节落在驱动之前,
 *   其余全落在驱动之后). 就是这个平移决定了成像方向.
 *
 * 结论: 根因尚未确定. 两个档位的可读寄存器完全一致, 所以嫌疑落在"某条只写不可读的
 *       面板寄存器是否落到面板上"; 但 FSMC 波形既与布局无关, 这一步的物理机制目前
 *       没有解释, 需要示波器对比测量才能收口.
 *       当前生产配置用 SCAN_0 是对该现象的经验性规避, 不是对 MADCTL 语义的修正. */
typedef enum {
    BSP_LCD_SCAN_0 = 0,     /* 上→下, 左→右 (横屏, X=240 Y=320) — 当前生产默认 (见上, 经验性规避) */
    BSP_LCD_SCAN_1 = 1,     /* 上→下, 右→左 (竖屏, X=320 Y=240) */
    BSP_LCD_SCAN_2 = 2,     /* 下→上, 左→右 (横屏, X=240 Y=320) */
    BSP_LCD_SCAN_3 = 3,     /* 下→上, 右→左 (竖屏, X=320 Y=240) */
    BSP_LCD_SCAN_4 = 4,     /* 左→右, 上→下 (横屏, X=240 Y=320) */
    BSP_LCD_SCAN_5 = 5,     /* 右→左, 上→下 (竖屏, X=320 Y=240) */
    BSP_LCD_SCAN_6 = 6,     /* 左→右, 下→上 (横屏, X=240 Y=320) — 野火参考驱动的默认值 */
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
