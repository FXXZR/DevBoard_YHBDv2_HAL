/**
 * @file    test_eeprom.c
 * @brief   EEPROM (AT24C02) 读写验证测试
 * @note    通过 I2C (默认软件 I2C, 可通过宏切换硬件 I2C) 操作 AT24C02。
 *
 *          AT24C02 规格: 256B, 32 页 × 8B, 7-bit 地址 0x50。
 *
 *          测试流程:
 *            1. 初始化 EEPROM (含底层 I2C 总线)
 *            2. 全片擦除 (写 0xFF)
 *            3. 写入测试模式 (0x00, 0x01, ..., 0xFF)
 *            4. 读回校验所有 256 字节
 *            5. 打印测试结果
 *
 *          预期输出 (串口):
 *            [EEPROM Test] AT24C02 via Soft I2C (PB6=SCL, PB7=SDA)
 *            Erasing...
 *            Writing test pattern...
 *            Verifying... PASS
 *            -- or --
 *            FAIL at addr 0x12: expected 0x12, got 0xFF
 */

#include "test.h"

void Test_EEPROM_ReadWrite(void)
{
    uint8_t buf[BSP_EEPROM_CAPACITY];
    uint8_t errors = 0;

    /* ---- 1. 初始化 ---- */
    BSP_EEPROM_Init();

#if defined(BSP_EEPROM_USE_HW_I2C)
    printf("\r\n[EEPROM Test] AT24C02 via HW I2C (PB6=SCL, PB7=SDA)\r\n");
#else
    printf("\r\n[EEPROM Test] AT24C02 via Soft I2C (PB6=SCL, PB7=SDA)\r\n");
#endif

    /* ---- 2. 检查设备是否在线 ---- */
    if (BSP_EEPROM_IsReady() != BSP_EEPROM_OK) {
        printf("  FAIL: EEPROM device not found at addr 0x%02X\r\n", BSP_EEPROM_DEV_ADDR);
        printf("  Check: I2C wiring (PB6=SCL, PB7=SDA) and pull-up resistors.\r\n");
        return;
    }
    printf("  Device detected OK.\r\n");

    /* ---- 3. 全片擦除 ---- */
    printf("  Erasing (fill 0xFF)...\r\n");
    BSP_EEPROM_Erase();

    /* ---- 4. 写入测试模式 0x00~0xFF ---- */
    printf("  Writing test pattern (0x00..0xFF)...\r\n");
    for (uint16_t i = 0; i < BSP_EEPROM_CAPACITY; i++) {
        buf[i] = (uint8_t)i;
    }
    BSP_EEPROM_Write(buf, 0, BSP_EEPROM_CAPACITY);

    /* ---- 5. 读回并按字节校验 ---- */
    printf("  Verifying...\r\n");
    if (BSP_EEPROM_Read(buf, 0, BSP_EEPROM_CAPACITY) != BSP_EEPROM_OK) {
        printf("  FAIL: Read error.\r\n");
        return;
    }

    for (uint16_t i = 0; i < BSP_EEPROM_CAPACITY; i++) {
        if (buf[i] != (uint8_t)i) {
            printf("  FAIL at addr 0x%02X: expected 0x%02X, got 0x%02X\r\n",
                   (uint8_t)i, (uint8_t)i, buf[i]);
            errors++;
            if (errors >= 10) {
                printf("  ... (stopped after 10 errors)\r\n");
                break;
            }
        }
    }

    if (errors == 0) {
        printf("  PASS — all %u bytes verified.\r\n", BSP_EEPROM_CAPACITY);
    }

    printf("\r\n");
}
