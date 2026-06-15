/**
 * @file    bsp_adc.h
 * @brief   BSP ADC 驱动接口 —— 平台无关的 ADC 采样 API
 * @note    本文件只暴露抽象接口，不包含任何 HAL/寄存器依赖。
 *          更换 MCU 或 ADC 通道时只需修改对应的 .c 实现文件。
 *          PC1 → ADC1_CH11, DMA1_Ch1 循环转换, 12bit 分辨率。
 */

#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * ADC 编号枚举
 * ================================================================ */
typedef enum {
    BSP_ADC_1 = 0,      /* ADC1 — PC1 / Channel 11 / DMA1_Ch1 */
    BSP_ADC_COUNT       /* ADC 总数（自动计算） */
} BSP_ADC_Id_t;

/* ================================================================
 * 配置常量
 * ================================================================ */
#define BSP_ADC_VREF_MV  3300U   /* ADC 参考电压 (mV)，默认 3.3V */
#define BSP_ADC_RES_MAX  4096U   /* 12-bit ADC 最大量化值 */

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化指定 ADC 通道（含 DMA 循环转换配置）
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用。
 *         初始化完成后 DMA 已在后台循环搬运，无需手动 Start。
 */
void BSP_ADC_Init(BSP_ADC_Id_t id);

/**
 * @brief  启动 ADC DMA 连续转换（Init 后自动启动，通常无需再调用）
 * @param  id  ADC 编号
 */
void BSP_ADC_Start(BSP_ADC_Id_t id);

/**
 * @brief  停止 ADC DMA 连续转换
 * @param  id  ADC 编号
 */
void BSP_ADC_Stop(BSP_ADC_Id_t id);

/**
 * @brief  读取最近一次 ADC 转换结果（毫伏）
 * @param  id  ADC 编号
 * @return 电压值 (mV)，范围 0 ~ BSP_ADC_VREF_MV (3300)
 * @note   DMA 后台循环更新，本函数直接返回最新值，零等待。
 */
uint16_t BSP_ADC_Read_mV(BSP_ADC_Id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ADC_H__ */
