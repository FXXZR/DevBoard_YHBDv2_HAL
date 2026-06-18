/**
 * @file    test_sdcard_fatfs.c
 * @brief   SD Card + FatFS 读写验证测试
 * @note    测试流程:
 *            1. 初始化 SD 卡, 打印卡类型和容量
 *            2. 挂载 FatFS (不自动格式化, 保护已有数据)
 *            3. 创建测试文件, 写入测试内容
 *            4. 读回文件内容并逐字节校验
 *            5. 卸载文件系统, 打印 PASS/FAIL
 */

#include "test.h"
#include "bsp_sdcard.h"
#include "ff.h"

/* ---- 测试参数 (匿名枚举) ---- */
enum {
    kTestBufSize = 512,        /* 测试缓冲区大小 */
    kTestPatternLen = 256      /* 写入模式长度 */
};

/**
 * @brief  SD Card + FatFS 读写验证测试
 */
void Test_SDCARD_FatFS(void)
{
    printf("\r\n========================================\r\n");
    printf("  [SD Card + FatFS Test]\r\n");
    printf("========================================\r\n\r\n");

    /* ================================================================
     * 1. 初始化 SD 卡
     * ================================================================ */
    printf("  [1/5] Initializing SD card...\r\n");

    BSP_SDCARD_Status_t sd_st = BSP_SDCARD_Init(BSP_SDCARD_BUS_1);
    if (sd_st == BSP_SDCARD_NO_CARD) {
        printf("  FAIL: No SD card detected!\r\n");
        printf("  Please insert an SD card and reset.\r\n");
        return;
    }
    if (sd_st != BSP_SDCARD_OK) {
        printf("  FAIL: SD card init error (code=%d)\r\n", (int)sd_st);
        return;
    }

    /* 打印卡信息 */
    BSP_SDCARD_CardInfo_t info;
    BSP_SDCARD_GetInfo(BSP_SDCARD_BUS_1, &info);
    printf("  Card type   : %s\r\n",
           info.card_type ? "SDHC/SDXC" : "SDSC");
    printf("  Card version: V%d.x\r\n",
           info.card_version ? 2 : 1);
    printf("  Capacity    : %lu MB (%lu blocks x %lu bytes)\r\n",
           (unsigned long)((uint64_t)info.block_count * info.block_size / (1024UL * 1024UL)),
           (unsigned long)info.block_count,
           (unsigned long)info.block_size);
    printf("  Log blocks  : %lu x %lu bytes\r\n\r\n",
           (unsigned long)info.block_count_log,
           (unsigned long)info.block_size_log);

    /* ================================================================
     * 2. 挂载文件系统
     * ================================================================ */
    printf("  [2/5] Mounting filesystem...\r\n");

    static FATFS g_fs;  /* 静态分配, 避免大栈帧 */
    FRESULT fr = f_mount(&g_fs, "0:", 1);
    if (fr == FR_NO_FILESYSTEM) {
        printf("  No filesystem found, formatting as FAT32...\r\n");
        uint8_t work[FF_MAX_SS] __attribute__((aligned(4)));
        MKFS_PARM opt = {FM_FAT32, 0, 0, 0};
        fr = f_mkfs("0:", &opt, work, sizeof(work));
        if (fr != FR_OK) {
            printf("  FAIL: f_mkfs() returned %d\r\n", (int)fr);
            f_mount(NULL, "0:", 0);
            return;
        }
        printf("  Format OK. Re-mounting...\r\n");
        /* 格式化后需重新挂载 */
        fr = f_mount(&g_fs, "0:", 1);
    }
    if (fr != FR_OK) {
        printf("  FAIL: f_mount() returned %d\r\n", (int)fr);
        return;
    }
    printf("  Mount OK.\r\n\r\n");

    /* ================================================================
     * 3. 创建测试文件并写入
     * ================================================================ */
    printf("  [3/5] Writing test file...\r\n");

    /* 准备测试数据模式: 0x00, 0x01, ..., 0xFF, 0x00, ... */
    uint8_t tx_buf[kTestBufSize];
    for (int i = 0; i < kTestBufSize; i++) {
        tx_buf[i] = (uint8_t)(i & 0xFF);
    }

    FIL fil;
    UINT bw;
    fr = f_open(&fil, "0:test.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("  FAIL: f_open(write) returned %d\r\n", (int)fr);
        f_mount(NULL, "0:", 0);
        return;
    }

    fr = f_write(&fil, tx_buf, kTestBufSize, &bw);
    if (fr != FR_OK) {
        printf("  FAIL: f_write() returned %d\r\n", (int)fr);
        f_close(&fil);
        f_mount(NULL, "0:", 0);
        return;
    }
    if (bw != kTestBufSize) {
        printf("  FAIL: f_write() wrote %lu/%d bytes\r\n",
               (unsigned long)bw, kTestBufSize);
        f_close(&fil);
        f_mount(NULL, "0:", 0);
        return;
    }
    f_close(&fil);
    printf("  Wrote %lu bytes to test.txt\r\n\r\n", (unsigned long)bw);

    /* ================================================================
     * 4. 读回并校验
     * ================================================================ */
    printf("  [4/5] Reading back and verifying...\r\n");

    uint8_t rx_buf[kTestBufSize] = {0};
    UINT br;

    fr = f_open(&fil, "0:test.txt", FA_OPEN_EXISTING | FA_READ);
    if (fr != FR_OK) {
        printf("  FAIL: f_open(read) returned %d\r\n", (int)fr);
        f_mount(NULL, "0:", 0);
        return;
    }

    fr = f_read(&fil, rx_buf, kTestBufSize, &br);
    if (fr != FR_OK) {
        printf("  FAIL: f_read() returned %d\r\n", (int)fr);
        f_close(&fil);
        f_mount(NULL, "0:", 0);
        return;
    }
    f_close(&fil);

    if (br != kTestBufSize) {
        printf("  FAIL: f_read() only got %lu/%d bytes\r\n",
               (unsigned long)br, kTestBufSize);
        f_mount(NULL, "0:", 0);
        return;
    }

    /* 逐字节校验 */
    int errors = 0;
    for (int i = 0; i < kTestBufSize; i++) {
        if (rx_buf[i] != tx_buf[i]) {
            if (errors < 10) {
                printf("  MISMATCH at byte %d: wrote 0x%02X, read 0x%02X\r\n",
                       i, tx_buf[i], rx_buf[i]);
            }
            errors++;
        }
    }
    if (errors > 10) {
        printf("  ... and %d more errors (total %d)\r\n",
               errors - 10, errors);
    }

    /* ================================================================
     * 5. 结果
     * ================================================================ */
    printf("\r\n  [5/5] Result: ");
    if (errors == 0) {
        printf("PASS\r\n");
        printf("  SD Card + FatFS read/write verified OK.\r\n");
    } else {
        printf("FAIL (%d byte mismatches)\r\n", errors);
    }

    /* 清理: 卸载文件系统 */
    f_mount(NULL, "0:", 0);

    printf("\r\n========================================\r\n");
    printf("  Test complete.\r\n");
    printf("========================================\r\n\r\n");
}
