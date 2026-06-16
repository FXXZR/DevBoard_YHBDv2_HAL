/**
 * @file    bsp_eeprom.h
 * @brief   BSP EEPROM 驱动接口 —— AT24C02 (256B, I2C)
 * @note    本模块不直接依赖 I2C 硬件/软件实现，而是通过宏
 *          BSP_EEPROM_USE_SOFT_I2C 选择底层 I2C 驱动。
 *
 *          默认使用软件 I2C (bsp_soft_i2c)，因为 STM32F1
 *          硬件 I2C 存在 BUSY 锁死等已知缺陷。
 *
 *          切换为硬件 I2C: 注释 #define BSP_EEPROM_USE_SOFT_I2C
 *          并取消注释 #define BSP_EEPROM_USE_HW_I2C。
 *
 *          AT24C02 规格:
 *            容量:     2Kbit = 256 Bytes
 *            页大小:    8 Bytes
 *            地址:      7-bit 0x50 (A2=A1=A0=0)
 *            I2C 速率:  100KHz (Standard) / 400KHz (Fast)
 *            写周期:    ≤5ms (自定时)
 */

#ifndef __BSP_EEPROM_H__
#define __BSP_EEPROM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * I2C 后端选择 (二选一，条件编译 — 唯一允许的宏场景)
 * ================================================================ */

#define BSP_EEPROM_USE_SOFT_I2C     /* 默认: 软件 I2C */
// #define BSP_EEPROM_USE_HW_I2C    /* 备选: 硬件 I2C */

/* ================================================================
 * AT24C02 配置常量 (对外暴露，使用匿名 enum)
 * ================================================================ */

enum {
    BSP_EEPROM_DEV_ADDR     = 0x50, /* 7-bit I2C 地址 (A2=A1=A0=0) */
    BSP_EEPROM_PAGE_SIZE    = 8,    /* 页大小 (AT24C01/02)         */
    BSP_EEPROM_CAPACITY     = 256,  /* 总容量 (Bytes)              */
    BSP_EEPROM_I2C_TIMEOUT_MS = 100 /* I2C 操作超时 (ms)           */
};

/* ================================================================
 * 状态码
 * ================================================================ */

typedef enum {
    BSP_EEPROM_OK           = 0,
    BSP_EEPROM_ERROR        = 1,
    BSP_EEPROM_ERR_NACK     = 2,  /* 从设备无应答 (保留 I2C 层信息) */
    BSP_EEPROM_ERR_TIMEOUT  = 3   /* 总线超时                       */
} BSP_EEPROM_Status_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化 EEPROM（含底层 I2C 总线初始化）
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用。
 *         仅需调用一次。
 */
void BSP_EEPROM_Init(void);

/**
 * @brief  从 EEPROM 读取数据
 * @param  buf   接收缓冲区
 * @param  addr  EEPROM 内部起始地址 (0 ~ 255)
 * @param  len   读取长度 (字节)，addr+len 不可超过 256
 * @return BSP_EEPROM_OK (0) 成功，其他值携带 I2C 层原始错误信息
 */
BSP_EEPROM_Status_t BSP_EEPROM_Read(uint8_t *buf, uint8_t addr, uint16_t len);

/**
 * @brief  向 EEPROM 写入数据（自动处理页边界跨页）
 * @param  buf   数据缓冲区
 * @param  addr  EEPROM 内部起始地址 (0 ~ 255)
 * @param  len   写入长度 (字节)，addr+len 不可超过 256
 * @note   自动处理页边界拆分 + 写周期等待。
 *         跨页时会自动等待上一页写完成后再写下一页。
 */
void BSP_EEPROM_Write(const uint8_t *buf, uint8_t addr, uint16_t len);

/**
 * @brief  检测 EEPROM 是否就绪（可用于写后确认）
 * @return BSP_EEPROM_OK (0) 就绪
 */
BSP_EEPROM_Status_t BSP_EEPROM_IsReady(void);

/**
 * @brief  擦除整个 EEPROM（全写 0xFF）
 * @note   按页擦除，每页写满 0xFF。耗时约 32 页 × 5ms = ~160ms。
 */
void BSP_EEPROM_Erase(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_EEPROM_H__ */
