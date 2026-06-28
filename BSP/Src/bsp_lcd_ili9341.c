/**
 * @file    bsp_lcd_ili9341.c
 * @brief   BSP LCD 驱动实现 — ILI9341/ST7789V via FSMC 8080 并口
 * @note    FSMC Bank1 Sector4 (NE4), 基地址 0x6C000000.
 *
 *          地址技巧: A23 → LCD DC 线
 *            CMD 地址  = 0x6C000000  (A23=0 → DC=0 → 写命令寄存器)
 *            DATA 地址 = 0x6D000000  (A23=1 → DC=1 → 写数据寄存器)
 *
 *          FSMC 时序模式 B, 16 位数据总线.
 *
 *          适配自野火霸道 V2 参考驱动.
 */

#include "bsp_lcd_ili9341.h"
#include "bsp_fonts.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * FSMC 地址映射
 * ================================================================ */

enum {
    FSMC_LCD_CMD_ADDR  = 0x6C000000,   /* A23=0 → LCD DC=0 (命令) */
    FSMC_LCD_DATA_ADDR = 0x6D000000,   /* A23=1 → LCD DC=1 (数据) */
};

/* ================================================================
 * FSMC 引脚枚举 — 命名索引
 * ================================================================ */

typedef enum {
    /* 数据总线 D0~D15 */
    LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
    LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
    LCD_PIN_D8, LCD_PIN_D9, LCD_PIN_D10, LCD_PIN_D11,
    LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,

    /* 控制/专用 */
    LCD_PIN_NE4,            /* FSMC CS (PG12) */
    LCD_PIN_NOE,            /* FSMC NOE (PD4) — 读使能, ReadID 等需要 */
    LCD_PIN_NWE,            /* FSMC NWE (PD5) — 写使能, 最关键: 缺失则 LCD 无法锁存数据 */
    LCD_PIN_A23,            /* FSMC A23 (PE2) — LCD DC 线 */

    LCD_PIN_COUNT
} LCD_PinID_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} LCD_Pin_t;

static const LCD_Pin_t lcd_fsmc_pins[LCD_PIN_COUNT] = {
    /* ---- 数据总线 D0~D15 (与 SRAM 共用, 各自独立配置) ---- */
    [LCD_PIN_D0]  = {GPIOD, GPIO_PIN_14},
    [LCD_PIN_D1]  = {GPIOD, GPIO_PIN_15},
    [LCD_PIN_D2]  = {GPIOD, GPIO_PIN_0},
    [LCD_PIN_D3]  = {GPIOD, GPIO_PIN_1},
    [LCD_PIN_D4]  = {GPIOE, GPIO_PIN_7},
    [LCD_PIN_D5]  = {GPIOE, GPIO_PIN_8},
    [LCD_PIN_D6]  = {GPIOE, GPIO_PIN_9},
    [LCD_PIN_D7]  = {GPIOE, GPIO_PIN_10},
    [LCD_PIN_D8]  = {GPIOE, GPIO_PIN_11},
    [LCD_PIN_D9]  = {GPIOE, GPIO_PIN_12},
    [LCD_PIN_D10] = {GPIOE, GPIO_PIN_13},
    [LCD_PIN_D11] = {GPIOE, GPIO_PIN_14},
    [LCD_PIN_D12] = {GPIOE, GPIO_PIN_15},
    [LCD_PIN_D13] = {GPIOD, GPIO_PIN_8},
    [LCD_PIN_D14] = {GPIOD, GPIO_PIN_9},
    [LCD_PIN_D15] = {GPIOD, GPIO_PIN_10},

    /* ---- 控制/专用 ---- */
    [LCD_PIN_NE4] = {GPIOG, GPIO_PIN_12},   /* FSMC NE4 */
    [LCD_PIN_NOE] = {GPIOD, GPIO_PIN_4},    /* FSMC NOE */
    [LCD_PIN_NWE] = {GPIOD, GPIO_PIN_5},    /* FSMC NWE */
    [LCD_PIN_A23] = {GPIOE, GPIO_PIN_2},    /* FSMC A23 → DC */
};

/* 非 FSMC 引脚: RST (PG11, 推挽输出), BL (PG6, 推挽输出) */
#define LCD_RST_PORT  GPIOG
#define LCD_RST_PIN   GPIO_PIN_11
#define LCD_BL_PORT   GPIOG
#define LCD_BL_PIN    GPIO_PIN_6

/* ================================================================
 * 运行时上下文
 * ================================================================ */

typedef struct {
    SRAM_HandleTypeDef handle;
    uint8_t            initialized;
    uint16_t           id;                    /* 控制器 ID */
    BSP_LCD_ScanMode_t scan_mode;
    uint16_t           x_length;             /* 当前 X 方向像素数 */
    uint16_t           y_length;             /* 当前 Y 方向像素数 */
} LCD_Ctx_t;

static LCD_Ctx_t lcd_ctx;

/* ================================================================
 * 底层硬件访问 (内联)
 * ================================================================ */

static __inline void lcd_write_cmd(uint16_t cmd)
{
    *(__IO uint16_t *)FSMC_LCD_CMD_ADDR = cmd;
}

static __inline void lcd_write_data(uint16_t data)
{
    *(__IO uint16_t *)FSMC_LCD_DATA_ADDR = data;
}

static __inline uint16_t lcd_read_data(void)
{
    return *(__IO uint16_t *)FSMC_LCD_DATA_ADDR;
}

static void lcd_delay(__IO uint32_t n)
{
    for (; n != 0; n--) { }
}

/* ================================================================
 * GPIO 初始化
 * ================================================================ */

/**
 * @brief  统一初始化 LCD 的 FSMC 引脚 + RST + 背光
 * @note   所有 FSMC 引脚配置相同 (AF_PP/上拉/高速), 逐引脚 HAL_GPIO_Init.
 */
static void lcd_init_fsmc_gpio(void)
{
    /* 时钟 */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* FSMC 引脚: AF_PP */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    LCD_PinID_t id;
    for (id = 0; id < LCD_PIN_COUNT; id++) {
        gpio.Pin = lcd_fsmc_pins[id].pin;
        HAL_GPIO_Init(lcd_fsmc_pins[id].port, &gpio);
    }

    /* RST (PG11) — 推挽输出, 高电平 */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pin  = LCD_RST_PIN;
    HAL_GPIO_Init(LCD_RST_PORT, &gpio);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);

    /* 背光 (PG6) — 推挽输出, 默认低电平=点亮 (低有效) */
    gpio.Pin = LCD_BL_PIN;
    HAL_GPIO_Init(LCD_BL_PORT, &gpio);
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);
}

/* ================================================================
 * FSMC Bank4 配置
 * ================================================================ */

static void lcd_init_fsmc(void)
{
    /* FSMC 时序 — 匹配野火标准库参考驱动, HAL 算术适配
     * ADDSET=1 → 地址建立 2 HCLK
     * DATAST=4 → 写使能 (4+1)=5 HCLK≈69ns (ILI9341 写≥66ns → 余量 OK)
     * CLKDivision=2 → HAL算术: (2-1)<<20=0x00100000 (异步模式不使用)
     * DataLatency=2  → HAL算术: (2-2)<<24=0x00000000 (异步模式不使用)
     */
    FSMC_NORSRAM_TimingTypeDef timing = {0};
    timing.AddressSetupTime      = 0x01;
    timing.AddressHoldTime       = 0x00;
    timing.DataSetupTime         = 0x04;
    timing.BusTurnAroundDuration = 0x00;
    timing.CLKDivision           = 0x00;
    timing.DataLatency           = 0x00;
    timing.AccessMode            = FSMC_ACCESS_MODE_B;   /* LCD 用模式 B */

    lcd_ctx.handle.Instance               = FSMC_NORSRAM_DEVICE;
    lcd_ctx.handle.Extended               = FSMC_NORSRAM_EXTENDED_DEVICE;
    lcd_ctx.handle.Init.NSBank            = FSMC_NORSRAM_BANK4;
    lcd_ctx.handle.Init.DataAddressMux    = FSMC_DATA_ADDRESS_MUX_DISABLE;
    lcd_ctx.handle.Init.MemoryType        = FSMC_MEMORY_TYPE_NOR;
    lcd_ctx.handle.Init.MemoryDataWidth   = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    lcd_ctx.handle.Init.BurstAccessMode   = FSMC_BURST_ACCESS_MODE_DISABLE;
    lcd_ctx.handle.Init.WaitSignalPolarity= FSMC_WAIT_SIGNAL_POLARITY_LOW;
    lcd_ctx.handle.Init.WrapMode          = FSMC_WRAP_MODE_DISABLE;
    lcd_ctx.handle.Init.WaitSignalActive  = FSMC_WAIT_TIMING_BEFORE_WS;
    lcd_ctx.handle.Init.WriteOperation    = FSMC_WRITE_OPERATION_ENABLE;
    lcd_ctx.handle.Init.WaitSignal        = FSMC_WAIT_SIGNAL_DISABLE;
    lcd_ctx.handle.Init.ExtendedMode      = FSMC_EXTENDED_MODE_DISABLE;
    lcd_ctx.handle.Init.AsynchronousWait  = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    lcd_ctx.handle.Init.WriteBurst        = FSMC_WRITE_BURST_DISABLE;
    lcd_ctx.handle.Init.PageSize          = FSMC_PAGE_SIZE_NONE;

    HAL_SRAM_Init(&lcd_ctx.handle, &timing, &timing);
}

/* ================================================================
 * LCD 硬件复位
 * ================================================================ */

static void lcd_hw_reset(void)
{
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    lcd_delay(0xAFF);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    lcd_delay(0xAFF);
}

/* ================================================================
 * ILI9341 寄存器初始化序列
 * ================================================================ */

static void lcd_reg_config_ili9341(void)
{
    /* Power control B (CFh) */
    lcd_write_cmd(0xCF);
    lcd_write_data(0x00); lcd_write_data(0x81); lcd_write_data(0x30);

    /* Power on sequence control (EDh) */
    lcd_write_cmd(0xED);
    lcd_write_data(0x64); lcd_write_data(0x03);
    lcd_write_data(0x12); lcd_write_data(0x81);

    /* Driver timing control A (E8h) */
    lcd_write_cmd(0xE8);
    lcd_write_data(0x85); lcd_write_data(0x10); lcd_write_data(0x78);

    /* Power control A (CBh) */
    lcd_write_cmd(0xCB);
    lcd_write_data(0x39); lcd_write_data(0x2C);
    lcd_write_data(0x00); lcd_write_data(0x34);
    lcd_write_data(0x06);

    /* Pump ratio control (F7h) */
    lcd_write_cmd(0xF7);
    lcd_write_data(0x20);

    /* Driver timing control B (EAh) */
    lcd_write_cmd(0xEA);
    lcd_write_data(0x00); lcd_write_data(0x00);

    /* Frame Rate Control (B1h) */
    lcd_write_cmd(0xB1);
    lcd_write_data(0x00); lcd_write_data(0x1B);

    /* Display Function Control (B6h) */
    lcd_write_cmd(0xB6);
    lcd_write_data(0x0A); lcd_write_data(0xA2);

    /* Power Control 1 (C0h) */
    lcd_write_cmd(0xC0);
    lcd_write_data(0x35);

    /* Power Control 2 (C1h) */
    lcd_write_cmd(0xC1);
    lcd_write_data(0x11);

    /* VCOM Control 1 (C5h) */
    lcd_write_cmd(0xC5);
    lcd_write_data(0x45); lcd_write_data(0x45);

    /* VCOM Control 2 (C7h) */
    lcd_write_cmd(0xC7);
    lcd_write_data(0xA2);

    /* Enable 3G (F2h) */
    lcd_write_cmd(0xF2);
    lcd_write_data(0x00);

    /* Gamma Set (26h) */
    lcd_write_cmd(0x26);
    lcd_write_data(0x01);

    /* Positive Gamma Correction (E0h) */
    lcd_write_cmd(0xE0);
    lcd_write_data(0x0F); lcd_write_data(0x26); lcd_write_data(0x24);
    lcd_write_data(0x0B); lcd_write_data(0x0E); lcd_write_data(0x09);
    lcd_write_data(0x54); lcd_write_data(0xA8); lcd_write_data(0x46);
    lcd_write_data(0x0C); lcd_write_data(0x17); lcd_write_data(0x09);
    lcd_write_data(0x0F); lcd_write_data(0x07); lcd_write_data(0x00);

    /* Negative Gamma Correction (E1h) */
    lcd_write_cmd(0xE1);
    lcd_write_data(0x00); lcd_write_data(0x19); lcd_write_data(0x1B);
    lcd_write_data(0x04); lcd_write_data(0x10); lcd_write_data(0x07);
    lcd_write_data(0x2A); lcd_write_data(0x47); lcd_write_data(0x39);
    lcd_write_data(0x03); lcd_write_data(0x06); lcd_write_data(0x06);
    lcd_write_data(0x30); lcd_write_data(0x38); lcd_write_data(0x0F);

    /* Memory Access Control (36h) — 默认扫描方向 */
    lcd_write_cmd(0x36);
    lcd_write_data(0xC8);

    /* Column Address Set (2Ah) */
    lcd_write_cmd(0x2A);
    lcd_write_data(0x00); lcd_write_data(0x00);
    lcd_write_data(0x00); lcd_write_data(0xEF);

    /* Page Address Set (2Bh) */
    lcd_write_cmd(0x2B);
    lcd_write_data(0x00); lcd_write_data(0x00);
    lcd_write_data(0x01); lcd_write_data(0x3F);

    /* Pixel Format Set (3Ah) — 16bit/pixel */
    lcd_write_cmd(0x3A);
    lcd_write_data(0x55);

    /* Sleep Out (11h) */
    lcd_write_cmd(0x11);
    lcd_delay(0xAFF << 2);

    /* Display ON (29h) */
    lcd_write_cmd(0x29);
}

/* ================================================================
 * ST7789V 寄存器初始化序列
 * ================================================================ */

static void lcd_reg_config_st7789v(void)
{
    lcd_write_cmd(0xCF);
    lcd_write_data(0x00); lcd_write_data(0xC1); lcd_write_data(0x30);

    lcd_write_cmd(0xED);
    lcd_write_data(0x64); lcd_write_data(0x03);
    lcd_write_data(0x12); lcd_write_data(0x81);

    lcd_write_cmd(0xE8);
    lcd_write_data(0x85); lcd_write_data(0x10); lcd_write_data(0x78);

    lcd_write_cmd(0xCB);
    lcd_write_data(0x39); lcd_write_data(0x2C);
    lcd_write_data(0x00); lcd_write_data(0x34);
    lcd_write_data(0x02);

    lcd_write_cmd(0xF7);
    lcd_write_data(0x20);

    lcd_write_cmd(0xEA);
    lcd_write_data(0x00); lcd_write_data(0x00);

    lcd_write_cmd(0xC0);
    lcd_write_data(0x21);

    lcd_write_cmd(0xC1);
    lcd_write_data(0x11);

    lcd_write_cmd(0xC5);
    lcd_write_data(0x2D); lcd_write_data(0x33);

    lcd_write_cmd(0x36);
    lcd_write_data(0x00);

    lcd_write_cmd(0x3A);
    lcd_write_data(0x55);

    lcd_write_cmd(0xB1);
    lcd_write_data(0x00); lcd_write_data(0x17);

    lcd_write_cmd(0xB6);
    lcd_write_data(0x0A); lcd_write_data(0xA2);

    lcd_write_cmd(0xF6);
    lcd_write_data(0x01); lcd_write_data(0x30);

    lcd_write_cmd(0xF2);
    lcd_write_data(0x00);

    lcd_write_cmd(0x26);
    lcd_write_data(0x01);

    /* Positive Gamma */
    lcd_write_cmd(0xE0);
    lcd_write_data(0xD0); lcd_write_data(0x00); lcd_write_data(0x02);
    lcd_write_data(0x07); lcd_write_data(0x0B); lcd_write_data(0x1A);
    lcd_write_data(0x31); lcd_write_data(0x54); lcd_write_data(0x40);
    lcd_write_data(0x29); lcd_write_data(0x12); lcd_write_data(0x12);
    lcd_write_data(0x12); lcd_write_data(0x17);

    /* Negative Gamma */
    lcd_write_cmd(0xE1);
    lcd_write_data(0xD0); lcd_write_data(0x00); lcd_write_data(0x02);
    lcd_write_data(0x07); lcd_write_data(0x05); lcd_write_data(0x25);
    lcd_write_data(0x2D); lcd_write_data(0x44); lcd_write_data(0x45);
    lcd_write_data(0x1C); lcd_write_data(0x18); lcd_write_data(0x16);
    lcd_write_data(0x1C); lcd_write_data(0x1D);

    lcd_write_cmd(0x11);
    lcd_delay(0xAFF << 2);

    lcd_write_cmd(0x29);
    lcd_write_cmd(0x2C);
}

/* ================================================================
 * 内部辅助: 开窗 + 光标
 * ================================================================ */

static void lcd_open_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    lcd_write_cmd(0x2A);   /* Column Address Set */
    lcd_write_data(x >> 8);
    lcd_write_data(x & 0xFF);
    lcd_write_data((x + w - 1) >> 8);
    lcd_write_data((x + w - 1) & 0xFF);

    lcd_write_cmd(0x2B);   /* Page Address Set */
    lcd_write_data(y >> 8);
    lcd_write_data(y & 0xFF);
    lcd_write_data((y + h - 1) >> 8);
    lcd_write_data((y + h - 1) & 0xFF);
}

static void lcd_set_cursor(uint16_t x, uint16_t y)
{
    lcd_open_window(x, y, 1, 1);
}

/* ================================================================
 * 公开 API — 初始化
 * ================================================================ */

void BSP_LCD_Init(void)
{
    if (lcd_ctx.initialized) return;
    lcd_ctx.initialized = 1;

    /* 1. 使能 FSMC 时钟 */
    __HAL_RCC_FSMC_CLK_ENABLE();

    /* 2. GPIO */
    lcd_init_fsmc_gpio();

    /* 3. FSMC Bank4 配置 */
    lcd_init_fsmc();

    /* 4. 背光开 */
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);

    /* 5. 硬件复位 */
    lcd_hw_reset();

    /* 6. 读取 ID 并加载寄存器序列 */
    lcd_ctx.id = BSP_LCD_ReadID();

    if (lcd_ctx.id == BSP_LCD_ID_ILI9341) {
        lcd_reg_config_ili9341();
    } else if (lcd_ctx.id == BSP_LCD_ID_ST7789V) {
        lcd_reg_config_st7789v();
    } else {
        /* 未知控制器, 尝试 ILI9341 序列 */
        lcd_reg_config_ili9341();
    }

    /* 7. 默认扫描方向 */
    BSP_LCD_SetScanMode(BSP_LCD_SCAN_6);

    /* 8. 清屏为黑色 */
    BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);
}

uint16_t BSP_LCD_ReadID(void)
{
    uint16_t id = 0;

    /* 尝试 ST7789V ID (寄存器 0x04: 2 dummy + 2 ID bytes = 0x8552)
     * 注: 2个dummy与野火标准库参考驱动一致 */
    lcd_write_cmd(0x04);
    lcd_read_data();                /* 1st param: dummy */
    lcd_read_data();                /* 2nd param: dummy (参考驱动原样) */
    id  = (uint16_t)lcd_read_data() << 8;   /* 3rd param: 0x85 */
    id |= lcd_read_data();          /* 4th param: 0x52 */

    if (id == BSP_LCD_ID_ST7789V) {
        return id;
    }

    /* 尝试 ILI9341 ID (寄存器 0xD3: 2 dummy + 2 ID bytes = 0x9341) */
    lcd_write_cmd(0xD3);
    lcd_read_data();                /* 1st param: dummy */
    lcd_read_data();                /* 2nd param: dummy (参考驱动原样) */
    id  = (uint16_t)lcd_read_data() << 8;   /* 3rd param: 0x93 */
    id |= lcd_read_data();          /* 4th param: 0x41 */

    if (id == BSP_LCD_ID_ILI9341) {
        return id;
    }

    return BSP_LCD_ID_UNKNOWN;
}

void BSP_LCD_BackLight(uint8_t on)
{
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void BSP_LCD_SetScanMode(BSP_LCD_ScanMode_t mode)
{
    if (mode > 7) return;

    lcd_ctx.scan_mode = mode;

    /* 根据模式更新 X/Y 长度 */
    if (mode % 2 == 0) {
        /* 0, 2, 4, 6: 横屏 */
        lcd_ctx.x_length = BSP_LCD_WIDTH;
        lcd_ctx.y_length = BSP_LCD_HEIGHT;
    } else {
        /* 1, 3, 5, 7: 竖屏 */
        lcd_ctx.x_length = BSP_LCD_HEIGHT;
        lcd_ctx.y_length = BSP_LCD_WIDTH;
    }

    /* 写 MADCTL 寄存器 */
    lcd_write_cmd(0x36);

    if (lcd_ctx.id == BSP_LCD_ID_ST7789V) {
        lcd_write_data(0x00 | ((uint16_t)mode << 5));
    } else {
        lcd_write_data(0x08 | ((uint16_t)mode << 5));
    }

    /* 更新全屏窗口 */
    lcd_write_cmd(0x2A);
    lcd_write_data(0x00); lcd_write_data(0x00);
    lcd_write_data(((lcd_ctx.x_length - 1) >> 8) & 0xFF);
    lcd_write_data((lcd_ctx.x_length - 1) & 0xFF);

    lcd_write_cmd(0x2B);
    lcd_write_data(0x00); lcd_write_data(0x00);
    lcd_write_data(((lcd_ctx.y_length - 1) >> 8) & 0xFF);
    lcd_write_data((lcd_ctx.y_length - 1) & 0xFF);

    /* 启动 GRAM 写入 */
    lcd_write_cmd(0x2C);
}

/* ================================================================
 * 公开 API — 基本绘图
 * ================================================================ */

void BSP_LCD_SetPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= lcd_ctx.x_length || y >= lcd_ctx.y_length) return;

    lcd_set_cursor(x, y);
    lcd_write_cmd(0x2C);
    lcd_write_data(color);
}

uint16_t BSP_LCD_GetPixel(uint16_t x, uint16_t y)
{
    uint16_t r, g;

    if (x >= lcd_ctx.x_length || y >= lcd_ctx.y_length) return 0;

    lcd_set_cursor(x, y);
    lcd_write_cmd(0x2E);          /* Read Memory Start */
    lcd_read_data();              /* Dummy read */
    r = lcd_read_data();          /* R + G (high 8 bits) */
    g = lcd_read_data();          /* B (low 8 bits) */

    return ((r & 0xF800) | ((g << 3) & 0x07E0) | (g >> 11));
}

void BSP_LCD_Clear(uint16_t color)
{
    BSP_LCD_FillRect(0, 0, lcd_ctx.x_length, lcd_ctx.y_length, color);
}

void BSP_LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t total = (uint32_t)w * h;
    uint32_t i;

    if (x >= lcd_ctx.x_length || y >= lcd_ctx.y_length) return;
    if (x + w > lcd_ctx.x_length) w = lcd_ctx.x_length - x;
    if (y + h > lcd_ctx.y_length) h = lcd_ctx.y_length - y;

    lcd_open_window(x, y, w, h);
    lcd_write_cmd(0x2C);

    for (i = 0; i < total; i++) {
        lcd_write_data(color);
    }
}

/* ================================================================
 * 公开 API — 几何图形 (Bresenham 算法)
 * ================================================================ */

void BSP_LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t us;
    uint16_t x_cur = x1, y_cur = y1;
    int32_t  err_x = 0, err_y = 0;
    int32_t  delta_x, delta_y, distance;
    int32_t  inc_x, inc_y;

    delta_x = (int32_t)x2 - x1;
    delta_y = (int32_t)y2 - y1;

    inc_x = (delta_x > 0) ? 1 : (delta_x == 0 ? 0 : -1);
    delta_x = (delta_x > 0) ? delta_x : -delta_x;

    inc_y = (delta_y > 0) ? 1 : (delta_y == 0 ? 0 : -1);
    delta_y = (delta_y > 0) ? delta_y : -delta_y;

    distance = (delta_x > delta_y) ? delta_x : delta_y;

    for (us = 0; us <= (uint16_t)distance + 1; us++) {
        BSP_LCD_SetPixel(x_cur, y_cur, color);

        err_x += delta_x;
        err_y += delta_y;

        if (err_x > distance) {
            err_x -= distance;
            x_cur += (uint16_t)inc_x;
        }
        if (err_y > distance) {
            err_y -= distance;
            y_cur += (uint16_t)inc_y;
        }
    }
}

void BSP_LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color, uint8_t filled)
{
    if (filled) {
        BSP_LCD_FillRect(x, y, w, h, color);
    } else {
        BSP_LCD_DrawLine(x,       y,       x + w - 1, y,         color);
        BSP_LCD_DrawLine(x,       y + h - 1, x + w - 1, y + h - 1, color);
        BSP_LCD_DrawLine(x,       y,       x,         y + h - 1, color);
        BSP_LCD_DrawLine(x + w - 1, y,     x + w - 1, y + h - 1, color);
    }
}

void BSP_LCD_DrawCircle(uint16_t cx, uint16_t cy, uint16_t r,
                        uint16_t color, uint8_t filled)
{
    int16_t cur_x = 0, cur_y = (int16_t)r;
    int16_t err = 3 - ((int16_t)r << 1);

    while (cur_x <= cur_y) {
        int16_t cnt;

        if (filled) {
            for (cnt = cur_x; cnt <= cur_y; cnt++) {
                BSP_LCD_SetPixel(cx + cur_x, cy + cnt, color);
                BSP_LCD_SetPixel(cx - cur_x, cy + cnt, color);
                BSP_LCD_SetPixel(cx - cnt,   cy + cur_x, color);
                BSP_LCD_SetPixel(cx - cnt,   cy - cur_x, color);
                BSP_LCD_SetPixel(cx - cur_x, cy - cnt, color);
                BSP_LCD_SetPixel(cx + cur_x, cy - cnt, color);
                BSP_LCD_SetPixel(cx + cnt,   cy - cur_x, color);
                BSP_LCD_SetPixel(cx + cnt,   cy + cur_x, color);
            }
        } else {
            BSP_LCD_SetPixel(cx + cur_x, cy + cur_y, color);
            BSP_LCD_SetPixel(cx - cur_x, cy + cur_y, color);
            BSP_LCD_SetPixel(cx - cur_y, cy + cur_x, color);
            BSP_LCD_SetPixel(cx - cur_y, cy - cur_x, color);
            BSP_LCD_SetPixel(cx - cur_x, cy - cur_y, color);
            BSP_LCD_SetPixel(cx + cur_x, cy - cur_y, color);
            BSP_LCD_SetPixel(cx + cur_y, cy - cur_x, color);
            BSP_LCD_SetPixel(cx + cur_y, cy + cur_x, color);
        }

        cur_x++;
        if (err < 0) {
            err += 4 * cur_x + 6;
        } else {
            err += 10 + 4 * (cur_x - cur_y);
            cur_y--;
        }
    }
}

/* ================================================================
 * 公开 API — 字符/字符串
 * ================================================================ */

void BSP_LCD_DrawChar(uint16_t x, uint16_t y, char ch, const sFONT *font,
                      uint16_t fg_color, uint16_t bg_color)
{
    uint16_t rel_pos = (uint16_t)(ch - ' ');
    uint16_t font_size = (font->Width * font->Height) / 8;
    const uint8_t *pfont = &font->table[rel_pos * font_size];

    uint16_t row, col;

    lcd_open_window(x, y, font->Width, font->Height);
    lcd_write_cmd(0x2C);

    for (row = 0; row < font->Height; row++) {
        for (col = 0; col < font->Width; col++) {
            uint16_t byte_idx = row * ((font->Width + 7) / 8) + (col / 8);
            uint8_t  bit_pos  = 7 - (col % 8);

            if (pfont[byte_idx] & (1 << bit_pos)) {
                lcd_write_data(fg_color);
            } else {
                lcd_write_data(bg_color);
            }
        }
    }
}

void BSP_LCD_DrawString(uint16_t x, uint16_t y, const char *str, const sFONT *font,
                        uint16_t fg_color, uint16_t bg_color)
{
    uint16_t cur_x = x, cur_y = y;

    while (*str != '\0') {
        /* 换行检查 */
        if (cur_x + font->Width > lcd_ctx.x_length) {
            cur_x = x;
            cur_y += font->Height;
        }
        if (cur_y + font->Height > lcd_ctx.y_length) {
            cur_x = x;
            cur_y = y;
        }

        BSP_LCD_DrawChar(cur_x, cur_y, *str, font, fg_color, bg_color);

        str++;
        cur_x += font->Width;
    }
}
