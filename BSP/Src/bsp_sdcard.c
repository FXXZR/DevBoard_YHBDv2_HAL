/**
 * @file    bsp_sdcard.c
 * @brief   BSP SD Card 驱动实现 —— SDIO DMA 模式
 * @note    基于 HAL SD 驱动 + LL SDMMC 底层。
 *          MSP 初始化、DMA 配置、ISR 处理、HAL 回调全部在本文件内实现。
 *          对外提供阻塞式扇区读写 API，内部通过 DMA + 轮询完成。
 *
 *          SDIO 引脚 (固定复用):
 *            PC8=D0, PC9=D1, PC10=D2, PC11=D3, PC12=CK, PD2=CMD
 *
 *          DMA: DMA2_Channel4 半双工 (TX/RX 分时复用, 每次传输前重配方向)
 *          IRQ: SDIO_IRQn + DMA2_Channel4_5_IRQn
 */

#include "bsp_sdcard.h"
#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

/* 本地延时封装 — 换平台只需改此两行 */
static inline void delay_ms(uint32_t ms) { BSP_SYSTICK_DelayMS(ms); }
static inline void delay_us(uint32_t us) { BSP_SYSTICK_DelayUS(us); }

/* ================================================================
 * 硬件映射表
 * ================================================================ */
typedef struct {
    SDIO_TypeDef *sdio_instance;
    /* GPIO */
    GPIO_TypeDef *port_c;
    uint16_t      pin_c;        /* PC8-PC12 全部 */
    GPIO_TypeDef *port_d;
    uint16_t      pin_d;        /* PD2 */
    /* DMA */
    DMA_Channel_TypeDef *dma_ch;
} SDCard_HW_Map_t;

static const SDCard_HW_Map_t hw_map[BSP_SDCARD_BUS_COUNT] = {
    [BSP_SDCARD_BUS_1] = {
        .sdio_instance = SDIO,
        .port_c        = GPIOC,
        .pin_c         = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12,
        .port_d        = GPIOD,
        .pin_d         = GPIO_PIN_2,
        .dma_ch        = DMA2_Channel4,
    },
};

/* ================================================================
 * 运行时上下文
 * ================================================================ */
typedef struct {
    SD_HandleTypeDef      hsd;
    DMA_HandleTypeDef     hdma_tx;
    DMA_HandleTypeDef     hdma_rx;
    BSP_SDCARD_CardInfo_t info;
    uint8_t               initialized;
    volatile uint8_t      transfer_done;   /* DMA 传输完成标志 */
    volatile uint8_t      transfer_error;  /* DMA 传输错误标志 */
} SDCard_Ctx_t;

static SDCard_Ctx_t s_ctx[BSP_SDCARD_BUS_COUNT];

/* ================================================================
 * 内部辅助函数声明
 * ================================================================ */
static void              s_dma_config_rx(SDCard_Ctx_t *ctx, const SDCard_HW_Map_t *hw);
static void              s_dma_config_tx(SDCard_Ctx_t *ctx, const SDCard_HW_Map_t *hw);
static BSP_SDCARD_Status_t s_wait_transfer(SDCard_Ctx_t *ctx, uint32_t timeout_ms);
static BSP_SDCARD_Status_t s_read_sector(SDCard_Ctx_t *ctx, uint8_t *buf, uint32_t sector);
static BSP_SDCARD_Status_t s_write_sector(SDCard_Ctx_t *ctx, const uint8_t *buf, uint32_t sector);

/* ================================================================
 * HAL 回调 — 供 ISR 调用
 * ================================================================ */

void HAL_SD_MspInit(SD_HandleTypeDef *hsd);
void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd);

/**
 * @brief  SDIO 全局中断处理 (覆盖 startup 中的弱定义)
 */
void SDIO_IRQHandler(void)
{
    /* 根据 handle 分发 — 遍历上下文找到匹配的 handle */
    for (int i = 0; i < BSP_SDCARD_BUS_COUNT; i++) {
        if (s_ctx[i].initialized) {
            HAL_SD_IRQHandler(&s_ctx[i].hsd);
        }
    }
}

/**
 * @brief  DMA2 Channel4/5 全局中断处理 (覆盖 startup 中的弱定义)
 * @note   SDIO 仅使用 DMA2_Channel4 (TX/RX 分时复用), Channel5 保留
 */
void DMA2_Channel4_5_IRQHandler(void)
{
    for (int i = 0; i < BSP_SDCARD_BUS_COUNT; i++) {
        if (s_ctx[i].initialized) {
            HAL_DMA_IRQHandler(s_ctx[i].hsd.hdmatx);
            HAL_DMA_IRQHandler(s_ctx[i].hsd.hdmarx);
        }
    }
}

/* ---- HAL SD 传输完成回调 ---- */

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    for (int i = 0; i < BSP_SDCARD_BUS_COUNT; i++) {
        if (&s_ctx[i].hsd == hsd) {
            s_ctx[i].transfer_done = 1;
            return;
        }
    }
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    for (int i = 0; i < BSP_SDCARD_BUS_COUNT; i++) {
        if (&s_ctx[i].hsd == hsd) {
            s_ctx[i].transfer_done = 1;
            return;
        }
    }
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    for (int i = 0; i < BSP_SDCARD_BUS_COUNT; i++) {
        if (&s_ctx[i].hsd == hsd) {
            s_ctx[i].transfer_error = 1;
            return;
        }
    }
}

void HAL_SD_AbortCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    /* 预留, 暂不处理 */
}

/* ================================================================
 * MSP 初始化 / 反初始化
 * ================================================================ */

/**
 * @brief  SD MSP 初始化 — GPIO + 时钟 + DMA + NVIC
 * @note   由 HAL_SD_Init() 内部自动调用
 */
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    (void)hsd;

    /* 1. 使能外设时钟 */
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* 2. 配置 SDIO GPIO (AF_PP + 上拉 + 高速) */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* GPIOC: D0(PC8) | D1(PC9) | D2(PC10) | D3(PC11) | CK(PC12) */
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* GPIOD: CMD(PD2) */
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* 3. NVIC 配置 (DMA 配置由 s_dma_config_rx/tx 按需进行) */
    /*    此处仅设置中断优先级并使能 */
    HAL_NVIC_SetPriority(DMA2_Channel4_5_IRQn, 0x0D, 0);
    HAL_NVIC_EnableIRQ(DMA2_Channel4_5_IRQn);

    HAL_NVIC_SetPriority(SDIO_IRQn, 0x0C, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
}

/**
 * @brief  SD MSP 反初始化
 */
void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{
    (void)hsd;

    HAL_NVIC_DisableIRQ(SDIO_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Channel4_5_IRQn);

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);

    __HAL_RCC_SDIO_CLK_DISABLE();
}

/* ================================================================
 * 内部辅助函数
 * ================================================================ */

/**
 * @brief  配置 DMA2_Channel4 为 RX 方向 (Periph → Memory)
 */
static void s_dma_config_rx(SDCard_Ctx_t *ctx, const SDCard_HW_Map_t *hw)
{
    /* 先断开旧的 DMA 链接, 中止并反初始化 */
    if (ctx->hsd.hdmarx != NULL) {
        HAL_DMA_Abort(ctx->hsd.hdmarx);
        HAL_DMA_DeInit(ctx->hsd.hdmarx);
    }
    ctx->hsd.hdmarx = NULL;

    DMA_HandleTypeDef *hdma = &ctx->hdma_rx;
    hdma->Instance                 = hw->dma_ch;
    hdma->Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma->Init.MemInc              = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma->Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    hdma->Init.Mode                = DMA_NORMAL;
    hdma->Init.Priority            = DMA_PRIORITY_VERY_HIGH;

    HAL_DMA_Init(hdma);
    __HAL_LINKDMA(&ctx->hsd, hdmarx, ctx->hdma_rx);
}

/**
 * @brief  配置 DMA2_Channel4 为 TX 方向 (Memory → Periph)
 */
static void s_dma_config_tx(SDCard_Ctx_t *ctx, const SDCard_HW_Map_t *hw)
{
    if (ctx->hsd.hdmatx != NULL) {
        HAL_DMA_Abort(ctx->hsd.hdmatx);
        HAL_DMA_DeInit(ctx->hsd.hdmatx);
    }
    ctx->hsd.hdmatx = NULL;

    DMA_HandleTypeDef *hdma = &ctx->hdma_tx;
    hdma->Instance                 = hw->dma_ch;
    hdma->Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma->Init.MemInc              = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma->Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    hdma->Init.Mode                = DMA_NORMAL;
    hdma->Init.Priority            = DMA_PRIORITY_VERY_HIGH;

    HAL_DMA_Init(hdma);
    __HAL_LINKDMA(&ctx->hsd, hdmatx, ctx->hdma_tx);
}

/**
 * @brief  等待 DMA 传输完成 (轮询 transfer_done 标志)
 */
static BSP_SDCARD_Status_t s_wait_transfer(SDCard_Ctx_t *ctx, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;

    while (!ctx->transfer_done && !ctx->transfer_error) {
        if (elapsed >= timeout_ms) {
            return BSP_SDCARD_TIMEOUT;
        }
        delay_us(1000);   /* 1ms 轮询 */
        elapsed++;
    }

    if (ctx->transfer_error) {
        ctx->transfer_error = 0;
        return BSP_SDCARD_ERROR;
    }

    ctx->transfer_done = 0;
    return BSP_SDCARD_OK;
}

/**
 * @brief  读取单个 512 字节逻辑扇区 (处理 SDSC/SDHC 地址差异)
 * @note   对 SDSC (BlockSize > 512) 使用 bounce buffer 避免 HAL 多读溢出
 *         对 SDHC (BlockSize = 512) 直接使用 DMA
 */
static BSP_SDCARD_Status_t s_read_sector(SDCard_Ctx_t *ctx, uint8_t *buf, uint32_t sector)
{
    uint32_t block_size = ctx->info.block_size;

    if (block_size == 512) {
        /* SDHC: LBA = block address, 1 block = 512 bytes → 直接 DMA */
        s_dma_config_rx(ctx, &hw_map[0]);
        if (HAL_SD_ReadBlocks_DMA(&ctx->hsd, buf, sector, 1) != HAL_OK) {
            return BSP_SDCARD_ERROR;
        }
        return s_wait_transfer(ctx, BSP_SDCARD_TIMEOUT_READ);
    }

    /* SDSC: 强制 512-byte 块传输 — 临时设 BlockSize=512 触发 HAL
     * 发送 CMD16(SET_BLOCKLEN=512). 使用 polling 避免 DMA 状态冲突. */
    uint32_t saved_bs = ctx->hsd.SdCard.BlockSize;
    ctx->hsd.SdCard.BlockSize = 512;
    uint32_t byte_addr = sector * 512;
    HAL_StatusTypeDef hal_ret = HAL_SD_ReadBlocks(&ctx->hsd, buf, byte_addr, 1, BSP_SDCARD_TIMEOUT_READ);
    BSP_SDCARD_Status_t ret = (hal_ret == HAL_OK) ? BSP_SDCARD_OK : BSP_SDCARD_ERROR;
    ctx->hsd.SdCard.BlockSize = saved_bs;
    return ret;
}

/**
 * @brief  写入单个 512 字节逻辑扇区
 */
static BSP_SDCARD_Status_t s_write_sector(SDCard_Ctx_t *ctx, const uint8_t *buf, uint32_t sector)
{
    uint32_t block_size = ctx->info.block_size;

    if (block_size == 512) {
        /* SDHC: 直接 DMA 写 */
        s_dma_config_tx(ctx, &hw_map[0]);
        if (HAL_SD_WriteBlocks_DMA(&ctx->hsd, (uint8_t *)buf, sector, 1) != HAL_OK) {
            return BSP_SDCARD_ERROR;
        }
        return s_wait_transfer(ctx, BSP_SDCARD_TIMEOUT_WRITE);
    }

    /* SDSC: 强制 512-byte 块传输 — CMD16(SET_BLOCKLEN=512) + polling 写. */
    uint32_t saved_bs = ctx->hsd.SdCard.BlockSize;
    ctx->hsd.SdCard.BlockSize = 512;
    uint32_t byte_addr = sector * 512;
    HAL_StatusTypeDef hal_ret = HAL_SD_WriteBlocks(&ctx->hsd, (uint8_t *)buf, byte_addr, 1, BSP_SDCARD_TIMEOUT_WRITE);
    ctx->hsd.SdCard.BlockSize = saved_bs;
    if (hal_ret != HAL_OK) return BSP_SDCARD_ERROR;

    /* 写后必须轮询卡状态 (CMD13) 直到退出编程态. */
    {
        uint32_t elapsed    = 0;  /* ms */
        uint32_t rca        = ctx->hsd.SdCard.RelCardAdd;
        SDIO_CmdInitTypeDef sdio_cmd = {
            .Argument         = (uint32_t)(rca << 16),
            .CmdIndex         = 13,  /* SEND_STATUS */
            .Response         = SDIO_RESPONSE_SHORT,
            .WaitForInterrupt = SDIO_WAIT_NO,
            .CPSM             = SDIO_CPSM_ENABLE,
        };
        do {
            SDIO_SendCommand(ctx->hsd.Instance, &sdio_cmd);
            /* 等待命令响应完成 (50ms 超时) */
            uint32_t cmd_elapsed = 0;
            while (!__HAL_SD_GET_FLAG(&ctx->hsd, SDIO_FLAG_CMDREND)) {
                if (cmd_elapsed >= 50) break;
                delay_us(100);
                cmd_elapsed++;   /* ≈ 0.1ms */
            }
            __HAL_SD_CLEAR_FLAG(&ctx->hsd, SDIO_STATIC_FLAGS);
            uint32_t resp  = SDIO_GetResponse(ctx->hsd.Instance, SDIO_RESP1);
            uint32_t state = (resp >> 9) & 0x0F;
            if (state == 4) break;  /* 4 = Transfer state */
            if (elapsed >= BSP_SDCARD_TIMEOUT_WRITE) return BSP_SDCARD_TIMEOUT;
            delay_ms(5);
            elapsed += 5;
        } while (1);
    }
    return BSP_SDCARD_OK;
}

/* ================================================================
 * 公共 API
 * ================================================================ */

/**
 * @brief  初始化 SDIO 外设并检测/初始化 SD 卡
 */
BSP_SDCARD_Status_t BSP_SDCARD_Init(BSP_SDCARD_Bus_Id_t id)
{
    if (id >= BSP_SDCARD_BUS_COUNT) return BSP_SDCARD_ERROR;

    SDCard_Ctx_t *ctx = &s_ctx[id];
    const SDCard_HW_Map_t *hw = &hw_map[id];

    /* 幂等: 已初始化直接返回 */
    if (ctx->initialized) return BSP_SDCARD_OK;

    /* ---- 1. 配置 SDIO 初始化参数 ---- */
    ctx->hsd.Instance = hw->sdio_instance;

    ctx->hsd.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;
    ctx->hsd.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
    ctx->hsd.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
    ctx->hsd.Init.BusWide             = SDIO_BUS_WIDE_1B;
    ctx->hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    ctx->hsd.Init.ClockDiv            = BSP_SDCARD_CLK_DIV_INIT;  /* ~400KHz @ 72MHz HCLK */

    /* ---- 2. HAL SD 初始化 (内部调用 HAL_SD_MspInit) ---- */
    if (HAL_SD_Init(&ctx->hsd) != HAL_OK) {
        ctx->initialized = 0;
        return BSP_SDCARD_NO_CARD;
    }

    /* ---- 3. 配置 4-bit 宽总线模式 (同时提升时钟到 24MHz) ---- */
    if (HAL_SD_ConfigWideBusOperation(&ctx->hsd, SDIO_BUS_WIDE_4B) != HAL_OK) {
        HAL_SD_DeInit(&ctx->hsd);
        ctx->initialized = 0;
        return BSP_SDCARD_ERROR;
    }

    /* ---- 4. 读取并缓存卡信息 ---- */
    HAL_SD_CardInfoTypeDef hal_info;
    if (HAL_SD_GetCardInfo(&ctx->hsd, &hal_info) != HAL_OK) {
        HAL_SD_DeInit(&ctx->hsd);
        ctx->initialized = 0;
        return BSP_SDCARD_ERROR;
    }

    ctx->info.block_size      = hal_info.BlockSize;
    ctx->info.block_count     = hal_info.BlockNbr;
    ctx->info.block_count_log = hal_info.LogBlockNbr;
    ctx->info.block_size_log  = hal_info.LogBlockSize;
    ctx->info.card_type       = hal_info.CardType;
    ctx->info.card_version    = hal_info.CardVersion;

    ctx->transfer_done  = 0;
    ctx->transfer_error = 0;
    ctx->initialized    = 1;

    return BSP_SDCARD_OK;
}

/**
 * @brief  从 SD 卡读取扇区 (阻塞, 自动处理 SDSC/SDHC 差异)
 */
BSP_SDCARD_Status_t BSP_SDCARD_Read(BSP_SDCARD_Bus_Id_t id, uint8_t *buf,
                                    uint32_t sector_addr, uint32_t num_sectors)
{
    if (id >= BSP_SDCARD_BUS_COUNT) return BSP_SDCARD_ERROR;

    SDCard_Ctx_t *ctx = &s_ctx[id];
    if (!ctx->initialized) return BSP_SDCARD_ERROR;

    /* 逐扇区读取 (s_read_sector 内部处理 SDSC bounce / SDHC DMA) */
    for (uint32_t i = 0; i < num_sectors; i++) {
        BSP_SDCARD_Status_t st = s_read_sector(ctx, buf + i * 512, sector_addr + i);
        if (st != BSP_SDCARD_OK) return st;
    }
    return BSP_SDCARD_OK;
}

/**
 * @brief  向 SD 卡写入扇区 (阻塞, 自动处理 SDSC/SDHC 差异)
 */
BSP_SDCARD_Status_t BSP_SDCARD_Write(BSP_SDCARD_Bus_Id_t id, const uint8_t *buf,
                                     uint32_t sector_addr, uint32_t num_sectors)
{
    if (id >= BSP_SDCARD_BUS_COUNT) return BSP_SDCARD_ERROR;

    SDCard_Ctx_t *ctx = &s_ctx[id];
    if (!ctx->initialized) return BSP_SDCARD_ERROR;

    /* 逐扇区写入 */
    for (uint32_t i = 0; i < num_sectors; i++) {
        BSP_SDCARD_Status_t st = s_write_sector(ctx, buf + i * 512, sector_addr + i);
        if (st != BSP_SDCARD_OK) return st;
    }
    return BSP_SDCARD_OK;
}

/**
 * @brief  获取 SD 卡信息
 */
BSP_SDCARD_Status_t BSP_SDCARD_GetInfo(BSP_SDCARD_Bus_Id_t id, BSP_SDCARD_CardInfo_t *info)
{
    if (id >= BSP_SDCARD_BUS_COUNT || info == NULL) return BSP_SDCARD_ERROR;

    SDCard_Ctx_t *ctx = &s_ctx[id];
    if (!ctx->initialized) return BSP_SDCARD_ERROR;

    *info = ctx->info;
    return BSP_SDCARD_OK;
}

/**
 * @brief  检查 SD 卡是否就绪
 */
uint8_t BSP_SDCARD_IsReady(BSP_SDCARD_Bus_Id_t id)
{
    if (id >= BSP_SDCARD_BUS_COUNT) return 0;
    return s_ctx[id].initialized ? 1 : 0;
}

/**
 * @brief  反初始化 SDIO 外设
 */
BSP_SDCARD_Status_t BSP_SDCARD_DeInit(BSP_SDCARD_Bus_Id_t id)
{
    if (id >= BSP_SDCARD_BUS_COUNT) return BSP_SDCARD_ERROR;

    SDCard_Ctx_t *ctx = &s_ctx[id];
    if (!ctx->initialized) return BSP_SDCARD_OK;

    HAL_SD_DeInit(&ctx->hsd);
    ctx->initialized   = 0;
    ctx->transfer_done = 0;
    ctx->transfer_error = 0;

    return BSP_SDCARD_OK;
}
