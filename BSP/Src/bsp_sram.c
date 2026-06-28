/**
 * @file    bsp_sram.c
 * @brief   BSP 外部 SRAM 驱动实现 — IS62WV51216 via FSMC Bank3
 * @note    架构遵循项目 BSP 模式: static const HW 映射表 (枚举下标) + static ctx.
 *
 *          引脚 (野火霸道 V2 YHBDv2):
 *            地址 A0~A18  → PF0~5,PF12~15,PG0~5,PD11~13
 *            数据 D0~D15  → PD14~15,PD0~1,PE7~15,PD8~10
 *            控制 CS/WE/OE/UB/LB → PG10,PD5,PD4,PE1,PE0
 *
 *          时序: AddressSetup=0, DataSetup=2 (3 HCLK≈42ns @72MHz), AccessMode=A.
 */

#include "bsp_sram.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 引脚枚举 — 命名索引, 映射表中不再需要注释猜测位置
 * ================================================================ */

typedef enum {
    /* 地址总线 A0~A18 */
    FSMC_PIN_A0,  FSMC_PIN_A1,  FSMC_PIN_A2,  FSMC_PIN_A3,  FSMC_PIN_A4,
    FSMC_PIN_A5,  FSMC_PIN_A6,  FSMC_PIN_A7,  FSMC_PIN_A8,  FSMC_PIN_A9,
    FSMC_PIN_A10, FSMC_PIN_A11, FSMC_PIN_A12, FSMC_PIN_A13, FSMC_PIN_A14,
    FSMC_PIN_A15, FSMC_PIN_A16, FSMC_PIN_A17, FSMC_PIN_A18,

    /* 数据总线 D0~D15 */
    FSMC_PIN_D0,  FSMC_PIN_D1,  FSMC_PIN_D2,  FSMC_PIN_D3,
    FSMC_PIN_D4,  FSMC_PIN_D5,  FSMC_PIN_D6,  FSMC_PIN_D7,
    FSMC_PIN_D8,  FSMC_PIN_D9,  FSMC_PIN_D10, FSMC_PIN_D11,
    FSMC_PIN_D12, FSMC_PIN_D13, FSMC_PIN_D14, FSMC_PIN_D15,

    /* 控制信号 */
    FSMC_PIN_NE3,       /* CS  — PG10 */
    FSMC_PIN_NWE,       /* WE  — PD5  */
    FSMC_PIN_NOE,       /* OE  — PD4  */
    FSMC_PIN_NBL1,      /* UB  — PE1  */
    FSMC_PIN_NBL0,      /* LB  — PE0  */

    FSMC_PIN_COUNT
} FSMC_PinID_t;

/* ================================================================
 * 硬件映射表 — 枚举名称 = 自文档化的数组下标
 * ================================================================ */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} FSMC_Pin_t;

static const FSMC_Pin_t fsmc_pins[FSMC_PIN_COUNT] = {
    /* ---- 地址总线 A0~A18 ---- */
    [FSMC_PIN_A0]  = {GPIOF, GPIO_PIN_0},
    [FSMC_PIN_A1]  = {GPIOF, GPIO_PIN_1},
    [FSMC_PIN_A2]  = {GPIOF, GPIO_PIN_2},
    [FSMC_PIN_A3]  = {GPIOF, GPIO_PIN_3},
    [FSMC_PIN_A4]  = {GPIOF, GPIO_PIN_4},
    [FSMC_PIN_A5]  = {GPIOF, GPIO_PIN_5},
    [FSMC_PIN_A6]  = {GPIOF, GPIO_PIN_12},
    [FSMC_PIN_A7]  = {GPIOF, GPIO_PIN_13},
    [FSMC_PIN_A8]  = {GPIOF, GPIO_PIN_14},
    [FSMC_PIN_A9]  = {GPIOF, GPIO_PIN_15},
    [FSMC_PIN_A10] = {GPIOG, GPIO_PIN_0},
    [FSMC_PIN_A11] = {GPIOG, GPIO_PIN_1},
    [FSMC_PIN_A12] = {GPIOG, GPIO_PIN_2},
    [FSMC_PIN_A13] = {GPIOG, GPIO_PIN_3},
    [FSMC_PIN_A14] = {GPIOG, GPIO_PIN_4},
    [FSMC_PIN_A15] = {GPIOG, GPIO_PIN_5},
    [FSMC_PIN_A16] = {GPIOD, GPIO_PIN_11},
    [FSMC_PIN_A17] = {GPIOD, GPIO_PIN_12},
    [FSMC_PIN_A18] = {GPIOD, GPIO_PIN_13},

    /* ---- 数据总线 D0~D15 ---- */
    [FSMC_PIN_D0]  = {GPIOD, GPIO_PIN_14},
    [FSMC_PIN_D1]  = {GPIOD, GPIO_PIN_15},
    [FSMC_PIN_D2]  = {GPIOD, GPIO_PIN_0},
    [FSMC_PIN_D3]  = {GPIOD, GPIO_PIN_1},
    [FSMC_PIN_D4]  = {GPIOE, GPIO_PIN_7},
    [FSMC_PIN_D5]  = {GPIOE, GPIO_PIN_8},
    [FSMC_PIN_D6]  = {GPIOE, GPIO_PIN_9},
    [FSMC_PIN_D7]  = {GPIOE, GPIO_PIN_10},
    [FSMC_PIN_D8]  = {GPIOE, GPIO_PIN_11},
    [FSMC_PIN_D9]  = {GPIOE, GPIO_PIN_12},
    [FSMC_PIN_D10] = {GPIOE, GPIO_PIN_13},
    [FSMC_PIN_D11] = {GPIOE, GPIO_PIN_14},
    [FSMC_PIN_D12] = {GPIOE, GPIO_PIN_15},
    [FSMC_PIN_D13] = {GPIOD, GPIO_PIN_8},
    [FSMC_PIN_D14] = {GPIOD, GPIO_PIN_9},
    [FSMC_PIN_D15] = {GPIOD, GPIO_PIN_10},

    /* ---- 控制信号 ---- */
    [FSMC_PIN_NE3]  = {GPIOG, GPIO_PIN_10},
    [FSMC_PIN_NWE]  = {GPIOD, GPIO_PIN_5},
    [FSMC_PIN_NOE]  = {GPIOD, GPIO_PIN_4},
    [FSMC_PIN_NBL1] = {GPIOE, GPIO_PIN_1},
    [FSMC_PIN_NBL0] = {GPIOE, GPIO_PIN_0},
};

/* ================================================================
 * 运行时上下文
 * ================================================================ */

typedef struct {
    SRAM_HandleTypeDef handle;
    uint8_t            initialized;
} SRAM_Ctx_t;

static SRAM_Ctx_t sram_ctx;

/* ================================================================
 * 内部辅助
 * ================================================================ */

/**
 * @brief  统一初始化所有 FSMC 引脚 (AF_PP, 上拉, 高速)
 * @note   所有引脚配置完全相同, 逐引脚 HAL_GPIO_Init 即可.
 *         Init 在启动时只跑一次, 无需端口分组优化.
 */
static void fsmc_init_all_gpio(void)
{
    /* 使能所有 FSMC 相关 GPIO 时钟 */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    FSMC_PinID_t id;
    for (id = 0; id < FSMC_PIN_COUNT; id++) {
        gpio.Pin = fsmc_pins[id].pin;
        HAL_GPIO_Init(fsmc_pins[id].port, &gpio);
    }
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_SRAM_Init(void)
{
    if (sram_ctx.initialized) return;
    sram_ctx.initialized = 1;

    /* 1. 使能 FSMC 外设时钟 */
    __HAL_RCC_FSMC_CLK_ENABLE();

    /* 2. 初始化所有 FSMC GPIO (AF_PP, 上拉, 高速) */
    fsmc_init_all_gpio();

    /* 3. HAL SRAM 配置 */
    FSMC_NORSRAM_TimingTypeDef timing = {0};
    timing.AddressSetupTime      = 0x00;
    timing.AddressHoldTime       = 0x00;
    timing.DataSetupTime         = 0x02;   /* HCLK×(2+1)=3×13.9ns≈42ns (IS62WV51216 tDW>25ns) */
    timing.BusTurnAroundDuration = 0x00;
    timing.CLKDivision           = 0x01;   /* HAL算术: (1-1)<<20=0, 异步模式不使用 */
    timing.DataLatency           = 0x02;   /* HAL算术: (2-2)<<24=0, 异步模式不使用 */
    timing.AccessMode            = FSMC_ACCESS_MODE_A;

    sram_ctx.handle.Instance               = FSMC_NORSRAM_DEVICE;
    sram_ctx.handle.Extended               = FSMC_NORSRAM_EXTENDED_DEVICE;
    sram_ctx.handle.Init.NSBank            = FSMC_NORSRAM_BANK3;
    sram_ctx.handle.Init.DataAddressMux    = FSMC_DATA_ADDRESS_MUX_DISABLE;
    sram_ctx.handle.Init.MemoryType        = FSMC_MEMORY_TYPE_SRAM;
    sram_ctx.handle.Init.MemoryDataWidth   = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    sram_ctx.handle.Init.BurstAccessMode   = FSMC_BURST_ACCESS_MODE_DISABLE;
    sram_ctx.handle.Init.WaitSignalPolarity= FSMC_WAIT_SIGNAL_POLARITY_LOW;
    sram_ctx.handle.Init.WrapMode          = FSMC_WRAP_MODE_DISABLE;
    sram_ctx.handle.Init.WaitSignalActive  = FSMC_WAIT_TIMING_BEFORE_WS;
    sram_ctx.handle.Init.WriteOperation    = FSMC_WRITE_OPERATION_ENABLE;
    sram_ctx.handle.Init.WaitSignal        = FSMC_WAIT_SIGNAL_DISABLE;
    sram_ctx.handle.Init.ExtendedMode      = FSMC_EXTENDED_MODE_DISABLE;
    sram_ctx.handle.Init.AsynchronousWait  = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    sram_ctx.handle.Init.WriteBurst        = FSMC_WRITE_BURST_DISABLE;
    sram_ctx.handle.Init.PageSize          = FSMC_PAGE_SIZE_NONE;

    HAL_SRAM_Init(&sram_ctx.handle, &timing, &timing);
}
