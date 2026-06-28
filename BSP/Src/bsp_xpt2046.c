/**
 * @file    bsp_xpt2046.c
 * @brief   BSP 触摸屏驱动实现 — XPT2046 (软件 SPI + EXTI)
 * @note    通信: 软件 SPI (PG7=CLK, PF11=MOSI, PF6=MISO, PF10=CS).
 *          触摸检测: EXTI PF9 下降沿中断.
 *          校准: 4 参考点 + 仿射变换 (取前 3 点计算, 第 4 点验证).
 */

#include "bsp_xpt2046.h"
#include "bsp_lcd_ili9341.h"
#include "bsp_fonts.h"
#include "bsp_systick.h"
#include <stdio.h>
#include <string.h>
#include "stm32f1xx_hal.h"

/* 本地延时封装 — 换平台只需改此两行 */
static inline void delay_ms(uint32_t ms) { BSP_SYSTICK_DelayMS(ms); }
static inline void delay_us(uint32_t us) { BSP_SYSTICK_DelayUS(us); }

/* ================================================================
 * 硬件映射表
 * ================================================================ */

typedef struct {
    /* 软件 SPI */
    GPIO_TypeDef  *clk_port;   uint16_t clk_pin;
    GPIO_TypeDef  *mosi_port;  uint16_t mosi_pin;
    GPIO_TypeDef  *miso_port;  uint16_t miso_pin;
    GPIO_TypeDef  *cs_port;    uint16_t cs_pin;
    /* 触摸中断 (EXTI) */
    GPIO_TypeDef  *irq_port;   uint16_t irq_pin;
    IRQn_Type      irq_n;
    uint8_t        irq_prio;      /* NVIC 优先级 */
} XPT2046_HW_Map_t;

static const XPT2046_HW_Map_t xpt_hw = {
    .clk_port  = GPIOG, .clk_pin  = GPIO_PIN_7,
    .mosi_port = GPIOF, .mosi_pin = GPIO_PIN_11,
    .miso_port = GPIOF, .miso_pin = GPIO_PIN_6,
    .cs_port   = GPIOF, .cs_pin   = GPIO_PIN_10,
    .irq_port  = GPIOF, .irq_pin  = GPIO_PIN_9,
    .irq_n     = EXTI9_5_IRQn,
    .irq_prio  = 7,
};

/* ================================================================
 * 全局状态
 * ================================================================ */

volatile uint8_t BSP_XPT2046_TouchFlag = 0;
volatile BSP_XPT2046_Coordinate_t BSP_XPT2046_TouchPoint = {0};

/* 校准系数 (运行时更新) */
static BSP_XPT2046_TouchPara_t touch_para = {
    0.085958, -0.001073, -4.979353,
    -0.001750, 0.065168, -13.318824
};

static uint8_t xpt_initialized = 0;

/* ================================================================
 * 软件 SPI 底层
 * ================================================================ */

/**
 * @brief  软件 SPI 微秒级延时 (72MHz HCLK, 粗略校准 ~1μs/count)
 */
static void xpt_spi_delay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us; i++) {
        volatile uint8_t c = 12;
        while (c--) { __NOP(); }
    }
}

static uint16_t xpt_spi_read_adc(uint8_t channel)
{
    uint8_t  i;
    uint16_t buf = 0, temp;

    HAL_GPIO_WritePin(xpt_hw.cs_port, xpt_hw.cs_pin, GPIO_PIN_RESET);

    /* 写 8 位命令 (5μs 半周期 ≈ 100KHz) */
    HAL_GPIO_WritePin(xpt_hw.clk_port, xpt_hw.clk_pin, GPIO_PIN_RESET);
    for (i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(xpt_hw.mosi_port, xpt_hw.mosi_pin,
                          (channel >> (7 - i)) & 0x01 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        xpt_spi_delay(5);
        HAL_GPIO_WritePin(xpt_hw.clk_port, xpt_hw.clk_pin, GPIO_PIN_SET);
        xpt_spi_delay(5);
        HAL_GPIO_WritePin(xpt_hw.clk_port, xpt_hw.clk_pin, GPIO_PIN_RESET);
    }

    /* 读 12 位 (CLK 低时采样, 然后拉高 — 匹配 XPT2046 输出时序) */
    for (i = 0; i < 12; i++) {
        HAL_GPIO_WritePin(xpt_hw.clk_port, xpt_hw.clk_pin, GPIO_PIN_RESET);
        xpt_spi_delay(1);
        temp = HAL_GPIO_ReadPin(xpt_hw.miso_port, xpt_hw.miso_pin);
        buf |= (temp << (11 - i));
        HAL_GPIO_WritePin(xpt_hw.clk_port, xpt_hw.clk_pin, GPIO_PIN_SET);
        xpt_spi_delay(1);
    }

    HAL_GPIO_WritePin(xpt_hw.cs_port, xpt_hw.cs_pin, GPIO_PIN_SET);

    return buf;
}

static void xpt_read_adc_xy(int16_t *x, int16_t *y)
{
    *x = (int16_t)xpt_spi_read_adc(0x90);  /* Channel Y+ → X 坐标原始值 */
    xpt_spi_delay(2);                       /* 通道切换间隔 */
    *y = (int16_t)xpt_spi_read_adc(0xD0);  /* Channel X+ → Y 坐标原始值 */
}

/* ================================================================
 * EXTI 中断初始化
 * ================================================================ */

static void xpt_init_exti(void)
{
    /* PF9: 输入上拉 (触摸 IRQ, 低电平有效) */
    __HAL_RCC_GPIOF_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = xpt_hw.irq_pin;
    gpio.Mode  = GPIO_MODE_IT_FALLING;
    gpio.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(xpt_hw.irq_port, &gpio);

    /* 使能并设置 EXTI 中断优先级 */
    HAL_NVIC_SetPriority(xpt_hw.irq_n, xpt_hw.irq_prio, 0);
    HAL_NVIC_EnableIRQ(xpt_hw.irq_n);
}

/* ================================================================
 * GPIO 初始化 (软件 SPI 引脚)
 * ================================================================ */

static void xpt_init_gpio_spi(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    /* CLK (PG7) — 推挽输出 */
    gpio.Pin   = xpt_hw.clk_pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(xpt_hw.clk_port, &gpio);

    /* MOSI (PF11) — 推挽输出 */
    gpio.Pin   = xpt_hw.mosi_pin;
    HAL_GPIO_Init(xpt_hw.mosi_port, &gpio);

    /* MISO (PF6) — 输入上拉 */
    gpio.Pin   = xpt_hw.miso_pin;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(xpt_hw.miso_port, &gpio);

    /* CS (PF10) — 推挽输出, 初始高 (失能) */
    gpio.Pin   = xpt_hw.cs_pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(xpt_hw.cs_port, &gpio);
    HAL_GPIO_WritePin(xpt_hw.cs_port, xpt_hw.cs_pin, GPIO_PIN_SET);
}

/* ================================================================
 * 采样 + 滤波
 * ================================================================ */

static uint8_t xpt_read_adc_smooth_xy(BSP_XPT2046_Coordinate_t *coord)
{
    uint8_t  cnt = 0, i;
    int16_t  ad_x, ad_y;
    int16_t  buf_x[10], buf_y[10];
    int32_t  min_x, max_x, min_y, max_y;

    /* 循环采样 10 次 (只要触摸仍有效且不足 10 次) */
    do {
        xpt_read_adc_xy(&ad_x, &ad_y);
        buf_x[cnt] = ad_x;
        buf_y[cnt] = ad_y;
        cnt++;
    } while ((HAL_GPIO_ReadPin(xpt_hw.irq_port, xpt_hw.irq_pin) == GPIO_PIN_RESET)
             && (cnt < 10));

    /* 触摸已松开则清除标志 */
    if (HAL_GPIO_ReadPin(xpt_hw.irq_port, xpt_hw.irq_pin) != GPIO_PIN_RESET) {
        BSP_XPT2046_TouchFlag = 0;
    }

    /* 成功采集 10 次: 去极值取平均 */
    if (cnt == 10) {
        min_x = max_x = buf_x[0];
        min_y = max_y = buf_y[0];

        for (i = 1; i < 10; i++) {
            if (buf_x[i] < min_x) min_x = buf_x[i];
            if (buf_x[i] > max_x) max_x = buf_x[i];
            if (buf_y[i] < min_y) min_y = buf_y[i];
            if (buf_y[i] > max_y) max_y = buf_y[i];
        }

        /* 去掉 min/max, 剩下 8 个取平均 */
        int32_t sum_x = 0, sum_y = 0;
        for (i = 0; i < 10; i++) {
            sum_x += buf_x[i];
            sum_y += buf_y[i];
        }
        sum_x -= (min_x + max_x);
        sum_y -= (min_y + max_y);

        coord->x = (uint16_t)(sum_x >> 3);   /* /8 */
        coord->y = (uint16_t)(sum_y >> 3);
        return 1;
    }

    return 0;
}

/* ================================================================
 * 校准 (3 点仿射变换)
 * ================================================================ */

/**
 * @brief  绘制校准十字参考点
 */
static void xpt_draw_cross(uint16_t x, uint16_t y)
{
    BSP_LCD_FillRect(x - 10, y, 21, 2, BSP_LCD_COLOR_RED);
    BSP_LCD_FillRect(x, y - 10, 2, 21, BSP_LCD_COLOR_RED);
}

/**
 * @brief  根据 3 个采样点计算仿射变换系数
 * @return 1=成功, 0=除零失败
 */
static uint8_t xpt_calc_calibration(const BSP_XPT2046_Coordinate_t *display,
                                     const BSP_XPT2046_Coordinate_t *screen,
                                     BSP_XPT2046_TouchPara_t *para)
{
    int32_t k = ((int32_t)(screen[0].x - screen[2].x) * (int32_t)(screen[1].y - screen[2].y))
              - ((int32_t)(screen[1].x - screen[2].x) * (int32_t)(screen[0].y - screen[2].y));

    if (k == 0) return 0;

    double dk = (double)k;

    int32_t an = ((int32_t)(display[0].x - display[2].x) * (int32_t)(screen[1].y - screen[2].y))
               - ((int32_t)(display[1].x - display[2].x) * (int32_t)(screen[0].y - screen[2].y));

    int32_t bn = ((int32_t)(screen[0].x - screen[2].x) * (int32_t)(display[1].x - display[2].x))
               - ((int32_t)(display[0].x - display[2].x) * (int32_t)(screen[1].x - screen[2].x));

    int32_t cn = ((int32_t)screen[2].x * (int32_t)display[1].x - (int32_t)screen[1].x * (int32_t)display[2].x) * (int32_t)screen[0].y
               + ((int32_t)screen[0].x * (int32_t)display[2].x - (int32_t)screen[2].x * (int32_t)display[0].x) * (int32_t)screen[1].y
               + ((int32_t)screen[1].x * (int32_t)display[0].x - (int32_t)screen[0].x * (int32_t)display[1].x) * (int32_t)screen[2].y;

    int32_t dn = ((int32_t)(display[0].y - display[2].y) * (int32_t)(screen[1].y - screen[2].y))
               - ((int32_t)(display[1].y - display[2].y) * (int32_t)(screen[0].y - screen[2].y));

    int32_t en = ((int32_t)(screen[0].x - screen[2].x) * (int32_t)(display[1].y - display[2].y))
               - ((int32_t)(display[0].y - display[2].y) * (int32_t)(screen[1].x - screen[2].x));

    int32_t fn = ((int32_t)screen[2].x * (int32_t)display[1].y - (int32_t)screen[1].x * (int32_t)display[2].y) * (int32_t)screen[0].y
               + ((int32_t)screen[0].x * (int32_t)display[2].y - (int32_t)screen[2].x * (int32_t)display[0].y) * (int32_t)screen[1].y
               + ((int32_t)screen[1].x * (int32_t)display[0].y - (int32_t)screen[0].x * (int32_t)display[1].y) * (int32_t)screen[2].y;

    para->dX_X = (double)an / dk;
    para->dX_Y = (double)bn / dk;
    para->dX   = (double)cn / dk;
    para->dY_X = (double)dn / dk;
    para->dY_Y = (double)en / dk;
    para->dY   = (double)fn / dk;

    return 1;
}

uint8_t BSP_XPT2046_TouchCalibrate(void)
{
    uint8_t i;
    char str[40];
    BSP_XPT2046_Coordinate_t display_pts[4], screen_samples[4];
    BSP_XPT2046_TouchPara_t new_para;

    uint16_t sw = BSP_LCD_WIDTH, sh = BSP_LCD_HEIGHT;

    /* 校准参考点 — 靠近四角以改善边缘覆盖 (10%/90% 位置, 约 24~32px 边距) */
    uint16_t margin_x = sw / 10;   /* 24 */
    uint16_t margin_y = sh / 10;   /* 32 */

    display_pts[0].x = margin_x;              display_pts[0].y = margin_y;
    display_pts[1].x = margin_x;              display_pts[1].y = sh - margin_y;
    display_pts[2].x = sw - margin_x;         display_pts[2].y = sh - margin_y;
    display_pts[3].x = sw - margin_x;         display_pts[3].y = margin_y;

    for (i = 0; i < 4; i++) {
        /* 清屏 + 提示 */
        BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);

        snprintf(str, sizeof(str), "Touch Calibrate %d/4", i + 1);
        BSP_LCD_DrawString((sw - strlen(str) * 8) >> 1, sh >> 1,
                           str, &Font8x16,
                           BSP_LCD_COLOR_RED, BSP_LCD_COLOR_BLACK);

        /* 绘制十字参考点 */
        xpt_draw_cross(display_pts[i].x, display_pts[i].y);

        delay_ms(200);

        /* 等待触摸采样 (10s 超时, 100μs 轮询) */
        BSP_XPT2046_Coordinate_t sample;
        uint8_t valid = 0;
        uint32_t elapsed_us = 0;

        while (elapsed_us < 10000000U) {
            if (BSP_XPT2046_TouchFlag) {
                valid = xpt_read_adc_smooth_xy(&sample);
                if (valid) break;
            }
            delay_us(100);
            elapsed_us += 100;
        }

        if (!valid) goto fail;

        screen_samples[i] = sample;
    }

    /* 用前 3 个参考点计算系数，第 4 个验证 */
    if (!xpt_calc_calibration(display_pts, screen_samples, &new_para)) goto fail;

    /* 验证: 用第 4 个点测试偏差 */
    {
        int32_t test_x = (int32_t)(new_para.dX_X * screen_samples[3].x
                                 + new_para.dX_Y * screen_samples[3].y + new_para.dX);
        int32_t test_y = (int32_t)(new_para.dY_X * screen_samples[3].x
                                 + new_para.dY_Y * screen_samples[3].y + new_para.dY);
        int32_t gap_x = test_x > (int32_t)display_pts[3].x
                        ? test_x - display_pts[3].x : display_pts[3].x - test_x;
        int32_t gap_y = test_y > (int32_t)display_pts[3].y
                        ? test_y - display_pts[3].y : display_pts[3].y - test_y;

        if (gap_x > 15 || gap_y > 15) goto fail;
    }

    /* 保存校准系数 */
    touch_para = new_para;

    BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);
    BSP_LCD_DrawString((sw - strlen("Calibrate OK") * 8) >> 1, sh >> 1,
                       "Calibrate OK", &Font8x16,
                       BSP_LCD_COLOR_GREEN, BSP_LCD_COLOR_BLACK);
    delay_ms(1500);

    return 1;

fail:
    BSP_LCD_Clear(BSP_LCD_COLOR_BLACK);
    BSP_LCD_DrawString((sw - strlen("Calibrate FAIL") * 8) >> 1, sh >> 1,
                       "Calibrate FAIL", &Font8x16,
                       BSP_LCD_COLOR_RED, BSP_LCD_COLOR_BLACK);
    delay_ms(1500);
    return 0;
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_XPT2046_Init(void)
{
    if (xpt_initialized) return;
    xpt_initialized = 1;

    xpt_init_gpio_spi();
    xpt_init_exti();
}

uint8_t BSP_XPT2046_GetTouchPoint(BSP_XPT2046_Coordinate_t *pCoord,
                                   const BSP_XPT2046_TouchPara_t *pPara)
{
    BSP_XPT2046_Coordinate_t sample;

    if (!pPara) pPara = &touch_para;

    if (xpt_read_adc_smooth_xy(&sample)) {
        pCoord->x = (uint16_t)(pPara->dX_X * sample.x + pPara->dX_Y * sample.y + pPara->dX);
        pCoord->y = (uint16_t)(pPara->dY_X * sample.x + pPara->dY_Y * sample.y + pPara->dY);
        return 1;
    }

    return 0;
}

void BSP_XPT2046_EXTI_Callback(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(xpt_hw.irq_pin) != 0x00u) {
        __HAL_GPIO_EXTI_CLEAR_IT(xpt_hw.irq_pin);
        BSP_XPT2046_TouchFlag = 1;
    }
}

/* ================================================================
 * ISR: EXTI9_5 中断 (覆盖 startup 中的弱定义)
 * ================================================================ */

void EXTI9_5_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(xpt_hw.irq_pin) != 0x00u) {
        __HAL_GPIO_EXTI_CLEAR_IT(xpt_hw.irq_pin);
        BSP_XPT2046_TouchFlag = 1;
    }
}
