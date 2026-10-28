/**
 * @file    bsp_mpu6050.c
 * @brief   BSP MPU6050 6轴运动传感器驱动实现
 * @note    通过条件编译选择底层 I2C 后端:
 *            BSP_MPU6050_USE_SOFT_I2C → bsp_soft_i2c (默认)
 *            BSP_MPU6050_USE_HW_I2C   → bsp_i2c
 *
 *          I2C 速度不通过宏定义，而是内部 static const 常量。
 *
 *          总线共享说明: 本模块与 EEPROM (AT24C02)、AP3216C 共用 I2C1
 *          (PB6=SCL, PB7=SDA)。总线速率由首个调用 _Init() 的模块决定。
 *          当前所有模块统一使用 400KHz。
 *
 *          参考: MPU-6000/MPU-6050 Register Map (InvenSense RM-MPU-6000A-00)
 */

#include "bsp_mpu6050.h"
#include "bsp_systick.h"

/* ---- 底层 I2C 后端选择 (条件编译 — 唯一合理的宏场景) ---- */
#if defined(BSP_MPU6050_USE_HW_I2C)
#include "bsp_i2c.h"
#define MPU6050_I2C_BUS_ID       BSP_I2C_BUS_1
#define mpu6050_i2c_init(id, sp) BSP_I2C_Init((id), (sp))
#define mpu6050_i2c_mem_write(id, dev, ma, mas, d, l, to) \
    BSP_I2C_MemWrite((id), (dev), (ma), (mas), (d), (l), (to))
#define mpu6050_i2c_mem_read(id, dev, ma, mas, d, l, to) \
    BSP_I2C_MemRead((id), (dev), (ma), (mas), (d), (l), (to))
#define mpu6050_i2c_ready(id, dev, tr, to) \
    BSP_I2C_IsDeviceReady((id), (dev), (tr), (to))
#define MPU6050_I2C_OK           BSP_I2C_OK
#define MPU6050_I2C_NACK         BSP_I2C_ERROR   /* HW I2C 不区分 NACK */
#define MPU6050_I2C_TIMEOUT      BSP_I2C_TIMEOUT

#else /* BSP_MPU6050_USE_SOFT_I2C (default) */
#include "bsp_soft_i2c.h"
#define MPU6050_I2C_BUS_ID       BSP_SOFT_I2C_BUS_1
#define mpu6050_i2c_init(id, sp) BSP_SoftI2C_Init((id), (BSP_SoftI2C_Speed_t)(sp))
#define mpu6050_i2c_mem_write(id, dev, ma, mas, d, l, to) \
    BSP_SoftI2C_MemWrite((id), (dev), (ma), (mas), (d), (l), (to))
#define mpu6050_i2c_mem_read(id, dev, ma, mas, d, l, to) \
    BSP_SoftI2C_MemRead((id), (dev), (ma), (mas), (d), (l), (to))
#define mpu6050_i2c_ready(id, dev, tr, to) \
    BSP_SoftI2C_IsDeviceReady((id), (dev), (tr), (to))
#define MPU6050_I2C_OK           BSP_SOFT_I2C_OK
#define MPU6050_I2C_NACK         BSP_SOFT_I2C_NACK
#define MPU6050_I2C_TIMEOUT      BSP_SOFT_I2C_TIMEOUT
#endif

/* ================================================================
 * 内部常量 (static const — 不对外暴露)
 *
 * 按功能分三组: 寄存器地址 / 初始化配置与换算系数 / 总线速率。
 * 类型不导出, 所以用匿名 struct, 靠实例名区分字段归属。
 * ================================================================ */

static const uint32_t mpu6050_i2c_speed = 400000U;   /* 400KHz (共享总线统一速率) */

/* ---- 寄存器地址 (RM-MPU-6000A-00) ---- */
static const struct {
    uint8_t pwr_mgmt1;      /* 电源管理 1 */
    uint8_t smplrt_div;     /* 采样率分频器 */
    uint8_t config;         /* 配置 (DLPF) */
    uint8_t gyro_config;    /* 陀螺仪配置 */
    uint8_t accel_config;   /* 加速度计配置 */
    uint8_t accel_xout_h;   /* 加速度数据起始 (burst read 起点) */
    uint8_t who_am_i;       /* WHO_AM_I 设备 ID */
} mpu6050_regs = {
    .pwr_mgmt1    = 0x6B,
    .smplrt_div   = 0x19,
    .config       = 0x1A,
    .gyro_config  = 0x1B,
    .accel_config = 0x1C,
    .accel_xout_h = 0x3B,
    .who_am_i     = 0x75,
};

/* ---- 初始化配置值与换算系数 ---- */
static const struct {
    uint8_t  wake_up;             /* PWR_MGMT_1: 唤醒, 内部 8MHz 时钟 */
    uint8_t  smplrt_div;          /* 采样率 = 8kHz / (1+7) = 1kHz */
    uint8_t  dlpf_cfg;            /* DLPF_CFG=6 → 5Hz 带宽 */
    uint8_t  accel_fs;            /* AFS_SEL=1 → ±4g, HPF bypass */
    uint8_t  gyro_fs;             /* FS_SEL=3 → ±2000°/s, 自检关闭 */
    uint8_t  who_am_i_expected;
    uint32_t pwr_on_delay_ms;     /* 上电稳定延时 */
    uint32_t stabilize_delay_ms;  /* 配置后稳定延时 */
    float    temp_scale;          /* 温度公式: raw/340 + 36.53 */
    float    temp_offset;
} mpu6050_cfg = {
    .wake_up            = 0x00,
    .smplrt_div         = 0x07,
    .dlpf_cfg           = 0x06,
    .accel_fs           = 0x08,
    .gyro_fs            = 0x18,
    .who_am_i_expected  = 0x68,
    .pwr_on_delay_ms    = 100U,
    .stabilize_delay_ms = 200U,
    .temp_scale         = 340.0f,
    .temp_offset        = 36.53f,
};

/* 数据 burst read 长度: 0x3B 起连续 14 字节。
 * 要当栈数组尺寸用, 必须是编译期整数 —— 按规范这类用宏。 */
#define MPU6050_BURST_LEN   14U

/* ================================================================
 * 内部: 错误码映射 — 将 I2C 层错误码映射为 MPU6050 层错误码
 * ================================================================ */

/**
 * @brief  将 I2C 层状态码映射为 MPU6050 层状态码 (保留 NACK/TIMEOUT 信息)
 */
static BSP_MPU6050_Status_t mpu6050_map_i2c_err(uint32_t i2c_status)
{
    if (i2c_status == MPU6050_I2C_OK)      return BSP_MPU6050_OK;
    if (i2c_status == MPU6050_I2C_TIMEOUT) return BSP_MPU6050_ERR_TIMEOUT;
    if (i2c_status == MPU6050_I2C_NACK)    return BSP_MPU6050_ERR_NACK;
    return BSP_MPU6050_ERROR;
}

/* ================================================================
 * 内部: I2C 寄存器读写封装
 * ================================================================ */

/** @brief 向 MPU6050 寄存器单字节写 */
static BSP_MPU6050_Status_t mpu6050_write_reg(uint8_t reg, uint8_t val)
{
    uint32_t ret = mpu6050_i2c_mem_write(MPU6050_I2C_BUS_ID,
                                         (uint8_t)BSP_MPU6050_DEV_ADDR,
                                         reg, 1,
                                         &val, 1,
                                         BSP_MPU6050_I2C_TIMEOUT_MS);
    return mpu6050_map_i2c_err(ret);
}

/** @brief 从 MPU6050 寄存器读取指定字节数 */
static BSP_MPU6050_Status_t mpu6050_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint32_t ret = mpu6050_i2c_mem_read(MPU6050_I2C_BUS_ID,
                                        (uint8_t)BSP_MPU6050_DEV_ADDR,
                                        reg, 1,
                                        buf, len,
                                        BSP_MPU6050_I2C_TIMEOUT_MS);
    return mpu6050_map_i2c_err(ret);
}

/* ================================================================
 * 内部: 14 字节 burst read 解析为数据 struct
 * ================================================================ */

/**
 * @brief  将 14 字节原始 buffer 解包为 BSP_MPU6050_Data_t
 * @note   buf 布局 (大端):
 *           [ 0: 1] ACCEL_XOUT_H/L
 *           [ 2: 3] ACCEL_YOUT_H/L
 *           [ 4: 5] ACCEL_ZOUT_H/L
 *           [ 6: 7] TEMP_OUT_H/L
 *           [ 8: 9] GYRO_XOUT_H/L
 *           [10:11] GYRO_YOUT_H/L
 *           [12:13] GYRO_ZOUT_H/L
 */
static void mpu6050_parse_data(const uint8_t buf[], BSP_MPU6050_Data_t *p_data)
{
    /* 加速度: 大端 → int16_t */
    p_data->accel_x = (int16_t)((buf[ 0] << 8) | buf[ 1]);
    p_data->accel_y = (int16_t)((buf[ 2] << 8) | buf[ 3]);
    p_data->accel_z = (int16_t)((buf[ 4] << 8) | buf[ 5]);

    /* 温度: 大端 → 先转为 signed int16_t, 再换算 °C */
    {
        int16_t raw_temp = (int16_t)((buf[6] << 8) | buf[7]);
        p_data->temp_c = (float)raw_temp / mpu6050_cfg.temp_scale + mpu6050_cfg.temp_offset;
    }

    /* 陀螺仪: 大端 → int16_t */
    p_data->gyro_x  = (int16_t)((buf[ 8] << 8) | buf[ 9]);
    p_data->gyro_y  = (int16_t)((buf[10] << 8) | buf[11]);
    p_data->gyro_z  = (int16_t)((buf[12] << 8) | buf[13]);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

BSP_MPU6050_Status_t BSP_MPU6050_Init(void)
{
    BSP_MPU6050_Status_t ret;

    /* ---- I2C 总线初始化 (幂等) ---- */
    mpu6050_i2c_init(MPU6050_I2C_BUS_ID, mpu6050_i2c_speed);

    /* ---- 上电稳定延时 ---- */
    BSP_SYSTICK_DelayMS(mpu6050_cfg.pwr_on_delay_ms);

    /* ---- 唤醒设备 (退出睡眠模式) ---- */
    ret = mpu6050_write_reg(mpu6050_regs.pwr_mgmt1, mpu6050_cfg.wake_up);
    if (ret != BSP_MPU6050_OK) return ret;

    /* ---- 配置采样率: 1kHz ---- */
    ret = mpu6050_write_reg(mpu6050_regs.smplrt_div, mpu6050_cfg.smplrt_div);
    if (ret != BSP_MPU6050_OK) return ret;

    /* ---- 配置 DLPF: 5Hz 带宽 ---- */
    ret = mpu6050_write_reg(mpu6050_regs.config, mpu6050_cfg.dlpf_cfg);
    if (ret != BSP_MPU6050_OK) return ret;

    /* ---- 配置加速度计: ±4g ---- */
    ret = mpu6050_write_reg(mpu6050_regs.accel_config, mpu6050_cfg.accel_fs);
    if (ret != BSP_MPU6050_OK) return ret;

    /* ---- 配置陀螺仪: ±2000°/s ---- */
    ret = mpu6050_write_reg(mpu6050_regs.gyro_config, mpu6050_cfg.gyro_fs);
    if (ret != BSP_MPU6050_OK) return ret;

    /* ---- 等待 PLL 稳定 ---- */
    BSP_SYSTICK_DelayMS(mpu6050_cfg.stabilize_delay_ms);

    /* ---- WHO_AM_I 校验 ---- */
    {
        uint8_t who_am_i = 0;
        ret = mpu6050_read_regs(mpu6050_regs.who_am_i, &who_am_i, 1);
        if (ret != BSP_MPU6050_OK) return ret;

        if (who_am_i != mpu6050_cfg.who_am_i_expected) {
            return BSP_MPU6050_ERR_ID_MISMATCH;
        }
    }

    return BSP_MPU6050_OK;
}

BSP_MPU6050_Status_t BSP_MPU6050_ReadAll(BSP_MPU6050_Data_t *p_data)
{
    if (!p_data) return BSP_MPU6050_ERROR;

    uint8_t buf[MPU6050_BURST_LEN];
    BSP_MPU6050_Status_t ret = mpu6050_read_regs(mpu6050_regs.accel_xout_h, buf, MPU6050_BURST_LEN);
    if (ret != BSP_MPU6050_OK) return ret;

    mpu6050_parse_data(buf, p_data);

    return BSP_MPU6050_OK;
}

BSP_MPU6050_Status_t BSP_MPU6050_IsReady(void)
{
    uint32_t ret = mpu6050_i2c_ready(MPU6050_I2C_BUS_ID,
                                     (uint8_t)BSP_MPU6050_DEV_ADDR,
                                     3, 5);
    return mpu6050_map_i2c_err(ret);
}
