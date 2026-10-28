/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for FatFs — SD Card via BSP SD Card driver  */
/*-----------------------------------------------------------------------*/
/* This is a glue layer that bridges FatFs to the BSP SD Card driver.    */
/* Drive 0 = SD Card (BSP_SDCARD).                                       */
/*                                                                        */
/* 归 Services/devices/ 而非 Middleware/FatFs —— 它调 BSP, 放 Middleware  */
/* 会让 Middleware 反向依赖 BSP。FatFs 侧只留上游源码与 ffconf.h。        */
/*-----------------------------------------------------------------------*/

#include "diskio.h"
#include "ff.h"
#include "string.h"

#include "bsp_sdcard.h"

/* 驱动号定义: 本工程只挂一张 SD 卡 */
static const uint8_t drive_sd_card = 0U;

/* 扇区大小必须是编译期整数 (用作栈数组尺寸), 按规范用宏 */
#define SD_BLOCKSIZE    512

/*-----------------------------------------------------------------------*/
/* 读取设备状态                                                          */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != drive_sd_card) return STA_NOINIT;
    if (!BSP_SDCARD_IsReady(BSP_SDCARD_BUS_1)) return STA_NOINIT;
    return 0;
}

/*-----------------------------------------------------------------------*/
/* 设备初始化                                                            */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != drive_sd_card) return STA_NOINIT;

    if (BSP_SDCARD_Init(BSP_SDCARD_BUS_1) == BSP_SDCARD_OK) {
        return 0;
    }
    return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* 从指定存储区读取扇区数据                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(
    BYTE pdrv,
    BYTE *buff,
    LBA_t sector,
    UINT count
)
{
    if (pdrv != drive_sd_card) return RES_PARERR;
    if (count == 0) return RES_PARERR;

    /* DMA 要求 4 字节对齐 — 非对齐时使用 bounce buffer */
    if ((DWORD)buff & 3) {
        DRESULT res = RES_OK;
        DWORD scratch[SD_BLOCKSIZE / 4];

        while (count--) {
            res = disk_read(drive_sd_card, (BYTE *)scratch, sector++, 1);
            if (res != RES_OK) break;
            memcpy(buff, scratch, SD_BLOCKSIZE);
            buff += SD_BLOCKSIZE;
        }
        return res;
    }

    if (BSP_SDCARD_Read(BSP_SDCARD_BUS_1, buff, sector, count) == BSP_SDCARD_OK) {
        return RES_OK;
    }
    return RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* 写数据到指定存储区的扇区                                              */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT disk_write(
    BYTE pdrv,
    const BYTE *buff,
    LBA_t sector,
    UINT count
)
{
    if (pdrv != drive_sd_card) return RES_PARERR;
    if (count == 0) return RES_PARERR;

    /* DMA 要求 4 字节对齐 — 非对齐时使用 bounce buffer */
    if ((DWORD)buff & 3) {
        DRESULT res = RES_OK;
        DWORD scratch[SD_BLOCKSIZE / 4];

        while (count--) {
            memcpy(scratch, buff, SD_BLOCKSIZE);
            res = disk_write(drive_sd_card, (const BYTE *)scratch, sector++, 1);
            if (res != RES_OK) break;
            buff += SD_BLOCKSIZE;
        }
        return res;
    }

    if (BSP_SDCARD_Write(BSP_SDCARD_BUS_1, buff, sector, count) == BSP_SDCARD_OK) {
        return RES_OK;
    }
    return RES_ERROR;
}
#endif

/*-----------------------------------------------------------------------*/
/* 杂项控制                                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(
    BYTE pdrv,
    BYTE cmd,
    void *buff
)
{
    if (pdrv != drive_sd_card) return RES_PARERR;

    BSP_SDCARD_CardInfo_t info;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = SD_BLOCKSIZE;
        return RES_OK;

    case GET_SECTOR_COUNT:
        if (BSP_SDCARD_GetInfo(BSP_SDCARD_BUS_1, &info) != BSP_SDCARD_OK) {
            return RES_ERROR;
        }
        *(LBA_t *)buff = (info.block_size_log == 512)
                         ? info.block_count_log
                         : (info.block_count * (info.block_size / 512));
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;  /* 擦除块大小 = 1 扇区 (SD 卡不需要此信息) */
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

/*-----------------------------------------------------------------------*/
/* 获取当前时间 (FatFS 文件时间戳)                                       */
/*-----------------------------------------------------------------------*/
DWORD get_fattime(void)
{
    /* 从 RTC 获取当前日期时间 */
    extern void BSP_RTC_GetDateTime(void *dt);  /* 前向声明, 避免额外 include */
    extern uint32_t BSP_RTC_dt_to_ts(const void *dt);

    /* 如果 RTC 已初始化, 返回实际时间 */
    /* 这里使用固定的 FatFS NORTC 配置 (ffconf.h FF_FS_NORTC=1),      */
    /* 因此 get_fattime 不会被调用。若启用 RTC, 取消 FF_FS_NORTC 即可。 */

    /* 默认返回固定时间戳: 2026-06-17 12:00:00 */
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)6 << 21)
         | ((DWORD)17 << 16)
         | ((DWORD)12 << 11)
         | ((DWORD)0 << 5)
         | ((DWORD)0 >> 1);
}
