/**
 * @file    bsp_eeprom.c
 * @brief   BSP EEPROM 驱动实现 —— AT24C02 通过 I2C (硬件/软件可选)
 * @note    通过条件编译选择底层 I2C 后端:
 *            BSP_EEPROM_USE_SOFT_I2C → bsp_soft_i2c (默认)
 *            BSP_EEPROM_USE_HW_I2C   → bsp_i2c
 *          I2C 速度不通过宏定义，而是内部 static const 常量。
 *
 *          P2 改进: EEPROM 层保留并传递 I2C 层的详细错误码 (NACK/TIMEOUT/ERROR)。
 *
 *          参考: AT24C02 Datasheet (Atmel 0180Z)
 */

#include "bsp_eeprom.h"

/* ---- 底层 I2C 后端选择 (条件编译 — 唯一合理的宏场景) ---- */
#if defined(BSP_EEPROM_USE_HW_I2C)
#include "bsp_i2c.h"
#define EEPROM_I2C_BUS_ID       BSP_I2C_BUS_1
#define eeprom_i2c_init(id, sp) BSP_I2C_Init((id), (sp))
#define eeprom_i2c_mem_write(id, dev, ma, mas, d, l, to) \
    BSP_I2C_MemWrite((id), (dev), (ma), (mas), (d), (l), (to))
#define eeprom_i2c_mem_read(id, dev, ma, mas, d, l, to) \
    BSP_I2C_MemRead((id), (dev), (ma), (mas), (d), (l), (to))
#define eeprom_i2c_ready(id, dev, tr, to) \
    BSP_I2C_IsDeviceReady((id), (dev), (tr), (to))
#define EEPROM_I2C_OK           BSP_I2C_OK
#define EEPROM_I2C_NACK         BSP_I2C_ERROR   /* HW I2C 不区分 NACK */
#define EEPROM_I2C_TIMEOUT      BSP_I2C_TIMEOUT

#else /* BSP_EEPROM_USE_SOFT_I2C (default) */
#include "bsp_soft_i2c.h"
#define EEPROM_I2C_BUS_ID       BSP_SOFT_I2C_BUS_1
#define eeprom_i2c_init(id, sp) BSP_SoftI2C_Init((id), (BSP_SoftI2C_Speed_t)(sp))
#define eeprom_i2c_mem_write(id, dev, ma, mas, d, l, to) \
    BSP_SoftI2C_MemWrite((id), (dev), (ma), (mas), (d), (l), (to))
#define eeprom_i2c_mem_read(id, dev, ma, mas, d, l, to) \
    BSP_SoftI2C_MemRead((id), (dev), (ma), (mas), (d), (l), (to))
#define eeprom_i2c_ready(id, dev, tr, to) \
    BSP_SoftI2C_IsDeviceReady((id), (dev), (tr), (to))
#define EEPROM_I2C_OK           BSP_SOFT_I2C_OK
#define EEPROM_I2C_NACK         BSP_SOFT_I2C_NACK
#define EEPROM_I2C_TIMEOUT      BSP_SOFT_I2C_TIMEOUT
#endif

/* ================================================================
 * 内部常量
 * ================================================================ */

static const uint32_t eeprom_i2c_speed = 400000U;   /* 400KHz (shared bus with MPU6050/AP3216C) */

/* 写周期等待: 轮询次数 × 每次空循环 ~200us @72MHz -O0 */
static const struct {
    uint32_t write_wait_polls;   /* 写周期轮询次数 */
    uint32_t poll_loop_cycles;   /* 轮询间隔 */
} eeprom_write_timing = {
    .write_wait_polls = 50U,
    .poll_loop_cycles = 2000U,
};

/* ================================================================
 * 内部: 错误码映射 — 将 I2C 层错误码映射为 EEPROM 层错误码
 * ================================================================ */

/**
 * @brief  将 I2C 层状态码映射为 EEPROM 层状态码 (保留 NACK/TIMEOUT 信息)
 */
static BSP_EEPROM_Status_t eeprom_map_i2c_err(uint32_t i2c_status)
{
    if (i2c_status == EEPROM_I2C_OK)      return BSP_EEPROM_OK;
    if (i2c_status == EEPROM_I2C_TIMEOUT) return BSP_EEPROM_ERR_TIMEOUT;
    if (i2c_status == EEPROM_I2C_NACK)    return BSP_EEPROM_ERR_NACK;
    return BSP_EEPROM_ERROR;
}

/* ================================================================
 * 内部: 等待 EEPROM 写周期完成 (轮询设备地址 ACK)
 * ================================================================ */

static void eeprom_wait_write_done(void)
{
    for (uint32_t i = 0; i < eeprom_write_timing.write_wait_polls; i++) {
        uint32_t ret = eeprom_i2c_ready(EEPROM_I2C_BUS_ID,
                                        (uint8_t)BSP_EEPROM_DEV_ADDR,
                                        1, 2);
        if (ret == EEPROM_I2C_OK) return;

        /* 轮询间隔 (~200us @72MHz -O0) */
        for (volatile uint32_t d = 0; d < eeprom_write_timing.poll_loop_cycles; d++) {
            /* busy wait */
        }
    }
}

/* ================================================================
 * 内部: 单页写入 (不处理边界, 调用者保证不跨页)
 * ================================================================ */

static BSP_EEPROM_Status_t eeprom_page_write(const uint8_t *buf, uint8_t addr, uint8_t len)
{
    if (len == 0) return BSP_EEPROM_OK;

    uint32_t ret = eeprom_i2c_mem_write(EEPROM_I2C_BUS_ID,
                                        (uint8_t)BSP_EEPROM_DEV_ADDR,
                                        addr, 1,
                                        buf, len,
                                        BSP_EEPROM_I2C_TIMEOUT_MS);
    if (ret != EEPROM_I2C_OK) {
        return eeprom_map_i2c_err(ret);
    }

    eeprom_wait_write_done();
    return BSP_EEPROM_OK;
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_EEPROM_Init(void)
{
    eeprom_i2c_init(EEPROM_I2C_BUS_ID, eeprom_i2c_speed);
}

BSP_EEPROM_Status_t BSP_EEPROM_Read(uint8_t *buf, uint8_t addr, uint16_t len)
{
    if (!buf || len == 0) return BSP_EEPROM_ERROR;
    if ((uint16_t)addr + len > BSP_EEPROM_CAPACITY) return BSP_EEPROM_ERROR;

    uint32_t ret = eeprom_i2c_mem_read(EEPROM_I2C_BUS_ID,
                                       (uint8_t)BSP_EEPROM_DEV_ADDR,
                                       addr, 1,
                                       buf, len,
                                       BSP_EEPROM_I2C_TIMEOUT_MS);
    return eeprom_map_i2c_err(ret);
}

void BSP_EEPROM_Write(const uint8_t *buf, uint8_t addr, uint16_t len)
{
    if (!buf || len == 0) return;
    if ((uint16_t)addr + len > BSP_EEPROM_CAPACITY) return;

    uint8_t  page_remain = (uint8_t)(BSP_EEPROM_PAGE_SIZE - (addr % BSP_EEPROM_PAGE_SIZE));
    uint16_t remaining   = len;

    if (remaining <= page_remain) {
        eeprom_page_write(buf, addr, (uint8_t)remaining);
        return;
    }

    if (page_remain > 0) {
        eeprom_page_write(buf, addr, page_remain);
        buf       += page_remain;
        addr      += (uint8_t)page_remain;
        remaining -= page_remain;
    }

    while (remaining >= BSP_EEPROM_PAGE_SIZE) {
        eeprom_page_write(buf, addr, BSP_EEPROM_PAGE_SIZE);
        buf       += BSP_EEPROM_PAGE_SIZE;
        addr      += (uint8_t)BSP_EEPROM_PAGE_SIZE;
        remaining -= BSP_EEPROM_PAGE_SIZE;
    }

    if (remaining > 0) {
        eeprom_page_write(buf, addr, (uint8_t)remaining);
    }
}

BSP_EEPROM_Status_t BSP_EEPROM_IsReady(void)
{
    uint32_t ret = eeprom_i2c_ready(EEPROM_I2C_BUS_ID,
                                    (uint8_t)BSP_EEPROM_DEV_ADDR,
                                    1, 2);
    return eeprom_map_i2c_err(ret);
}

void BSP_EEPROM_Erase(void)
{
    static const uint8_t erase_buf[BSP_EEPROM_PAGE_SIZE] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

    for (uint16_t page = 0; page < BSP_EEPROM_CAPACITY; page += BSP_EEPROM_PAGE_SIZE) {
        eeprom_page_write(erase_buf, (uint8_t)page, BSP_EEPROM_PAGE_SIZE);
    }
}
