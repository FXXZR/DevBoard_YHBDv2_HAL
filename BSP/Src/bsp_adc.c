/**
 * @file    bsp_adc.c
 * @brief   BSP ADC 驱动实现 —— 基于 STM32 HAL 的 ADC + DMA 连续转换
 * @note    ADC1 CH11 (PC1), DMA1_Ch1 循环搬运, 12bit 右对齐。
 *          VREF+ = VDDA = 3.3V (板级连接), 实际精度受 VDDA 纹波影响。
 *
 *          架构：
 *            HAL_ADC_Start_DMA → 后台循环更新 ctx->dma_value
 *            BSP_ADC_Read_mV → 直接返回最新值，零等待
 *
 *          更换 MCU 或 ADC 通道时只需修改 adc_hw_map[] 映射表。
 */

#include "bsp_adc.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 硬件映射表
 * ================================================================ */

typedef struct {
    ADC_TypeDef   *instance;
    uint32_t       channel;
    GPIO_TypeDef  *port;
    uint16_t       pin;
    DMA_Channel_TypeDef *dma_channel;
    IRQn_Type      dma_irqn;
} ADC_HW_Map_t;

static const ADC_HW_Map_t adc_hw_map[BSP_ADC_COUNT] = {
    [BSP_ADC_1] = {
        .instance    = ADC1,
        .channel     = ADC_CHANNEL_11,
        .port        = GPIOC,
        .pin         = GPIO_PIN_1,
        .dma_channel = DMA1_Channel1,
        .dma_irqn    = DMA1_Channel1_IRQn,
    },
};

/* ================================================================
 * 运行时上下文
 * ================================================================ */

typedef struct {
    ADC_HandleTypeDef   hadc;
    DMA_HandleTypeDef   hdma;
    volatile uint16_t   dma_value;      /* DMA 循环写入的最新值 */
    uint8_t             initialized;
} ADC_Ctx_t;

static ADC_Ctx_t adc_ctx[BSP_ADC_COUNT];

/* ================================================================
 * HAL 回调
 * ================================================================ */

/**
 * @brief  ADC DMA 转换完成回调（每完成一次转换触发）
 * @note   DMA 循环模式 + 单通道，每次转换完成后 DMA 自动更新 dma_value，
 *         HAL 仅用于标记完成状态，无需额外处理。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    /* 数据已由 DMA 自动写入 ctx->dma_value，无需手动拷贝 */
    (void)hadc;
}

/* ================================================================
 * 内部辅助
 * ================================================================ */

static ADC_Ctx_t *adc_ctx_from_id(BSP_ADC_Id_t id)
{
    if (id >= BSP_ADC_COUNT) return NULL;
    return &adc_ctx[id];
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_ADC_Init(BSP_ADC_Id_t id)
{
    if (id >= BSP_ADC_COUNT) return;
    const ADC_HW_Map_t *hw  = &adc_hw_map[id];
    ADC_Ctx_t          *ctx = &adc_ctx[id];

    if (ctx->initialized) return;
    ctx->initialized = 1;

    /* ---- 1. 时钟使能 ---- */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* ---- 2. GPIO: 模拟输入 ---- */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = hw->pin;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(hw->port, &gpio);

    /* ---- 3. DMA 配置 (循环模式, 外设→内存, 半字) ---- */
    ctx->hdma.Instance                 = hw->dma_channel;
    ctx->hdma.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    ctx->hdma.Init.PeriphInc           = DMA_PINC_DISABLE;
    ctx->hdma.Init.MemInc              = DMA_MINC_DISABLE;
    ctx->hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    ctx->hdma.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    ctx->hdma.Init.Mode                = DMA_CIRCULAR;
    ctx->hdma.Init.Priority            = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&ctx->hdma);

    __HAL_LINKDMA(&ctx->hadc, DMA_Handle, ctx->hdma);

    /* ---- 4. ADC 配置 ---- */
    ctx->hadc.Instance                   = hw->instance;
    ctx->hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    ctx->hadc.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    ctx->hadc.Init.ContinuousConvMode    = ENABLE;
    ctx->hadc.Init.NbrOfConversion       = 1;
    ctx->hadc.Init.DiscontinuousConvMode = DISABLE;
    ctx->hadc.Init.NbrOfDiscConversion   = 0;
    ctx->hadc.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    HAL_ADC_Init(&ctx->hadc);

    /* ---- 5. 通道配置 ---- */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = hw->channel;
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    HAL_ADC_ConfigChannel(&ctx->hadc, &sConfig);

    /* ---- 6. 校准 ---- */
    HAL_ADCEx_Calibration_Start(&ctx->hadc);

    /* ---- 7. 启动 DMA 连续转换 ---- */
    HAL_ADC_Start_DMA(&ctx->hadc, (uint32_t *)&ctx->dma_value, 1);
}

void BSP_ADC_Start(BSP_ADC_Id_t id)
{
    ADC_Ctx_t *ctx = adc_ctx_from_id(id);
    if (!ctx || !ctx->initialized) return;
    HAL_ADC_Start_DMA(&ctx->hadc, (uint32_t *)&ctx->dma_value, 1);
}

void BSP_ADC_Stop(BSP_ADC_Id_t id)
{
    ADC_Ctx_t *ctx = adc_ctx_from_id(id);
    if (!ctx || !ctx->initialized) return;
    HAL_ADC_Stop_DMA(&ctx->hadc);
}

uint16_t BSP_ADC_Read_mV(BSP_ADC_Id_t id)
{
    ADC_Ctx_t *ctx = adc_ctx_from_id(id);
    if (!ctx || !ctx->initialized) return 0;
    return (uint16_t)((uint32_t)ctx->dma_value * BSP_ADC_VREF_MV / BSP_ADC_RES_MAX);
}
