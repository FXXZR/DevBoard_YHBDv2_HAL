/**
 * @file    bsp_mpu6050.h
 * @brief   BSP MPU6050 6轴运动传感器驱动 (I2C)
 * @note    通过条件编译选择底层 I2C 后端:
 *            BSP_MPU6050_USE_SOFT_I2C → bsp_soft_i2c (默认)
 *            BSP_MPU6050_USE_HW_I2C   → bsp_i2c
 *
 *          I2C 总线 (PB6=SCL, PB7=SDA) 与 EEPROM/AP3216C 共享,
 *          总线速率由首个调用 _Init() 的模块决定 (当前为 400KHz)。
 *
 *          配置: ±4g 加速度量程, ±2000°/s 陀螺仪量程, 1kHz 采样率, 5Hz DLPF。
 *          数据 struct 存放原始 int16_t 值, 头文件提供 static inline 物理量转换函数。
 */

#ifndef __BSP_MPU6050_H__
#define __BSP_MPU6050_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * I2C 后端选择 (二选一)
 * ================================================================ */

#define BSP_MPU6050_USE_SOFT_I2C       /* 默认: BSP 软件 I2C */
// #define BSP_MPU6050_USE_HW_I2C      /* 备选: HAL 硬件 I2C */

/* ================================================================
 * 配置常量 (匿名 enum, 暴露给应用层)
 * ================================================================ */

enum {
    BSP_MPU6050_DEV_ADDR       = 0x68,   /* 7-bit I2C address (AD0=GND, 默认) */
    BSP_MPU6050_I2C_TIMEOUT_MS = 100,    /* I2C 操作超时 (ms) */
    BSP_MPU6050_WHO_AM_I_VAL   = 0x68,   /* WHO_AM_I 寄存器期望值 */
};

/* ================================================================
 * 状态码
 * ================================================================ */

/** @brief MPU6050 操作状态码 */
typedef enum {
    BSP_MPU6050_OK             = 0,      /* 操作成功 */
    BSP_MPU6050_ERROR          = 1,      /* 未知错误 */
    BSP_MPU6050_ERR_NACK       = 2,      /* 设备 NACK (无应答) */
    BSP_MPU6050_ERR_TIMEOUT    = 3,      /* I2C 总线超时 */
    BSP_MPU6050_ERR_ID_MISMATCH = 4,     /* WHO_AM_I 校验失败 */
} BSP_MPU6050_Status_t;

/* ================================================================
 * 传感器数据
 * ================================================================ */

/** @brief MPU6050 传感器原始数据 */
typedef struct {
    int16_t accel_x;     /* 加速度 X 原始值 (±32768 → ±4g)   */
    int16_t accel_y;     /* 加速度 Y 原始值                      */
    int16_t accel_z;     /* 加速度 Z 原始值                      */
    int16_t gyro_x;      /* 角速度 X 原始值 (±32768 → ±2000°/s) */
    int16_t gyro_y;      /* 角速度 Y 原始值                      */
    int16_t gyro_z;      /* 角速度 Z 原始值                      */
    float   temp_c;      /* 温度 (已转换为 °C)                   */
} BSP_MPU6050_Data_t;

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief  初始化 MPU6050 传感器
 * @note   幂等: 多次调用安全。内部执行:
 *          1. I2C 总线初始化 (若未初始化)
 *          2. 唤醒设备 → 配置寄存器 → WHO_AM_I 校验
 *          3. 等待陀螺仪 PLL 稳定 (200ms)
 * @retval BSP_MPU6050_OK             初始化成功
 * @retval BSP_MPU6050_ERR_ID_MISMATCH WHO_AM_I 与期望值不匹配
 * @retval 其他                         I2C 通信错误
 */
BSP_MPU6050_Status_t BSP_MPU6050_Init(void);

/**
 * @brief  一次 burst read 获取全部传感器数据
 * @param  p_data  [出参] 传感器数据 struct 指针 (不可为 NULL)
 * @note   14 字节 burst read (0x3B→0x48): 加速度(6) + 温度(2) + 陀螺仪(6)
 * @retval BSP_MPU6050_OK   读取成功
 * @retval 其他              I2C 通信错误
 */
BSP_MPU6050_Status_t BSP_MPU6050_ReadAll(BSP_MPU6050_Data_t *p_data);

/**
 * @brief  检查设备是否就绪 (发送 I2C 地址, 等待 ACK)
 * @retval BSP_MPU6050_OK   设备在线
 * @retval 其他              I2C 通信错误或设备无应答
 */
BSP_MPU6050_Status_t BSP_MPU6050_IsReady(void);

/* ================================================================
 * 原始值 → 物理量转换 (内联函数, 不占 FLASH 调用开销)
 * ================================================================ */

/** @brief 加速度原始值 → g */
static inline float BSP_MPU6050_Accel_ToG(int16_t raw) {
    /* ±4g 量程: LSB = 32768 / 4 = 8192 */
    return (float)raw / 8192.0f;
}

/** @brief 角速度原始值 → °/s */
static inline float BSP_MPU6050_Gyro_ToDegPerSec(int16_t raw) {
    /* ±2000°/s 量程: LSB = 32768 / 2000 = 16.384 */
    return (float)raw / 16.384f;
}

/** @brief 温度原始值 → °C */
static inline float BSP_MPU6050_Temp_ToC(int16_t raw) {
    /* MPU6050 标准公式: Temp = raw / 340 + 36.53 */
    return (float)raw / 340.0f + 36.53f;
}

#ifdef __cplusplus
}
#endif

#endif /* __BSP_MPU6050_H__ */
