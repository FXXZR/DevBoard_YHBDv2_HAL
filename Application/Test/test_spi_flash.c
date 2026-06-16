/**
 * @file    test_spi_flash.c
 * @brief   SPI Flash (W25Q64) 读写验证测试
 * @note    通过 SPI1 操作 W25Q64 (64Mbit / 8MB)。
 *          CS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7。
 *
 *          测试流程:
 *            1. 初始化 SPI Flash (含 SPI 总线 + CS 引脚)
 *            2. 读取 JEDEC ID 并校验
 *            3. 擦除首扇区 (4KB)
 *            4. 写入测试数据 (256B 跨页模式)
 *            5. 读回校验
 *            6. 打印测试结果
 *
 *          预期输出 (串口):
 *            [SPI Flash Test] W25Q64 via SPI1 (PA5=SCK, PA6=MISO, PA7=MOSI, PA4=CS)
 *            JEDEC ID: 0xEF4017 — OK
 *            Erasing sector 0...
 *            Writing test data...
 *            Verifying... PASS
 *
 *          WARNING: 测试会破坏扇区 0 的原有数据！
 */

#include "test.h"

/* 测试数据长度 (不超过一页 256B 以简化测试，实际可跨页) */
#define TEST_DATA_LEN   256U
#define TEST_SECTOR     0U     /* 测试用扇区 (扇区 0, 地址 0x000000) */

void Test_SPI_FLASH_ReadWrite(void)
{
    uint8_t  tx_buf[TEST_DATA_LEN];
    uint8_t  rx_buf[TEST_DATA_LEN];
    uint32_t jedec_id;
    uint16_t errors = 0;

    /* ---- 1. 初始化 ---- */
    BSP_SPI_FLASH_Init();

    printf("\r\n[SPI Flash Test] W25Q64 via SPI1 "
           "(PA5=SCK, PA6=MISO, PA7=MOSI, PA4=CS)\r\n");

    /* ---- 2. 读取 JEDEC ID ---- */
    jedec_id = BSP_SPI_FLASH_ReadID();
    printf("  JEDEC ID: 0x%06lX", jedec_id);

    if (jedec_id == BSP_SPI_FLASH_JEDEC_ID_W25Q64) {
        printf(" — OK (W25Q64)\r\n");
    } else if (jedec_id == 0x000000 || jedec_id == 0xFFFFFF) {
        printf("\r\n  FAIL: No response (check SPI wiring + CS=PA4)\r\n");
        return;
    } else {
        printf(" — WARNING: unexpected ID, continuing anyway...\r\n");
    }

    /* ---- 3. 擦除扇区 ---- */
    printf("  Erasing sector %u...\r\n", TEST_SECTOR);
    BSP_SPI_FLASH_SectorErase(TEST_SECTOR * BSP_SPI_FLASH_SECTOR_SIZE);

    /* ---- 4. 准备 + 写入测试数据 ---- */
    printf("  Writing test data (%u bytes)...\r\n", TEST_DATA_LEN);
    for (uint16_t i = 0; i < TEST_DATA_LEN; i++) {
        tx_buf[i] = (uint8_t)(i & 0xFF);
    }
    BSP_SPI_FLASH_Write(tx_buf, TEST_SECTOR * BSP_SPI_FLASH_SECTOR_SIZE, TEST_DATA_LEN);

    /* ---- 5. 读回 + 校验 ---- */
    printf("  Verifying...\r\n");
    BSP_SPI_FLASH_Read(rx_buf, TEST_SECTOR * BSP_SPI_FLASH_SECTOR_SIZE, TEST_DATA_LEN);

    for (uint16_t i = 0; i < TEST_DATA_LEN; i++) {
        if (rx_buf[i] != tx_buf[i]) {
            printf("  FAIL at addr 0x%04X: expected 0x%02X, got 0x%02X\r\n",
                   i, tx_buf[i], rx_buf[i]);
            errors++;
            if (errors >= 10) {
                printf("  ... (stopped after 10 errors)\r\n");
                break;
            }
        }
    }

    if (errors == 0) {
        printf("  PASS — all %u bytes verified.\r\n", TEST_DATA_LEN);
    }

    /* ---- 6. 容量信息 ---- */
    printf("  Flash size: %lu MB (%lu sectors, %lu blocks)\r\n\r\n",
           (uint32_t)(BSP_SPI_FLASH_CAPACITY / 1048576UL),
           (uint32_t)(BSP_SPI_FLASH_CAPACITY / BSP_SPI_FLASH_SECTOR_SIZE),
           (uint32_t)(BSP_SPI_FLASH_CAPACITY / BSP_SPI_FLASH_BLOCK_SIZE));
}
