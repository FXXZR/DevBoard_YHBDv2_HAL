/**
 * @file    bsp_ap3216c.c
 * @brief   BSP AP3216C 环境光/接近/红外三合一传感器驱动实现
 * @note    通过条件编译选择底层 I2C 后端:
 *            BSP_AP3216C_USE_SOFT_I2C → bsp_soft_i2c (默认)
 *            BSP_AP3216C_USE_HW_I2C   → bsp_i2c
 *
 *          I2C 速度不通过宏定义，而是内部 static const 常量。
 *
 *          总线共享说明: 本模块与 EEPROM (AT24C02)、MPU6050 共用 I2C1
 *          (PB6=SCL, PB7=SDA)。总线速率由首个调用 _Init() 的模块决定。
 *          当前所有模块统一使用 400KHz。
 *
 *          初版纯轮询模式, 不使用 INT 引脚 (PF8)。
 *          寄存器读写顺序严格对齐官方参考代码 (野火 HAL 库例程)。
 *          PS 增益/IR LED 驱动等均使用硬件默认值。
 *
 *          参考: AP3216C Datasheet + 野火 STM32 HAL 库例程
 */

#include "bsp_ap3216c.h"
#include "bsp_systick.h"

/* ---- 底层 I2C 后端选择 (条件编译 — 唯一合理的宏场景) ---- */
#if defined(BSP_AP3216C_USE_HW_I2C)
#include "bsp_i2c.h"
#define AP3216C_I2C_BUS_ID       BSP_I2C_BUS_1
#define ap3216c_i2c_init(id, sp) BSP_I2C_Init((id), (sp))
#define ap3216c_i2c_mem_write(id, dev, ma, mas, d, l, to) \
    BSP_I2C_MemWrite((id), (dev), (ma), (mas), (d), (l), (to))
#define ap3216c_i2c_mem_read(id, dev, ma, mas, d, l, to) \
    BSP_I2C_MemRead((id), (dev), (ma), (mas), (d), (l), (to))
#define ap3216c_i2c_ready(id, dev, tr, to) \
    BSP_I2C_IsDeviceReady((id), (dev), (tr), (to))
#define AP3216C_I2C_OK           BSP_I2C_OK
#define AP3216C_I2C_NACK         BSP_I2C_ERROR
#define AP3216C_I2C_TIMEOUT      BSP_I2C_TIMEOUT

#else /* BSP_AP3216C_USE_SOFT_I2C (default) */
#include "bsp_soft_i2c.h"
#define AP3216C_I2C_BUS_ID       BSP_SOFT_I2C_BUS_1
#define ap3216c_i2c_init(id, sp) BSP_SoftI2C_Init((id), (BSP_SoftI2C_Speed_t)(sp))
#define ap3216c_i2c_mem_write(id, dev, ma, mas, d, l, to) \
    BSP_SoftI2C_MemWrite((id), (dev), (ma), (mas), (d), (l), (to))
#define ap3216c_i2c_mem_read(id, dev, ma, mas, d, l, to) \
    BSP_SoftI2C_MemRead((id), (dev), (ma), (mas), (d), (l), (to))
#define ap3216c_i2c_ready(id, dev, tr, to) \
    BSP_SoftI2C_IsDeviceReady((id), (dev), (tr), (to))
#define AP3216C_I2C_OK           BSP_SOFT_I2C_OK
#define AP3216C_I2C_NACK         BSP_SOFT_I2C_NACK
#define AP3216C_I2C_TIMEOUT      BSP_SOFT_I2C_TIMEOUT
#endif

/* ================================================================
 * 内部常量 (static const)
 *
 * 按功能分组: 寄存器地址 / 配置值与延时 / 量程换算表 / 总线速率。
 * ================================================================ */

static const uint32_t ap3216c_i2c_speed = 400000U;

/* ---- 寄存器地址 ---- */
static const struct {
    uint8_t sys_config;
    uint8_t int_status;
    uint8_t ir_data_low;
    uint8_t als_data_low;
    uint8_t ps_data_low;
    uint8_t als_config;
} ap3216c_regs = {
    .sys_config   = 0x00,
    .int_status   = 0x01,
    .ir_data_low  = 0x0A,
    .als_data_low = 0x0C,
    .ps_data_low  = 0x0E,
    .als_config   = 0x10,
};

/* ---- 配置值与延时 ---- */
static const struct {
    uint8_t  sw_reset;
    uint8_t  als_ps_active;
    uint32_t reset_delay_ms;
    uint32_t mode_set_delay_ms;
} ap3216c_cfg = {
    .sw_reset          = 0x04,
    .als_ps_active     = 0x03,
    .reset_delay_ms    = 100U,
    .mode_set_delay_ms = 100U,
};

/* ALS 量程 → lux/count 分辨率 (查表, 下标对应 BSP_AP3216C_AlsRange_t) */
static const float ap3216c_als_resolution[4] = { 0.35f, 0.0788f, 0.0197f, 0.0049f };

/* ================================================================
 * 模块级状态: 记录当前 ALS 量程
 * ================================================================ */

static BSP_AP3216C_AlsRange_t s_ap3216c_als_range = BSP_AP3216C_ALS_RANGE_20661;

/* ================================================================
 * 内部: 错误码映射
 * ================================================================ */

static BSP_AP3216C_Status_t ap3216c_map_i2c_err(uint32_t i2c_status)
{
    if (i2c_status == AP3216C_I2C_OK)      return BSP_AP3216C_OK;
    if (i2c_status == AP3216C_I2C_TIMEOUT) return BSP_AP3216C_ERR_TIMEOUT;
    if (i2c_status == AP3216C_I2C_NACK)    return BSP_AP3216C_ERR_NACK;
    return BSP_AP3216C_ERROR;
}

/* ================================================================
 * 内部: I2C 读写封装
 * ================================================================ */

static BSP_AP3216C_Status_t ap3216c_write_reg(uint8_t reg, uint8_t val)
{
    uint32_t ret = ap3216c_i2c_mem_write(AP3216C_I2C_BUS_ID,
                                         (uint8_t)BSP_AP3216C_DEV_ADDR,
                                         reg, 1, &val, 1,
                                         BSP_AP3216C_I2C_TIMEOUT_MS);
    return ap3216c_map_i2c_err(ret);
}

static BSP_AP3216C_Status_t ap3216c_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint32_t ret = ap3216c_i2c_mem_read(AP3216C_I2C_BUS_ID,
                                        (uint8_t)BSP_AP3216C_DEV_ADDR,
                                        reg, 1, buf, len,
                                        BSP_AP3216C_I2C_TIMEOUT_MS);
    return ap3216c_map_i2c_err(ret);
}

/* ================================================================
 * 内部: 单寄存器低字节+高字节读取 (对齐官方 read_low_and_high)
 * ================================================================ */

static BSP_AP3216C_Status_t ap3216c_read_two_regs(uint8_t reg_low, uint32_t *out)
{
    uint8_t low, high;

    BSP_AP3216C_Status_t ret = ap3216c_read_regs(reg_low, &low, 1);
    if (ret != BSP_AP3216C_OK) return ret;

    ret = ap3216c_read_regs(reg_low + 1, &high, 1);
    if (ret != BSP_AP3216C_OK) return ret;

    *out = (uint32_t)low | ((uint32_t)high << 8);
    return BSP_AP3216C_OK;
}

/* ================================================================
 * 公开 API: Init — 严格对齐官方 ap3216c_init()
 * ================================================================ */

BSP_AP3216C_Status_t BSP_AP3216C_Init(BSP_AP3216C_AlsRange_t range)
{
    BSP_AP3216C_Status_t ret;

    s_ap3216c_als_range = range;

    /* ---- I2C 总线初始化 (幂等) ---- */
    ap3216c_i2c_init(AP3216C_I2C_BUS_ID, ap3216c_i2c_speed);

    /* ---- 软件复位 ---- */
    ret = ap3216c_write_reg(ap3216c_regs.sys_config, ap3216c_cfg.sw_reset);
    if (ret != BSP_AP3216C_OK) return ret;

    BSP_SYSTICK_DelayMS(ap3216c_cfg.reset_delay_ms);

    /* ---- ALS 量程配置 (RMW, 对齐官方 ap3216c_set_param) ---- */
    if (range != BSP_AP3216C_ALS_RANGE_20661) {
        uint8_t val;
        ret = ap3216c_read_regs(ap3216c_regs.als_config, &val, 1);
        if (ret != BSP_AP3216C_OK) return ret;
        val &= 0xCF;
        val |= (uint8_t)(range << 4);
        ret = ap3216c_write_reg(ap3216c_regs.als_config, val);
        if (ret != BSP_AP3216C_OK) return ret;
    }

    /* ---- 设置 ALS + PS 同时工作模式 ---- */
    ret = ap3216c_write_reg(ap3216c_regs.sys_config, ap3216c_cfg.als_ps_active);
    if (ret != BSP_AP3216C_OK) return ret;

    BSP_SYSTICK_DelayMS(ap3216c_cfg.mode_set_delay_ms);

    return BSP_AP3216C_OK;
}

/* ================================================================
 * 公开 API: ReadAll — 严格对齐官方读取流程
 *   (IntStatus → ALS → PS → IR, 分次读取低/高字节)
 * ================================================================ */

BSP_AP3216C_Status_t BSP_AP3216C_ReadAll(BSP_AP3216C_Data_t *p_data)
{
    if (!p_data) return BSP_AP3216C_ERROR;

    BSP_AP3216C_Status_t ret;
    uint32_t raw;

    /* ---- 1. 读中断状态 (官方先读此寄存器) ---- */
    {
        uint8_t dummy = 0;
        (void)ap3216c_read_regs(ap3216c_regs.int_status, &dummy, 1);
    }

    /* ---- 2. ALS: 低字节 + 高字节 (对齐官方 read_low_and_high) ---- */
    ret = ap3216c_read_two_regs(ap3216c_regs.als_data_low, &raw);
    if (ret != BSP_AP3216C_OK) return ret;
    p_data->als_lux = (float)(raw & 0xFFFF) * ap3216c_als_resolution[s_ap3216c_als_range];

    /* ---- 3. PS: 低字节 + 高字节 ---- */
    ret = ap3216c_read_two_regs(ap3216c_regs.ps_data_low, &raw);
    if (ret != BSP_AP3216C_OK) return ret;

    /* IR 饱和检查 (对齐官方: bit6 or bit14) */
    p_data->ir_saturated = ((raw >> 6) & 0x01) || ((raw >> 14) & 0x01) ? 1 : 0;
    p_data->ps           = ((raw & 0x000F) + (((raw >> 8) & 0x3F) << 4)) & 0x03FF;
    p_data->approaching  = (raw & 0x8000) ? 1 : 0;

    /* ---- 4. IR: 低字节 + 高字节 ---- */
    ret = ap3216c_read_two_regs(ap3216c_regs.ir_data_low, &raw);
    if (ret != BSP_AP3216C_OK) return ret;
    p_data->ir = ((raw & 0x0003) + ((raw >> 8) & 0xFF)) & 0x03FF;

    return BSP_AP3216C_OK;
}

/* ================================================================
 * 公开 API: 单通道读取
 * ================================================================ */

BSP_AP3216C_Status_t BSP_AP3216C_ReadALS(float *p_lux)
{
    if (!p_lux) return BSP_AP3216C_ERROR;

    uint32_t raw;
    BSP_AP3216C_Status_t ret = ap3216c_read_two_regs(ap3216c_regs.als_data_low, &raw);
    if (ret != BSP_AP3216C_OK) return ret;

    *p_lux = (float)(raw & 0xFFFF) * ap3216c_als_resolution[s_ap3216c_als_range];
    return BSP_AP3216C_OK;
}

BSP_AP3216C_Status_t BSP_AP3216C_ReadPS(uint16_t *p_ps)
{
    if (!p_ps) return BSP_AP3216C_ERROR;

    uint32_t raw;
    BSP_AP3216C_Status_t ret = ap3216c_read_two_regs(ap3216c_regs.ps_data_low, &raw);
    if (ret != BSP_AP3216C_OK) return ret;

    *p_ps = ((raw & 0x000F) + (((raw >> 8) & 0x3F) << 4)) & 0x03FF;
    return BSP_AP3216C_OK;
}

BSP_AP3216C_Status_t BSP_AP3216C_ReadIR(uint16_t *p_ir)
{
    if (!p_ir) return BSP_AP3216C_ERROR;

    uint32_t raw;
    BSP_AP3216C_Status_t ret = ap3216c_read_two_regs(ap3216c_regs.ir_data_low, &raw);
    if (ret != BSP_AP3216C_OK) return ret;

    *p_ir = ((raw & 0x0003) + ((raw >> 8) & 0xFF)) & 0x03FF;
    return BSP_AP3216C_OK;
}
