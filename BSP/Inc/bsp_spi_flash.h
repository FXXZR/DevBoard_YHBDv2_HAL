/**
 * @file    bsp_spi_flash.h
 * @brief   BSP SPI Flash 驱动接口 —— W25Q64 (64Mbit / 8MB)
 * @note    基于 bsp_spi 的纯传输能力，本模块管理 CS 引脚 (PA4)
 *          并实现 W25Q64 指令集: 读/写/擦除/ID。
 *
 *          W25Q64 规格:
 *            容量:      8MB (64Mbit)
 *            页大小:    256 Bytes
 *            扇区大小:  4KB (16 页)
 *            块大小:    64KB (16 扇区 / 256 页)
 *            擦除时间:  扇区 ~150ms, 块 ~800ms, 全片 ~20s (典型值)
 *            写时间:    页 ~0.7ms (典型), max 3ms
 *
 *            JEDEC ID: 0xEF4017
 *
 *          参考: W25Q64JV Datasheet (Winbond)
 */

#ifndef __BSP_SPI_FLASH_H__
#define __BSP_SPI_FLASH_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 容量常量 (对外暴露，使用匿名 enum)
 * ================================================================ */

enum {
    BSP_SPI_FLASH_PAGE_SIZE   = 256,       /* 页大小 (Bytes)              */
    BSP_SPI_FLASH_SECTOR_SIZE = 4096,      /* 扇区大小 (4KB)              */
    BSP_SPI_FLASH_BLOCK_SIZE  = 65536,     /* 块大小 (64KB)               */
    BSP_SPI_FLASH_CAPACITY    = 8388608    /* 总容量 (8MB = 64Mbit)      */
};

/* ================================================================
 * JEDEC ID 常量
 * ================================================================ */

enum {
    BSP_SPI_FLASH_JEDEC_ID_W25Q64 = 0xEF4017
};

/* ================================================================
 * 操作超时 (ms) — 不同操作时间差异大，分别定义
 * ================================================================ */

enum {
    BSP_SPI_FLASH_TIMEOUT_STD   = 100,     /* 标准操作 (读写状态/ID)      */
    BSP_SPI_FLASH_TIMEOUT_PAGE  = 10,      /* 页编程 (max 3ms, 留余量)    */
    BSP_SPI_FLASH_TIMEOUT_SECTOR = 400,    /* 扇区擦除 (typ 150ms)        */
    BSP_SPI_FLASH_TIMEOUT_BLOCK = 2000,    /* 块擦除 (typ 800ms)          */
    BSP_SPI_FLASH_TIMEOUT_CHIP  = 40000    /* 全片擦除 (typ 20s)          */
};

/* ================================================================
 * 状态码
 * ================================================================ */

typedef enum {
    BSP_SPI_FLASH_OK    = 0,
    BSP_SPI_FLASH_ERROR = 1,
    BSP_SPI_FLASH_TIMEOUT = 2
} BSP_SPI_FLASH_Status_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化 SPI Flash（含 SPI 总线 + CS 引脚 + Flash 上电）
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用。
 *         仅需调用一次。
 */
void BSP_SPI_FLASH_Init(void);

/**
 * @brief  读取 JEDEC ID (3 字节)
 * @return 24-bit JEDEC ID，W25Q64 应为 0xEF4017
 */
uint32_t BSP_SPI_FLASH_ReadID(void);

/**
 * @brief  从 Flash 读取数据
 * @param  buf   接收缓冲区
 * @param  addr  Flash 内部字节地址 (0 ~ 8MB-1)
 * @param  len   读取长度 (字节)
 * @note   addr + len 不可超过 8MB。支持跨页/跨扇区连续读取。
 */
void BSP_SPI_FLASH_Read(uint8_t *buf, uint32_t addr, uint32_t len);

/**
 * @brief  向 Flash 写入数据（自动处理页边界拆分 + 写使能 + 等待完成）
 * @param  buf   数据缓冲区
 * @param  addr  Flash 内部字节地址 (0 ~ 8MB-1)
 * @param  len   写入长度 (字节)
 * @return BSP_SPI_FLASH_OK (0) 成功
 * @note   addr + len 不可超过 8MB。
 *         自动拆分跨页写入，每页写完后等待 Flash 内部编程完成。
 *         写入前无需手动擦除 —— 但 Flash 硬件要求目标地址必须先擦除
 *         (写入只能将 bit 从 1 翻为 0)，调用者需自行保证。
 */
BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_Write(const uint8_t *buf, uint32_t addr, uint32_t len);

/**
 * @brief  擦除指定扇区 (4KB)
 * @param  addr  扇区内任意地址 (自动对齐到扇区起始地址)
 * @return BSP_SPI_FLASH_OK (0) 成功
 * @note   阻塞式，等待擦除完成。
 */
BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_SectorErase(uint32_t addr);

/**
 * @brief  擦除指定块 (64KB)
 * @param  addr  块内任意地址 (自动对齐到块起始地址)
 * @return BSP_SPI_FLASH_OK (0) 成功
 * @note   阻塞式，等待擦除完成。
 */
BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_BlockErase(uint32_t addr);

/**
 * @brief  擦除整个芯片 (8MB)
 * @return BSP_SPI_FLASH_OK (0) 成功
 * @note   阻塞式，等待擦除完成 (~20s typ)。谨慎使用！
 */
BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_ChipErase(void);

/**
 * @brief  进入掉电模式 (Power-down)
 * @note   唤醒需调用 BSP_SPI_FLASH_Wakeup()
 */
void BSP_SPI_FLASH_PowerDown(void);

/**
 * @brief  从掉电模式唤醒
 */
void BSP_SPI_FLASH_Wakeup(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SPI_FLASH_H__ */
