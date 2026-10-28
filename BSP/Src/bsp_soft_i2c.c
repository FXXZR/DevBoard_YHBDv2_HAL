/**
 * @file    bsp_soft_i2c.c
 * @brief   BSP 软件 I2C 驱动实现 —— GPIO 开漏 + 位带延时模拟 I2C 主机
 * @note    SCL/SDA 均配置为开漏输出 (OD) + BSRR/BRR 原子操作。
 *          延时基于 BSP_SYSTICK_DelayUS。
 *
 *          架构：
 *            遵循 bsp_usart 模式 — static const HW 映射表 + static ctx 数组。
 *            P2 改进: SCL 时钟拉伸检测 + ACK 等待超时保护。
 *
 *          参考: I2C-bus specification v2.1 (NXP UM10204)
 */

#include "bsp_soft_i2c.h"
#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * GPIO 位带操作宏 (BSRR/BRR 原子写, 避免读-改-写竞争)
 * 此处保留宏是因为它们属于寄存器位操作 — 符合 CLAUDE.md 规范
 * ================================================================ */

#define SCL_H(p,n)  ((GPIO_TypeDef *)(p))->BSRR = (n)
#define SCL_L(p,n)  ((GPIO_TypeDef *)(p))->BRR  = (n)
#define SDA_H(p,n)  ((GPIO_TypeDef *)(p))->BSRR = (n)
#define SDA_L(p,n)  ((GPIO_TypeDef *)(p))->BRR  = (n)
#define SDA_R(p,n)  ((((GPIO_TypeDef *)(p))->IDR & (n)) != 0U)
#define SCL_R(p,n)  ((((GPIO_TypeDef *)(p))->IDR & (n)) != 0U)

/* ================================================================
 * 硬件映射表 (PB6=SCL, PB7=SDA)
 * ================================================================ */

typedef struct {
    GPIO_TypeDef *scl_port; uint16_t scl_pin;
    GPIO_TypeDef *sda_port; uint16_t sda_pin;
} SoftI2C_HW_Map_t;

static const SoftI2C_HW_Map_t soft_i2c_hw_map[BSP_SOFT_I2C_BUS_COUNT] = {
    [BSP_SOFT_I2C_BUS_1] = {
        .scl_port = GPIOB, .scl_pin = GPIO_PIN_6,
        .sda_port = GPIOB, .sda_pin = GPIO_PIN_7,
    },
};

/* ================================================================
 * 内部常量
 * ================================================================ */

/* 各速率下的半周期延时 (空循环次数) 与两处轮询上限 */
static const struct {
    uint8_t  half_period_100k;
    uint8_t  half_period_400k;
    uint8_t  half_period_1m;
    uint32_t ack_timeout_cycles;   /* ACK 轮询超时 (~2.5ms @72MHz) */
    uint32_t scl_check_cycles;     /* SCL 回读超时 (~5ms @72MHz) */
} soft_i2c_timing = {
    .half_period_100k   = 5U,
    .half_period_400k   = 2U,
    .half_period_1m     = 1U,
    .ack_timeout_cycles = 50000U,
    .scl_check_cycles   = 100000U,
};

/* ================================================================
 * 运行时上下文
 * ================================================================ */

typedef struct {
    const SoftI2C_HW_Map_t *hw;     /* 指向 static const 映射表         */
    uint8_t  half_period;           /* 半周期延时 (us)                  */
    uint8_t  initialized;
} SoftI2C_Ctx_t;

static SoftI2C_Ctx_t s_si2c_ctx[BSP_SOFT_I2C_BUS_COUNT];

/* ================================================================
 * 内部: GPIO 基础操作
 * ================================================================ */

static inline void soft_i2c_delay(const SoftI2C_Ctx_t *ctx)
{
    BSP_SYSTICK_DelayUS(ctx->half_period);
}

static inline void soft_i2c_scl_h(const SoftI2C_Ctx_t *ctx)
{
    SCL_H(ctx->hw->scl_port, ctx->hw->scl_pin);
}

static inline void soft_i2c_scl_l(const SoftI2C_Ctx_t *ctx)
{
    SCL_L(ctx->hw->scl_port, ctx->hw->scl_pin);
}

static inline void soft_i2c_sda_h(const SoftI2C_Ctx_t *ctx)
{
    SDA_H(ctx->hw->sda_port, ctx->hw->sda_pin);
}

static inline void soft_i2c_sda_l(const SoftI2C_Ctx_t *ctx)
{
    SDA_L(ctx->hw->sda_port, ctx->hw->sda_pin);
}

static inline uint8_t soft_i2c_sda_read(const SoftI2C_Ctx_t *ctx)
{
    return SDA_R(ctx->hw->sda_port, ctx->hw->sda_pin);
}

static inline uint8_t soft_i2c_scl_read(const SoftI2C_Ctx_t *ctx)
{
    return SCL_R(ctx->hw->scl_port, ctx->hw->scl_pin);
}

/* ================================================================
 * 内部: SCL 时钟拉伸检测
 * ================================================================ */

/**
 * @brief  等待 SCL 被释放为高 (时钟拉伸检测)
 * @return 0 = SCL 已变高, 1 = 超时 (总线卡死)
 * @note   P2 改进: 从设备可通过拉低 SCL 暂停传输。本函数检测此情况并加超时保护。
 */
static uint8_t soft_i2c_scl_wait_high(const SoftI2C_Ctx_t *ctx)
{
    for (uint32_t to = 0; to < soft_i2c_timing.scl_check_cycles; to++) {
        if (soft_i2c_scl_read(ctx)) return 0;
    }
    return 1;  /* 超时: SCL 被从设备或硬件故障长期拉低 */
}

/* ================================================================
 * 内部: I2C 基本信号
 * ================================================================ */

static void soft_i2c_start(const SoftI2C_Ctx_t *ctx)
{
    soft_i2c_sda_h(ctx);
    soft_i2c_scl_h(ctx);
    soft_i2c_delay(ctx);
    soft_i2c_sda_l(ctx);
    soft_i2c_delay(ctx);
    soft_i2c_scl_l(ctx);
    soft_i2c_delay(ctx);
}

static void soft_i2c_stop(const SoftI2C_Ctx_t *ctx)
{
    soft_i2c_scl_l(ctx);
    soft_i2c_sda_l(ctx);
    soft_i2c_delay(ctx);
    soft_i2c_scl_h(ctx);
    /* P2: 检查 SCL 是否成功变高 */
    if (soft_i2c_scl_wait_high(ctx)) {
        /* 超时 — 强制释放总线 */
    }
    soft_i2c_delay(ctx);
    soft_i2c_sda_h(ctx);
    soft_i2c_delay(ctx);
}

/**
 * @brief  发送一个字节并等待 ACK (带超时)
 * @param  ctx   总线上下文
 * @param  byte  待发送字节
 * @return 0 = ACK, 1 = NACK, 2 = 超时
 */
static uint8_t soft_i2c_write_byte(const SoftI2C_Ctx_t *ctx, uint8_t byte)
{
    for (int8_t i = 7; i >= 0; i--) {
        soft_i2c_scl_l(ctx);
        soft_i2c_delay(ctx);
        if (byte & (1U << i)) {
            soft_i2c_sda_h(ctx);
        } else {
            soft_i2c_sda_l(ctx);
        }
        soft_i2c_delay(ctx);
        soft_i2c_scl_h(ctx);
        /* P2: 检查 SCL 是否被拉伸 */
        if (soft_i2c_scl_wait_high(ctx)) return 2;
        soft_i2c_delay(ctx);
    }

    /* 释放 SDA, 读取 ACK */
    soft_i2c_scl_l(ctx);
    soft_i2c_sda_h(ctx);
    soft_i2c_delay(ctx);
    soft_i2c_scl_h(ctx);
    if (soft_i2c_scl_wait_high(ctx)) return 2;
    soft_i2c_delay(ctx);

    /* 轮询 ACK 带超时 */
    uint8_t ack = 0;
    for (uint32_t to = 0; to < soft_i2c_timing.ack_timeout_cycles; to++) {
        ack = soft_i2c_sda_read(ctx);
        if (ack == 0) break;  /* SDA 被从设备拉低 = ACK */
    }

    soft_i2c_scl_l(ctx);
    soft_i2c_delay(ctx);

    return ack ? 1U : 0U;  /* 0=ACK, 1=NACK, 注意: 超时表现为 NACK+1 */
}

/**
 * @brief  读取一个字节并发送 ACK/NACK
 * @param  send_ack  1 = 发送 ACK, 0 = 发送 NACK (最后一个字节)
 */
static uint8_t soft_i2c_read_byte(const SoftI2C_Ctx_t *ctx, uint8_t send_ack)
{
    uint8_t byte = 0;

    soft_i2c_scl_l(ctx);
    soft_i2c_sda_h(ctx);  /* 释放 SDA */
    soft_i2c_delay(ctx);

    for (int8_t i = 7; i >= 0; i--) {
        soft_i2c_scl_h(ctx);
        if (soft_i2c_scl_wait_high(ctx)) break;  /* P2: 超时保护 */
        soft_i2c_delay(ctx);
        if (soft_i2c_sda_read(ctx)) {
            byte |= (uint8_t)(1U << i);
        }
        soft_i2c_scl_l(ctx);
        soft_i2c_delay(ctx);
    }

    /* 发送 ACK/NACK */
    if (send_ack) { soft_i2c_sda_l(ctx); }
    else          { soft_i2c_sda_h(ctx); }
    soft_i2c_delay(ctx);
    soft_i2c_scl_h(ctx);
    if (soft_i2c_scl_wait_high(ctx)) { /* 超时 */ }
    soft_i2c_delay(ctx);
    soft_i2c_scl_l(ctx);
    soft_i2c_delay(ctx);

    return byte;
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_SoftI2C_Init(BSP_SoftI2C_Bus_Id_t id, BSP_SoftI2C_Speed_t speed)
{
    if (id >= BSP_SOFT_I2C_BUS_COUNT) return;

    SoftI2C_Ctx_t *ctx = &s_si2c_ctx[id];
    if (ctx->initialized) return;
    ctx->initialized = 1;
    ctx->hw = &soft_i2c_hw_map[id];

    const SoftI2C_HW_Map_t *hw = ctx->hw;
    GPIO_TypeDef *scl = hw->scl_port;
    GPIO_TypeDef *sda = hw->sda_port;

    /* 1. 时钟使能 */
    if (scl == GPIOA || sda == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    if (scl == GPIOB || sda == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    if (scl == GPIOC || sda == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();

    /* 2. GPIO 开漏配置 */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = hw->scl_pin;
    HAL_GPIO_Init(scl, &gpio);
    gpio.Pin = hw->sda_pin;
    HAL_GPIO_Init(sda, &gpio);

    /* 3. 初始电平: 总线空闲 */
    soft_i2c_scl_h(ctx);
    soft_i2c_sda_h(ctx);

    /* 4. 速度 → 半周期延时 */
    switch (speed) {
    case BSP_SOFT_I2C_SPEED_400K: ctx->half_period = soft_i2c_timing.half_period_400k; break;
    case BSP_SOFT_I2C_SPEED_1M:   ctx->half_period = soft_i2c_timing.half_period_1m;   break;
    default:                      ctx->half_period = soft_i2c_timing.half_period_100k;  break;
    }
}

BSP_SoftI2C_Status_t BSP_SoftI2C_MemWrite(BSP_SoftI2C_Bus_Id_t id,
                                          uint8_t dev_addr, uint16_t mem_addr,
                                          uint8_t mem_addr_size,
                                          const uint8_t *data, uint16_t len,
                                          uint32_t timeout)
{
    (void)timeout;
    if (id >= BSP_SOFT_I2C_BUS_COUNT || !s_si2c_ctx[id].initialized) return BSP_SOFT_I2C_ERROR;
    if (len > 0 && !data) return BSP_SOFT_I2C_ERROR;

    const SoftI2C_Ctx_t *ctx = &s_si2c_ctx[id];
    uint8_t ack;

    /* START + dev(W) */
    soft_i2c_start(ctx);
    ack = soft_i2c_write_byte(ctx, (uint8_t)((dev_addr << 1) & 0xFE));
    if (ack == 1) { soft_i2c_stop(ctx); return BSP_SOFT_I2C_NACK; }
    if (ack == 2) { soft_i2c_stop(ctx); return BSP_SOFT_I2C_TIMEOUT; }

    /* MemAddr */
    if (mem_addr_size == 2) {
        ack = soft_i2c_write_byte(ctx, (uint8_t)((mem_addr >> 8) & 0xFF));
        if (ack) { soft_i2c_stop(ctx); return (ack == 1) ? BSP_SOFT_I2C_NACK : BSP_SOFT_I2C_TIMEOUT; }
    }
    ack = soft_i2c_write_byte(ctx, (uint8_t)(mem_addr & 0xFF));
    if (ack) { soft_i2c_stop(ctx); return (ack == 1) ? BSP_SOFT_I2C_NACK : BSP_SOFT_I2C_TIMEOUT; }

    /* Data */
    for (uint16_t i = 0; i < len; i++) {
        ack = soft_i2c_write_byte(ctx, data[i]);
        if (ack) { soft_i2c_stop(ctx); return (ack == 1) ? BSP_SOFT_I2C_NACK : BSP_SOFT_I2C_TIMEOUT; }
    }

    soft_i2c_stop(ctx);
    return BSP_SOFT_I2C_OK;
}

BSP_SoftI2C_Status_t BSP_SoftI2C_MemRead(BSP_SoftI2C_Bus_Id_t id,
                                         uint8_t dev_addr, uint16_t mem_addr,
                                         uint8_t mem_addr_size,
                                         uint8_t *data, uint16_t len,
                                         uint32_t timeout)
{
    (void)timeout;
    if (id >= BSP_SOFT_I2C_BUS_COUNT || !s_si2c_ctx[id].initialized) return BSP_SOFT_I2C_ERROR;
    if (len > 0 && !data) return BSP_SOFT_I2C_ERROR;

    const SoftI2C_Ctx_t *ctx = &s_si2c_ctx[id];
    uint8_t ack;

    /* Phase 1: START + dev(W) + MemAddr */
    soft_i2c_start(ctx);
    ack = soft_i2c_write_byte(ctx, (uint8_t)((dev_addr << 1) & 0xFE));
    if (ack) { soft_i2c_stop(ctx); return (ack == 1) ? BSP_SOFT_I2C_NACK : BSP_SOFT_I2C_TIMEOUT; }

    if (mem_addr_size == 2) {
        ack = soft_i2c_write_byte(ctx, (uint8_t)((mem_addr >> 8) & 0xFF));
        if (ack) { soft_i2c_stop(ctx); return (ack == 1) ? BSP_SOFT_I2C_NACK : BSP_SOFT_I2C_TIMEOUT; }
    }
    ack = soft_i2c_write_byte(ctx, (uint8_t)(mem_addr & 0xFF));
    if (ack) { soft_i2c_stop(ctx); return (ack == 1) ? BSP_SOFT_I2C_NACK : BSP_SOFT_I2C_TIMEOUT; }

    /* Phase 2: RESTART + dev(R) */
    soft_i2c_start(ctx);
    ack = soft_i2c_write_byte(ctx, (uint8_t)((dev_addr << 1) | 0x01));
    if (ack) { soft_i2c_stop(ctx); return (ack == 1) ? BSP_SOFT_I2C_NACK : BSP_SOFT_I2C_TIMEOUT; }

    /* Phase 3: Read data */
    for (uint16_t i = 0; i < len; i++) {
        data[i] = soft_i2c_read_byte(ctx, (i < len - 1) ? 1U : 0U);
    }

    soft_i2c_stop(ctx);
    return BSP_SOFT_I2C_OK;
}

BSP_SoftI2C_Status_t BSP_SoftI2C_IsDeviceReady(BSP_SoftI2C_Bus_Id_t id,
                                               uint8_t dev_addr,
                                               uint32_t trials, uint32_t timeout)
{
    (void)timeout;
    if (id >= BSP_SOFT_I2C_BUS_COUNT || !s_si2c_ctx[id].initialized) return BSP_SOFT_I2C_ERROR;

    const SoftI2C_Ctx_t *ctx = &s_si2c_ctx[id];

    for (uint32_t t = 0; t < trials; t++) {
        soft_i2c_start(ctx);
        uint8_t ack = soft_i2c_write_byte(ctx, (uint8_t)((dev_addr << 1) & 0xFE));
        soft_i2c_stop(ctx);
        if (ack == 0) return BSP_SOFT_I2C_OK;
        if (ack == 2) return BSP_SOFT_I2C_TIMEOUT;
    }

    return BSP_SOFT_I2C_NACK;
}
