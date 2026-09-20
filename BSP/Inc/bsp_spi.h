/**
 * @file    bsp_spi.h
 * @brief   BSP 硬件 SPI 驱动接口
 * @note    基于 STM32F1 HAL SPI 的主机模式驱动。
 *          默认映射 SPI1: PA5=SCK, PA6=MISO, PA7=MOSI。
 *          CS (NSS) 由上层模块软件管理，本模块不控制。
 *
 *          仅提供纯 SPI 字节传输能力，不绑定任何具体从设备。
 *          FLASH 等设备驱动由 bsp_spi_flash 等上层模块实现。
 *
 *          参考: ST RM0008 §25.3 SPI master mode
 */

#ifndef __BSP_SPI_H__
#define __BSP_SPI_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 总线编号
 * ================================================================ */

typedef enum {
    BSP_SPI_BUS_1 = 0,      /* SPI1: PA5=SCK, PA6=MISO, PA7=MOSI  (APB2, 72MHz) */
    BSP_SPI_BUS_2,          /* SPI2: PB13=SCK, PB14=MISO, PB15=MOSI (APB1, 36MHz) */
    BSP_SPI_BUS_COUNT
} BSP_SPI_Bus_Id_t;

/* ================================================================
 * 波特率预分频 (对应 HAL SPI_BAUDRATEPRESCALER_xxx)
 *
 * 实际波特率 = 该总线所在 APB 的时钟 / 分频值, 两条总线差一倍:
 *     SPI1 (APB2 = 72MHz):  DIV_2 = 36MHz,  DIV_4 = 18MHz,  DIV_8 = 9MHz ...
 *     SPI2 (APB1 = 36MHz):  DIV_2 = 18MHz,  DIV_4 =  9MHz,  DIV_8 = 4.5MHz ...
 * 从 SPI1 挪到 SPI2 时, 同一个枚举值实际速度会减半 —— 传参前先确认目标总线.
 * ================================================================ */

typedef enum {
    BSP_SPI_BAUD_DIV_2   = 0,  /* SPI1: 36 MHz   | SPI2: 18 MHz */
    BSP_SPI_BAUD_DIV_4   = 1,  /* SPI1: 18 MHz   | SPI2:  9 MHz */
    BSP_SPI_BAUD_DIV_8   = 2,  /* SPI1:  9 MHz   | SPI2: 4.5 MHz */
    BSP_SPI_BAUD_DIV_16  = 3,  /* SPI1:  4.5 MHz | SPI2: 2.25 MHz */
    BSP_SPI_BAUD_DIV_32  = 4,  /* SPI1:  2.25MHz | SPI2: 1.125 MHz */
    BSP_SPI_BAUD_DIV_64  = 5,  /* SPI1:  1.125MHz| SPI2: ~562 KHz */
    BSP_SPI_BAUD_DIV_128 = 6,
    BSP_SPI_BAUD_DIV_256 = 7
} BSP_SPI_BaudPrescaler_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化 SPI 总线（GPIO + 时钟 + HAL 外设配置）
 * @param  id       总线编号
 * @param  prescaler 波特率预分频
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用。
 *         CS 引脚由上层模块管理，本函数不配置。
 *         仅需调用一次。重复调用安全（幂等）。
 */
void BSP_SPI_Init(BSP_SPI_Bus_Id_t id, BSP_SPI_BaudPrescaler_t prescaler);

/**
 * @brief  SPI 单字节全双工传输
 * @param  id      总线编号
 * @param  data    待发送的字节
 * @param  timeout 超时 (ms)
 * @return 同时接收到的字节
 * @note   阻塞式操作，含超时保护。超时时返回 0xFF。
 */
uint8_t BSP_SPI_Transfer(BSP_SPI_Bus_Id_t id, uint8_t data, uint32_t timeout);

/**
 * @brief  SPI 多字节全双工传输 (等长收发)
 * @param  id      总线编号
 * @param  tx      发送缓冲区
 * @param  rx      接收缓冲区 (可与 tx 相同地址，实现原地传输)
 * @param  len     传输字节数
 * @param  timeout 超时 (ms)
 * @note   发送和接收同时进行：每发送一个字节便接收一个字节。
 *         若只关心接收，tx 可填 Dummy (0xFF)。
 *         若只关心发送，rx 可传 NULL 丢弃接收数据。
 */
void BSP_SPI_TransferBuffer(BSP_SPI_Bus_Id_t id,
                            const uint8_t *tx, uint8_t *rx, uint16_t len,
                            uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SPI_H__ */
