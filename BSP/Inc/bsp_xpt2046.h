/**
 * @file    bsp_xpt2046.h
 * @brief   BSP 触摸屏驱动接口 (XPT2046)
 * @note    XPT2046 通过软件 SPI 通信: PG7=CLK, PF11=MOSI, PF6=MISO, PF10=CS.
 *          触摸检测通过 EXTI 中断 (PF9, 下降沿).
 *
 *          校准算法基于三参考点仿射变换.
 *          触摸坐标自动转换为 LCD 像素坐标 (需先完成校准).
 */

#ifndef __BSP_XPT2046_H__
#define __BSP_XPT2046_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 类型定义
 * ================================================================ */

/** @brief 触摸坐标 (像素) */
typedef struct {
    uint16_t x;
    uint16_t y;
} BSP_XPT2046_Coordinate_t;

/** @brief 校准系数 (浮点形式, double) */
typedef struct {
    double dX_X;
    double dX_Y;
    double dX;
    double dY_X;
    double dY_Y;
    double dY;
} BSP_XPT2046_TouchPara_t;

/* ================================================================
 * 全局状态
 * ================================================================ */

/** @brief 触摸中断标志 (在 EXTI ISR 中置位, 应用层轮询后清零) */
extern volatile uint8_t BSP_XPT2046_TouchFlag;

/** @brief 触摸坐标 (中断回调中更新) */
extern volatile BSP_XPT2046_Coordinate_t BSP_XPT2046_TouchPoint;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化 XPT2046 (GPIO/SPI + EXTI 中断)
 * @note   依赖 LCD 已初始化 (校准 UI 需要绘图).
 */
void BSP_XPT2046_Init(void);

/**
 * @brief  执行触摸校准流程
 * @note   在屏幕上依次显示 4 个十字参考点, 用户触摸后自动采样.
 *         校准完成后系数存入全局 TouchPara.
 *         此函数包含屏幕绘制操作 (依赖 LCD BSP).
 * @return 1=成功, 0=失败
 */
uint8_t BSP_XPT2046_TouchCalibrate(void);

/**
 * @brief  获取触摸点坐标 (校准后)
 * @param  pCoord  [out] 指向坐标结构体的指针
 * @param  pPara   [in]  指向校准系数的指针 (NULL 表示使用默认系数)
 * @return 1=触摸有效, 0=无触摸
 */
uint8_t BSP_XPT2046_GetTouchPoint(BSP_XPT2046_Coordinate_t *pCoord,
                                   const BSP_XPT2046_TouchPara_t *pPara);

/**
 * @brief  查询触摸是否被按住 (电平检测, 非边沿)
 * @return 1=按住, 0=松开
 * @note   直接读 IRQ 引脚电平 (PF9, 低电平有效)。
 *         用于轮询场景 (如 LVGL read_cb) 在 TouchFlag 边沿之外维持按住状态。
 */
uint8_t BSP_XPT2046_IsPressed(void);

/**
 * @brief  触摸 EXTI 线 (PF9) 的处理入口
 * @note   由 bsp_exti.c 的 EXTI9_5_IRQHandler 分发调用 —— 该向量与 W5500 的
 *         INT (PG8) 共用, 所以向量本身不归本模块。业务代码不要直接调。
 */
void BSP_XPT2046_ExtiHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_XPT2046_H__ */
