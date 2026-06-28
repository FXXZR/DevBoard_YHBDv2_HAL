/**
 * @file    test_sram.c
 * @brief   外部 SRAM 读写验证测试
 * @note    流程: FSMC 初始化 → 8位写入/读回校验 → 16位写入/读回校验 → 打印结果.
 *          使用直接内存指针访问 SRAM (FSMC 内存映射).
 */

#include <stdint.h>
#include <stdio.h>
#include "test.h"

/* ================================================================
 * 内部常量
 * ================================================================ */

static const uint32_t kTestSize = BSP_SRAM_SIZE;       /* 全片测试 (1MB) */

/* ================================================================
 * 测试函数
 * ================================================================ */

void Test_SRAM_ReadWrite(void)
{
    uint32_t i;
    uint8_t  err_8b  = 0;
    uint8_t  err_16b = 0;
    uint8_t  wdata_8b  = 0x00;
    uint16_t wdata_16b = 0x0000;

    /* 初始化 FSMC → SRAM */
    printf("\r\n[SRAM Test] Initializing FSMC Bank3...\r\n");
    BSP_SRAM_Init();
    printf("[SRAM Test] FSMC initialized, base=0x%08X, size=%u bytes\r\n",
           BSP_SRAM_BASE_ADDR, BSP_SRAM_SIZE);

    /* 获取直接指针 */
    volatile uint8_t  *p8  = (volatile uint8_t  *)BSP_SRAM_BASE_ADDR;
    volatile uint16_t *p16 = (volatile uint16_t *)BSP_SRAM_BASE_ADDR;

    /* ---- 8 位读写测试 ---- */
    printf("[SRAM Test] 8-bit write/read test... ");

    /* 先清零 */
    for (i = 0; i < kTestSize; i++) {
        p8[i] = 0x00;
    }

    /* 写入递增测试模式 */
    for (i = 0; i < kTestSize; i++) {
        p8[i] = (uint8_t)(wdata_8b + i);
    }

    /* 读回校验 */
    for (i = 0; i < kTestSize; i++) {
        if (p8[i] != (uint8_t)(wdata_8b + i)) {
            if (err_8b < 10) {  /* 最多打印 10 个错误 */
                printf("FAIL @0x%08lX: exp=0x%02X got=0x%02X\r\n",
                       (unsigned long)(BSP_SRAM_BASE_ADDR + i),
                       (uint8_t)(wdata_8b + i), p8[i]);
            }
            err_8b++;
        }
    }

    if (err_8b == 0) {
        printf("PASS\r\n");
    } else {
        printf("FAIL (%u errors)\r\n", err_8b);
    }

    /* ---- 16 位读写测试 ---- */
    printf("[SRAM Test] 16-bit write/read test... ");

    const uint32_t kHalfSize = kTestSize / 2;

    /* 先清零 */
    for (i = 0; i < kHalfSize; i++) {
        p16[i] = 0x0000;
    }

    /* 写入递增测试模式 */
    for (i = 0; i < kHalfSize; i++) {
        p16[i] = (uint16_t)(wdata_16b + i);
    }

    /* 读回校验 */
    for (i = 0; i < kHalfSize; i++) {
        if (p16[i] != (uint16_t)(wdata_16b + i)) {
            if (err_16b < 10) {
                printf("FAIL @0x%08lX: exp=0x%04X got=0x%04X\r\n",
                       (unsigned long)(BSP_SRAM_BASE_ADDR + i * 2),
                       (uint16_t)(wdata_16b + i), p16[i]);
            }
            err_16b++;
        }
    }

    if (err_16b == 0) {
        printf("PASS\r\n");
    } else {
        printf("FAIL (%u errors)\r\n", err_16b);
    }

    /* 汇总 */
    printf("[SRAM Test] Result: 8b=%s, 16b=%s\r\n",
           err_8b  == 0 ? "PASS" : "FAIL",
           err_16b == 0 ? "PASS" : "FAIL");
}
