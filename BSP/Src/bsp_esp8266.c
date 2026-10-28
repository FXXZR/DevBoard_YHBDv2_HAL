/**
 * @file    bsp_esp8266.c
 * @brief   BSP ESP8266 WiFi 模块驱动实现
 * @note    硬件抽象层:
 *              - PG13 (WIFI_EN):  推挽输出, 高=使能, 默认低
 *              - PG14 (WIFI_RST): 推挽输出, 高=正常, 默认高
 *              - USART3:          中断收发, 通过 BSP_USART_InitEx 配置
 *
 *          复位时序: RST 低 ≥100μs → 高 → 等待 ~200ms 启动
 */

#include "bsp_esp8266.h"
#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

#include <string.h>

/* 本地延时封装 — static inline 零开销, 换平台只需改此两行 */
static inline void esp8266_delay_ms(uint32_t ms) { BSP_SYSTICK_DelayMS(ms); }

/* ================================================================
 * 硬件映射
 * ================================================================ */

#define WIFI_EN_PORT            GPIOG
#define WIFI_EN_PIN             GPIO_PIN_13

#define WIFI_RST_PORT           GPIOG
#define WIFI_RST_PIN            GPIO_PIN_14

#define ESP8266_USART_ID        BSP_USART3

/* 复位时序 (毫秒) */
static const uint32_t reset_low_ms  = 10U;
static const uint32_t reset_boot_ms = 200U;

/* ================================================================
 * 运行时上下文
 * ================================================================ */

static struct {
    volatile uint8_t initialized;
    volatile uint8_t enabled;
    /* TX 环形缓冲 —— USART3 的使用者就是本模块, 所以由本模块提供.
     * 开 1KB: AT 命令短, 但透传模式下一次可能灌入较长的 TCP 载荷 */
    uint8_t tx_buf[1024];
} s_esp8266;

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief  简易初始化 ESP8266
 */
void BSP_ESP8266_Init(uint32_t baudrate)
{
    BSP_ESP8266_Config_t cfg = {
        .baudrate    = baudrate,
        .word_length = BSP_USART_WORD_8B,
        .stop_bits   = BSP_USART_STOP_1,
        .parity      = BSP_USART_PARITY_NONE,
    };
    BSP_ESP8266_InitEx(&cfg);
}

/**
 * @brief  完整初始化 ESP8266
 * @note   关键时序 (避免 TX 毛刺被 ESP8266 误收):
 *           1. EN=高 + RST=低 (模块上电但保持复位, UART 不工作)
 *           2. 初始化 USART3 (TX 引脚配置/使能可能产生毛刺, 但 ESP8266 在复位中不受影响)
 *           3. RST=高 (释放复位, TX 线已稳定为高)
 *           4. 等待模块启动 → 清空启动噪声
 */
void BSP_ESP8266_InitEx(const BSP_ESP8266_Config_t *config)
{
    if (s_esp8266.initialized) {
        BSP_ESP8266_DeInit();
    }

    if (!config) return;

    /* ---- GPIO 时钟 ---- */
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* ---- PG13: WIFI_EN (CHIP_EN), 初始拉高上电 ---- */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = WIFI_EN_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(WIFI_EN_PORT, &gpio);
    HAL_GPIO_WritePin(WIFI_EN_PORT, WIFI_EN_PIN, GPIO_PIN_SET);
    s_esp8266.enabled = 1;

    /* ---- PG14: WIFI_RST (EX_RSTB), 初始拉低保持复位 ---- */
    gpio.Pin  = WIFI_RST_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(WIFI_RST_PORT, &gpio);
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_RESET);
    esp8266_delay_ms(5);  /* 确保模块上电稳定 */

    /* ---- USART3 (此时 ESP8266 在复位中, TX 线任何毛刺都不会被接收) ---- */
    BSP_USART_Config_t usart_cfg = {
        .baudrate         = config->baudrate,
        .word_length      = config->word_length,
        .stop_bits        = config->stop_bits,
        .parity           = config->parity,
        .tx_cplt_callback = NULL,  /* ESP8266 全双工, 不需要方向切换 */
        .rx_idle_callback = config->rx_idle_callback,
        .tx_buf           = s_esp8266.tx_buf,
        .tx_buf_size      = sizeof(s_esp8266.tx_buf),
    };
    BSP_USART_InitEx(ESP8266_USART_ID, &usart_cfg);

    /* ---- 释放复位: TX 线已稳定为高, ESP8266 正常启动 ---- */
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
    esp8266_delay_ms(reset_boot_ms);  /* 等待模块固件启动 */

    /* 清空启动期间 ESP8266 可能发出的数据 (boot ROM 74880baud 噪声等) */
    BSP_USART_FlushRx(ESP8266_USART_ID);

    s_esp8266.initialized = 1;
}

/**
 * @brief  关闭 ESP8266
 */
void BSP_ESP8266_DeInit(void)
{
    if (!s_esp8266.initialized) return;

    BSP_USART_DeInit(ESP8266_USART_ID);
    HAL_GPIO_DeInit(WIFI_EN_PORT, WIFI_EN_PIN);
    HAL_GPIO_DeInit(WIFI_RST_PORT, WIFI_RST_PIN);
    s_esp8266.enabled    = 0;
    s_esp8266.initialized = 0;
}

/* ----------------------------------------------------------------
 * 硬件控制
 * ---------------------------------------------------------------- */

/**
 * @brief  使能 ESP8266 (EN 拉高)
 */
void BSP_ESP8266_Enable(void)
{
    if (!s_esp8266.initialized) return;
    HAL_GPIO_WritePin(WIFI_EN_PORT, WIFI_EN_PIN, GPIO_PIN_SET);
    s_esp8266.enabled = 1;
}

/**
 * @brief  禁用 ESP8266 (EN 拉低)
 */
void BSP_ESP8266_Disable(void)
{
    if (!s_esp8266.initialized) return;
    HAL_GPIO_WritePin(WIFI_EN_PORT, WIFI_EN_PIN, GPIO_PIN_RESET);
    s_esp8266.enabled = 0;
}

/**
 * @brief  硬件复位 ESP8266
 * @note   时序: RST 拉低 ~10ms → 拉高 → 等待 ~200ms 模块启动
 */
void BSP_ESP8266_Reset(void)
{
    if (!s_esp8266.initialized) return;

    /* 确保模块已使能 */
    if (!s_esp8266.enabled) {
        BSP_ESP8266_Enable();
        esp8266_delay_ms(10);
    }

    /* 拉低 RST ≥100μs, 用 10ms 保证足够裕量 */
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_RESET);
    esp8266_delay_ms(reset_low_ms);

    /* 拉高 RST, 等待模块启动 */
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
    esp8266_delay_ms(reset_boot_ms);

    /* 清空 USART3 接收缓冲 (复位期间可能产生乱码) */
    BSP_ESP8266_FlushRx();
}

/**
 * @brief  查询模块是否已使能
 */
uint8_t BSP_ESP8266_IsEnabled(void)
{
    return s_esp8266.enabled;
}

/* ----------------------------------------------------------------
 * 通信 (封装 USART3)
 * ---------------------------------------------------------------- */

uint16_t BSP_ESP8266_Send(const uint8_t *data, uint16_t len)
{
    if (!s_esp8266.initialized) return 0;
    return BSP_USART_Send(ESP8266_USART_ID, data, len);
}

uint16_t BSP_ESP8266_SendString(const char *str)
{
    if (!str) return 0;
    return BSP_ESP8266_Send((const uint8_t *)str, (uint16_t)strlen(str));
}

int BSP_ESP8266_Flush(uint32_t timeout)
{
    if (!s_esp8266.initialized) return -1;
    return BSP_USART_Flush(ESP8266_USART_ID, timeout);
}

/**
 * @brief  查询发送缓冲还能入队多少字节
 * @note   应用层通过本模块访问 USART3, 不直接调用 BSP_USART_* ——
 *         否则会绕过本模块对串口的所有权约定
 */
uint16_t BSP_ESP8266_TxSpace(void)
{
    return BSP_USART_TxSpace(ESP8266_USART_ID);
}

/**
 * @brief  查询发送缓冲中尚未发出的字节数
 */
uint16_t BSP_ESP8266_TxPending(void)
{
    return BSP_USART_TxPending(ESP8266_USART_ID);
}

/**
 * @brief  查询发送是否完全空闲
 * @return 1 = 空闲 (AT 命令已完整发出), 0 = 仍有数据在途
 * @note   AT 命令是严格请求-应答式的: 必须确认上一条命令发完,
 *         才能开始等它的响应, 否则会把两条命令的响应混在一起.
 */
uint8_t BSP_ESP8266_TxIdle(void)
{
    return BSP_USART_TxIdle(ESP8266_USART_ID);
}

uint16_t BSP_ESP8266_Available(void)
{
    if (!s_esp8266.initialized) return 0;
    return BSP_USART_Available(ESP8266_USART_ID);
}

int BSP_ESP8266_Read(uint8_t *ch)
{
    if (!s_esp8266.initialized) return -1;
    return BSP_USART_Read(ESP8266_USART_ID, ch);
}

uint16_t BSP_ESP8266_ReadBytes(uint8_t *buf, uint16_t len)
{
    if (!s_esp8266.initialized) return 0;
    return BSP_USART_ReadBytes(ESP8266_USART_ID, buf, len);
}

void BSP_ESP8266_FlushRx(void)
{
    if (!s_esp8266.initialized) return;
    BSP_USART_FlushRx(ESP8266_USART_ID);
}
