/**
 * @file    bsp_ap3216c.h
 * @brief   BSP AP3216C 环境光/接近/红外三合一传感器驱动 (I2C)
 * @note    通过条件编译选择底层 I2C 后端:
 *            BSP_AP3216C_USE_SOFT_I2C → bsp_soft_i2c (默认)
 *            BSP_AP3216C_USE_HW_I2C   → bsp_i2c
 *
 *          I2C 总线 (PB6=SCL, PB7=SDA) 与 EEPROM/MPU6050 共享,
 *          总线速率由首个调用 _Init() 的模块决定 (当前为 400KHz)。
 *
 *          初版为纯轮询模式, 不使用 INT 引脚 (PF8)。
 *          ALS 量程作为 Init() 参数传入; PS 增益使用默认值 (×2)。
 */

#ifndef __BSP_AP3216C_H__
#define __BSP_AP3216C_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * I2C 后端选择 (二选一)
 * ================================================================ */

#define BSP_AP3216C_USE_SOFT_I2C        /* 默认: BSP 软件 I2C */
// #define BSP_AP3216C_USE_HW_I2C       /* 备选: HAL 硬件 I2C */

/* ================================================================
 * 配置常量 (匿名 enum, 暴露给应用层 — 直接改这里即可调参)
 * ================================================================ */

enum {
    BSP_AP3216C_DEV_ADDR        = 0x1E,   /* 7-bit I2C 地址 */
    BSP_AP3216C_I2C_TIMEOUT_MS  = 100,    /* I2C 操作超时 (ms) */
};

/* ---- ALS 量程选择 (Init 参数) ---- */
typedef enum {
    BSP_AP3216C_ALS_RANGE_20661  = 0,    /* 0~20661 lux, 0.35 lux/count    (户外)   */
    BSP_AP3216C_ALS_RANGE_5162   = 1,    /* 0~5162  lux, 0.0788 lux/count  (室内亮) */
    BSP_AP3216C_ALS_RANGE_1291   = 2,    /* 0~1291  lux, 0.0197 lux/count  (室内暗) */
    BSP_AP3216C_ALS_RANGE_323    = 3,    /* 0~323   lux, 0.0049 lux/count  (暗光)   */
} BSP_AP3216C_AlsRange_t;

/* ================================================================
 * 状态码
 * ================================================================ */

/** @brief AP3216C 操作状态码 */
typedef enum {
    BSP_AP3216C_OK            = 0,        /* 操作成功 */
    BSP_AP3216C_ERROR         = 1,        /* 未知错误 */
    BSP_AP3216C_ERR_NACK      = 2,        /* 设备 NACK (无应答) */
    BSP_AP3216C_ERR_TIMEOUT   = 3,        /* I2C 总线超时 */
} BSP_AP3216C_Status_t;

/* ================================================================
 * 传感器数据
 * ================================================================ */

/** @brief AP3216C 传感器数据 */
typedef struct {
    float    als_lux;       /* 环境光强度 (lux)                     */
    uint16_t ps;            /* 接近传感器值 (0~1023, 10-bit)        */
    uint16_t ir;            /* 红外强度 (原始 ADC 值, 10-bit)       */
    uint8_t  ir_saturated;  /* IR 饱和标志 (1=PS 值不可靠)          */
    uint8_t  approaching;   /* 物体方向 (1=接近中, 0=远离中)         */
} BSP_AP3216C_Data_t;

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief  初始化 AP3216C 传感器
 * @param  range  ALS 量程, 取值: BSP_AP3216C_ALS_RANGE_20661 / _5162 / _1291 / _323
 * @note   幂等: 多次调用安全。PS 增益用默认值 (×2)。
 *          内部执行:
 *          1. I2C 总线初始化 (若未初始化)
 *          2. 软件复位 → 延时 100ms
 *          3. 配置 ALS 量程 → 设置为 ALS+PS 工作模式 → 延时 100ms
 * @retval BSP_AP3216C_OK   初始化成功
 * @retval 其他              I2C 通信错误
 */
BSP_AP3216C_Status_t BSP_AP3216C_Init(BSP_AP3216C_AlsRange_t range);

/**
 * @brief  一次读取全部传感器数据 (ALS + PS + IR)
 * @param  p_data  [出参] 传感器数据 struct 指针 (不可为 NULL)
 * @note   连续读取 6 字节 (0x0A→0x0F): IR(2) + ALS(2) + PS(2)
 * @retval BSP_AP3216C_OK   读取成功
 * @retval 其他              I2C 通信错误
 */
BSP_AP3216C_Status_t BSP_AP3216C_ReadAll(BSP_AP3216C_Data_t *p_data);

/**
 * @brief  单独读取环境光强度
 * @param  p_lux  [出参] 环境光 lux 值
 * @retval BSP_AP3216C_OK   读取成功
 * @retval 其他              I2C 通信错误
 */
BSP_AP3216C_Status_t BSP_AP3216C_ReadALS(float *p_lux);

/**
 * @brief  单独读取接近传感器数据
 * @param  p_ps  [出参] 10-bit 接近值 (0~1023)
 * @note   若 IR 过饱和导致 PS 无效, 返回值仍为无符号数但不可信;
 *         建议改用 ReadAll() 检查 ir_saturated 标志。
 * @retval BSP_AP3216C_OK   读取成功
 * @retval 其他              I2C 通信错误
 */
BSP_AP3216C_Status_t BSP_AP3216C_ReadPS(uint16_t *p_ps);

/**
 * @brief  单独读取红外强度
 * @param  p_ir  [出参] 10-bit 红外原始值
 * @retval BSP_AP3216C_OK   读取成功
 * @retval 其他              I2C 通信错误
 */
BSP_AP3216C_Status_t BSP_AP3216C_ReadIR(uint16_t *p_ir);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_AP3216C_H__ */
