/**
 * @file    bsp_sdcard.h
 * @brief   BSP SD Card 驱动接口 —— 基于 SDIO DMA 的 SD 卡块设备访问
 * @note    本模块封装 STM32F103 SDIO 外设 + HAL SD 驱动，对外提供阻塞式
 *          扇区读写 API。内部使用 DMA2_Channel4 进行数据传输（半双工，
 *          每次传输前重配方向），通过轮询 HAL_SD_GetCardState() 等待完成。
 *
 *          SDIO 引脚 (固定复用，无需 AFIO 重映射):
 *            PC8  = SDIO_D0,  PC9  = SDIO_D1
 *            PC10 = SDIO_D2,  PC11 = SDIO_D3
 *            PC12 = SDIO_CK,  PD2  = SDIO_CMD
 *
 *          MSP 初始化、DMA 配置、ISR 处理全部在本模块 .c 内实现，
 *          保持 BSP 模块自包含原则。
 */

#ifndef __BSP_SDCARD_H__
#define __BSP_SDCARD_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 总线 ID 枚举 (遵循项目"枚举 ID 代替指针句柄"规范)
 * ================================================================ */
typedef enum {
    BSP_SDCARD_BUS_1 = 0,
    BSP_SDCARD_BUS_COUNT
} BSP_SDCARD_Bus_Id_t;

/* ================================================================
 * 状态码枚举
 * ================================================================ */
typedef enum {
    BSP_SDCARD_OK       = 0,
    BSP_SDCARD_ERROR    = 1,
    BSP_SDCARD_TIMEOUT  = 2,
    BSP_SDCARD_NO_CARD  = 3,
    BSP_SDCARD_BUSY     = 4
} BSP_SDCARD_Status_t;

/* ================================================================
 * 配置常量 (匿名枚举)
 * ================================================================ */
enum {
    BSP_SDCARD_BLOCK_SIZE     = 512,        /* SD 卡标准块大小 */
    BSP_SDCARD_TIMEOUT_READ   = 30000,      /* 读超时 (ms) */
    BSP_SDCARD_TIMEOUT_WRITE  = 30000,      /* 写超时 (ms) */
    BSP_SDCARD_TIMEOUT_INIT   = 1000        /* 初始化超时 (ms) */
};

enum {
    BSP_SDCARD_CLK_DIV_INIT  = 0xB2,        /* 72MHz/(178+2) ≈ 400KHz (SD 规范初始化速率) */
    BSP_SDCARD_CLK_DIV_TRANS = 0x01         /* 72MHz/(1+2)   = 24MHz  (4-bit 高速传输)   */
};

/* ================================================================
 * SD 卡信息结构体 (平台无关, 不暴露 HAL 类型)
 * ================================================================ */
typedef struct {
    uint32_t block_size;       /* 物理块大小 (通常 512 字节)    */
    uint32_t block_count;      /* 物理块总数                     */
    uint32_t block_count_log;  /* 逻辑块数 (CardCapacity / 512)  */
    uint32_t block_size_log;   /* 逻辑块大小 (通常 512 字节)     */
    uint32_t card_type;        /* 0 = SDSC, 1 = SDHC/SDXC       */
    uint32_t card_version;     /* 0 = V1.x, 1 = V2.x            */
} BSP_SDCARD_CardInfo_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化 SDIO 外设并检测/初始化 SD 卡
 * @param  id  SDIO 总线 ID (BSP_SDCARD_BUS_1)
 * @return BSP_SDCARD_OK 成功, BSP_SDCARD_NO_CARD 未检测到卡, 其他为错误
 * @note   幂等: 重复调用安全 (已初始化则直接返回 OK)
 *         必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用
 */
BSP_SDCARD_Status_t BSP_SDCARD_Init(BSP_SDCARD_Bus_Id_t id);

/**
 * @brief  从 SD 卡读取一个或多个扇区 (阻塞, DMA 传输)
 * @param  id           SDIO 总线 ID
 * @param  buf          接收缓冲区 (需 4 字节对齐, 至少 count * 512 字节)
 * @param  sector_addr  起始扇区地址 (LBA)
 * @param  num_sectors  扇区数量
 * @return BSP_SDCARD_OK 成功, 其他为错误
 */
BSP_SDCARD_Status_t BSP_SDCARD_Read(BSP_SDCARD_Bus_Id_t id, uint8_t *buf,
                                    uint32_t sector_addr, uint32_t num_sectors);

/**
 * @brief  向 SD 卡写入一个或多个扇区 (阻塞, DMA 传输)
 * @param  id           SDIO 总线 ID
 * @param  buf          数据缓冲区 (需 4 字节对齐, 至少 count * 512 字节)
 * @param  sector_addr  起始扇区地址 (LBA)
 * @param  num_sectors  扇区数量
 * @return BSP_SDCARD_OK 成功, 其他为错误
 */
BSP_SDCARD_Status_t BSP_SDCARD_Write(BSP_SDCARD_Bus_Id_t id, const uint8_t *buf,
                                     uint32_t sector_addr, uint32_t num_sectors);

/**
 * @brief  获取 SD 卡信息 (容量、块大小、类型等)
 * @param  id    SDIO 总线 ID
 * @param  info  输出参数，存放卡信息
 * @return BSP_SDCARD_OK 成功
 */
BSP_SDCARD_Status_t BSP_SDCARD_GetInfo(BSP_SDCARD_Bus_Id_t id, BSP_SDCARD_CardInfo_t *info);

/**
 * @brief  检查 SD 卡是否就绪 (已初始化且卡在位)
 * @param  id  SDIO 总线 ID
 * @return 1 就绪, 0 未就绪
 */
uint8_t BSP_SDCARD_IsReady(BSP_SDCARD_Bus_Id_t id);

/**
 * @brief  反初始化 SDIO 外设, 关闭时钟
 * @param  id  SDIO 总线 ID
 * @return BSP_SDCARD_OK 成功
 */
BSP_SDCARD_Status_t BSP_SDCARD_DeInit(BSP_SDCARD_Bus_Id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SDCARD_H__ */
