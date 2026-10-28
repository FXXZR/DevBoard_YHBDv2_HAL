/**
 * @file    bsp_fonts.h
 * @brief   显示字体资源 —— sFONT 类型定义 + 外部字体声明
 * @note    本模块为纯数据层, 不依赖 HAL 外设, 仅依赖 <stdint.h>.
 *          当前提供三套 ASCII 字体: 8x16, 16x24, 24x32.
 */

#ifndef __BSP_FONTS_H__
#define __BSP_FONTS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 字体描述结构
 * ================================================================ */

typedef struct {
    const uint8_t *table;   /* 字模数据指针 */
    uint16_t       Width;   /* 字符宽度 (像素) */
    uint16_t       Height;  /* 字符高度 (像素) */
} sFONT;

/* ================================================================
 * 可用字体 (extern, 定义在 bsp_fonts.c)
 * ================================================================ */

extern sFONT Font8x16;    /* 8x16 ASCII (consolas 风格) */
extern sFONT Font16x24;   /* 16x24 ASCII */
extern sFONT Font24x32;   /* 24x32 ASCII */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_FONTS_H__ */
