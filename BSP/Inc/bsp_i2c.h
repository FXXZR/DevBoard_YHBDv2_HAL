/**
 * @file    bsp_i2c.h
 * @brief   BSP 硬件 I2C 驱动接口
 * @note    基于 STM32F1 HAL I2C 的阻塞式主机模式驱动。
 *          默认映射 I2C1: PB6=SCL (AF4 OD), PB7=SDA (AF4 OD)。
 *
 *          API 设计与 bsp_soft_i2c.h 保持对称，
 *          方便上层模块 (如 bsp_eeprom) 通过宏切换 I2C 实现。
 *
 *          STM32F1 硬件 I2C 已知存在 EV5/EV6/BUSY 锁死等硬件缺陷，
 *          生产环境建议优先使用 bsp_soft_i2c。
 *
 *          参考: ST RM0008 §26.4 I2C master mode
 */

#ifndef __BSP_I2C_H__
#define __BSP_I2C_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 总线编号
 * ================================================================ */

typedef enum {
    BSP_I2C_BUS_1 = 0,      /* I2C1: PB6=SCL, PB7=SDA (AF4, OD) */
    BSP_I2C_BUS_COUNT
} BSP_I2C_Bus_Id_t;

/* ================================================================
 * 状态码
 * ================================================================ */

typedef enum {
    BSP_I2C_OK      = 0,
    BSP_I2C_ERROR   = 1,
    BSP_I2C_TIMEOUT = 2,
    BSP_I2C_BUSY    = 3
} BSP_I2C_Status_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化 I2C 总线（GPIO + 时钟 + HAL 外设配置）
 * @param  id          总线编号
 * @param  clock_speed I2C 时钟频率 (Hz)，常用 100000 或 400000
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用。
 *         仅需调用一次。重复调用安全（幂等）。
 */
void BSP_I2C_Init(BSP_I2C_Bus_Id_t id, uint32_t clock_speed);

/**
 * @brief  向 I2C 从设备写入数据（Memory Write 协议）
 * @param  id            总线编号
 * @param  dev_addr      7-bit 从设备地址
 * @param  mem_addr      设备内部存储器地址
 * @param  mem_addr_size 存储器地址字节数 (1 或 2)
 * @param  data          待写入数据
 * @param  len           数据长度 (字节)
 * @param  timeout       超时 (ms)
 * @return BSP_I2C_OK (0) 成功
 */
BSP_I2C_Status_t BSP_I2C_MemWrite(BSP_I2C_Bus_Id_t id,
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
 * @return BSP_I2C_OK (0) 成功
 */
BSP_I2C_Status_t BSP_I2C_MemRead(BSP_I2C_Bus_Id_t id,
                                 uint8_t dev_addr, uint16_t mem_addr,
                                 uint8_t mem_addr_size,
                                 uint8_t *data, uint16_t len,
                                 uint32_t timeout);

/**
 * @brief  检测从设备是否就绪（发送地址并等待 ACK）
 * @param  id        总线编号
 * @param  dev_addr  7-bit 从设备地址
 * @param  trials    最大重试次数
 * @param  timeout   单次超时 (ms)
 * @return BSP_I2C_OK (0) 设备就绪
 */
BSP_I2C_Status_t BSP_I2C_IsDeviceReady(BSP_I2C_Bus_Id_t id,
                                       uint8_t dev_addr,
                                       uint32_t trials, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_I2C_H__ */
