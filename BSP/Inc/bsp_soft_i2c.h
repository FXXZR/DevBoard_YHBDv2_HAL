/**
 * @file    bsp_soft_i2c.h
 * @brief   BSP 软件 I2C (GPIO 位带) 驱动接口
 * @note    基于 GPIO 开漏 + 延时实现 I2C 主机协议，不依赖硬件 I2C 外设。
 *          适用于 STM32F1 等硬件 I2C 存在缺陷的 MCU，或任意 GPIO 引脚模拟 I2C。
 *
 *          API 设计与 bsp_i2c.h 保持对称，
 *          方便上层模块 (如 bsp_eeprom) 通过宏切换 I2C 实现。
 *
 *          参考: I2C-bus specification v2.1 (NXP UM10204)
 */

#ifndef __BSP_SOFT_I2C_H__
#define __BSP_SOFT_I2C_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 总线编号
 * ================================================================ */

typedef enum {
    BSP_SOFT_I2C_BUS_1 = 0, /* PB6=SCL, PB7=SDA */
    BSP_SOFT_I2C_BUS_COUNT
} BSP_SoftI2C_Bus_Id_t;

/* ================================================================
 * 速度选项
 * ================================================================ */

typedef enum {
    BSP_SOFT_I2C_SPEED_100K = 100000,
    BSP_SOFT_I2C_SPEED_400K = 400000,
    BSP_SOFT_I2C_SPEED_1M   = 1000000
} BSP_SoftI2C_Speed_t;

/* ================================================================
 * 状态码
 * ================================================================ */

typedef enum {
    BSP_SOFT_I2C_OK      = 0,
    BSP_SOFT_I2C_NACK    = 1,  /* 从设备未应答 */
    BSP_SOFT_I2C_TIMEOUT = 2,  /* 等待超时 (SCL 被从设备拉低/卡住) */
    BSP_SOFT_I2C_ERROR   = 3   /* 参数错误或其他 */
} BSP_SoftI2C_Status_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化软件 I2C 总线（GPIO 开漏 + 延时参数计算）
 * @param  id    总线编号
 * @param  speed I2C 时钟频率 (BSP_SOFT_I2C_SPEED_100K / 400K / 1M)
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用。
 *         SCL/SDA 均配置为开漏输出 (OD)，依赖外部上拉电阻。
 *         仅需调用一次。重复调用安全（幂等）。
 */
void BSP_SoftI2C_Init(BSP_SoftI2C_Bus_Id_t id, BSP_SoftI2C_Speed_t speed);

/**
 * @brief  向 I2C 从设备写入数据（Memory Write 协议）
 * @param  id            总线编号
 * @param  dev_addr      7-bit 从设备地址
 * @param  mem_addr      设备内部存储器地址
 * @param  mem_addr_size 存储器地址字节数 (1 或 2)
 * @param  data          待写入数据
 * @param  len           数据长度 (字节)
 * @param  timeout       超时 (ms)
 * @return BSP_SOFT_I2C_OK (0) 成功
 *
 * @note   总线链路: START → dev(W) → [mem_addr] → [data...] → STOP
 */
BSP_SoftI2C_Status_t BSP_SoftI2C_MemWrite(BSP_SoftI2C_Bus_Id_t id,
                                          uint8_t dev_addr, uint16_t mem_addr,
                                          uint8_t mem_addr_size,
                                          const uint8_t *data, uint16_t len,
                                          uint32_t timeout);

/**
 * @brief  从 I2C 从设备读取数据（Memory Read 协议）
 * @param  id            总线编号
 * @param  dev_addr      7-bit 从设备地址
 * @param  mem_addr      设备内部存储器地址
 * @param  mem_addr_size 存储器地址字节数 (1 或 2)
 * @param  data          接收缓冲区
 * @param  len           期望读取长度 (字节)
 * @param  timeout       超时 (ms)
 * @return BSP_SOFT_I2C_OK (0) 成功
 *
 * @note   总线链路: START → dev(W) → [mem_addr] → RESTART → dev(R) → [data...] → STOP
 *         最后一个字节后发送 NACK。
 */
BSP_SoftI2C_Status_t BSP_SoftI2C_MemRead(BSP_SoftI2C_Bus_Id_t id,
                                         uint8_t dev_addr, uint16_t mem_addr,
                                         uint8_t mem_addr_size,
                                         uint8_t *data, uint16_t len,
                                         uint32_t timeout);

/**
 * @brief  检测从设备是否就绪（发送地址并等待 ACK）
 * @param  id       总线编号
 * @param  dev_addr 7-bit 从设备地址
 * @param  trials   最大重试次数
 * @param  timeout  单次超时 (ms)
 * @return BSP_SOFT_I2C_OK (0) 设备 ACK 应答
 */
BSP_SoftI2C_Status_t BSP_SoftI2C_IsDeviceReady(BSP_SoftI2C_Bus_Id_t id,
                                               uint8_t dev_addr,
                                               uint32_t trials, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SOFT_I2C_H__ */
