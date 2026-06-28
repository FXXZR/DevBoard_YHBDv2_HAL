/**
 * @file    bsp_sram.h
 * @brief   BSP 外部 SRAM 驱动接口 (IS62WV51216)
 * @note    使用 FSMC Bank1 Sector3 (NE3), 基地址 0x68000000, 16 位总线, 1MB 容量.
 *
 *          FSMC 初始化完成后, SRAM 作为内存映射设备直接通过指针访问:
 *          @code
 *            uint8_t  *p8  = (uint8_t  *)BSP_SRAM_BASE_ADDR;
 *            uint16_t *p16 = (uint16_t *)BSP_SRAM_BASE_ADDR;
 *            p8[offset] = 0x55;         // 8 位写入
 *            val = p16[offset >> 1];    // 16 位读取 (地址必须对齐)
 *          @endcode
 *
 *          本模块不提供 Read/Write 包装函数 —— SRAM 是内存映射设备,
 *          C 语言原生指针操作即是最高效的 API.
 */

#ifndef __BSP_SRAM_H__
#define __BSP_SRAM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 硬件常量 (匿名 enum, 暴露给应用层)
 * ================================================================ */

enum {
    BSP_SRAM_BASE_ADDR = 0x68000000,   /* FSMC Bank1 Sector3 基地址 */
    BSP_SRAM_SIZE      = 0x100000      /* IS62WV51216: 1MB (512K x 16bit) */
};

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化外部 SRAM (FSMC Bank3, GPIO + HAL_SRAM_Init)
 * @note   依赖 HAL_Init() + BSP_SYSTICK_DelayInit() 已在 main() 中完成.
 *         幂等 —— 重复调用安全.
 */
void BSP_SRAM_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SRAM_H__ */
