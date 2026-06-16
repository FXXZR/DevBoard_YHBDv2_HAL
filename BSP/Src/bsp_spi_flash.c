/**
 * @file    bsp_spi_flash.c
 * @brief   BSP SPI Flash 驱动实现 —— W25Q64 通过 SPI1
 * @note    CS: PA4 (软件控制), SPI: PA5=SCK, PA6=MISO, PA7=MOSI
 *          SPI 模式 0 (CPOL=0, CPHA=0): 空闲低电平, 第1边沿采样。
 *
 *          W25Q64 指令协议 (每个写指令前需 Write Enable):
 *            读:   CS↓ → 0x03 → A23..A0 → Data... → CS↑
 *            页写:  CS↓ → 0x06 → CS↑  (Write Enable)
 *                  CS↓ → 0x02 → A23..A0 → Data(≤256B) → CS↑ → wait BUSY=0
 *            擦除:  CS↓ → 0x06 → CS↑  (Write Enable)
 *                  CS↓ → 0x20/0xD8/0xC7 → A23..A0 → CS↑ → wait BUSY=0
 *
 *          P0 改进: 所有 busy-wait 循环添加超时保护。
 *          P2 改进: 擦除/写入函数返回状态码，区分超时/错误。
 *
 *          参考: W25Q64JV Datasheet (Winbond)
 */

#include "bsp_spi_flash.h"
#include "bsp_spi.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * CS 引脚定义 (PA4, 软件控制) — 寄存器位操作宏
 * ================================================================ */

#define FLASH_CS_PORT      GPIOA
#define FLASH_CS_PIN       GPIO_PIN_4
#define FLASH_CS_LOW()     (FLASH_CS_PORT->BRR  = FLASH_CS_PIN)
#define FLASH_CS_HIGH()    (FLASH_CS_PORT->BSRR = FLASH_CS_PIN)

/* ================================================================
 * W25Q64 指令集 (内部常量)
 * ================================================================ */

static const uint8_t kCmdWriteEnable       = 0x06;
static const uint8_t kCmdReadStatus1       = 0x05;
static const uint8_t kCmdReadData          = 0x03;
static const uint8_t kCmdPageProgram       = 0x02;
static const uint8_t kCmdSectorErase       = 0x20;
static const uint8_t kCmdBlockErase64K     = 0xD8;
static const uint8_t kCmdChipErase         = 0xC7;
static const uint8_t kCmdJedecID           = 0x9F;
static const uint8_t kCmdPowerDown         = 0xB9;
static const uint8_t kCmdReleasePowerDown  = 0xAB;

static const uint8_t kStatusBusy           = 0x01;

/* ================================================================
 * 内部辅助函数
 * ================================================================ */

static inline uint8_t s_spi_byte(uint8_t data)
{
    return BSP_SPI_Transfer(BSP_SPI_BUS_1, data, BSP_SPI_FLASH_TIMEOUT_STD);
}

static void s_flash_write_enable(void)
{
    FLASH_CS_LOW();
    s_spi_byte(kCmdWriteEnable);
    FLASH_CS_HIGH();
}

static uint8_t s_flash_read_status(void)
{
    uint8_t status;
    FLASH_CS_LOW();
    s_spi_byte(kCmdReadStatus1);
    status = s_spi_byte(0xFF);
    FLASH_CS_HIGH();
    return status;
}

/**
 * @brief  等待 Flash 内部操作完成 (P0: 带超时)
 * @param  timeout_ms  超时 (ms)
 * @return BSP_SPI_FLASH_OK / BSP_SPI_FLASH_TIMEOUT
 */
static BSP_SPI_FLASH_Status_t s_flash_wait_busy(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (s_flash_read_status() & kStatusBusy) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return BSP_SPI_FLASH_TIMEOUT;
        }
    }
    return BSP_SPI_FLASH_OK;
}

/**
 * @brief  单页写入 (≤256B, 不跨页)
 * @note   调用者保证 (addr % 256 + len) ≤ 256。
 */
static BSP_SPI_FLASH_Status_t s_flash_page_write(const uint8_t *buf, uint32_t addr, uint16_t len)
{
    if (len == 0) return BSP_SPI_FLASH_OK;

    s_flash_write_enable();

    FLASH_CS_LOW();
    s_spi_byte(kCmdPageProgram);
    s_spi_byte((uint8_t)((addr >> 16) & 0xFF));
    s_spi_byte((uint8_t)((addr >> 8)  & 0xFF));
    s_spi_byte((uint8_t)( addr        & 0xFF));

    for (uint16_t i = 0; i < len; i++) {
        s_spi_byte(buf[i]);
    }
    FLASH_CS_HIGH();

    return s_flash_wait_busy(BSP_SPI_FLASH_TIMEOUT_PAGE);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_SPI_FLASH_Init(void)
{
    /* 1. 初始化 SPI 总线 (18MHz) */
    BSP_SPI_Init(BSP_SPI_BUS_1, BSP_SPI_BAUD_DIV_4);

    /* 2. 配置 CS 引脚 (PA4, 推挽输出, 初始高) */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = FLASH_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FLASH_CS_PORT, &gpio);

    FLASH_CS_HIGH();

    /* 3. 唤醒 Flash (从可能的 Power-down 状态) */
    BSP_SPI_FLASH_Wakeup();
}

uint32_t BSP_SPI_FLASH_ReadID(void)
{
    uint32_t id = 0;

    FLASH_CS_LOW();
    s_spi_byte(kCmdJedecID);
    id  = (uint32_t)s_spi_byte(0xFF) << 16;
    id |= (uint32_t)s_spi_byte(0xFF) << 8;
    id |= (uint32_t)s_spi_byte(0xFF);
    FLASH_CS_HIGH();

    return id;
}

void BSP_SPI_FLASH_Read(uint8_t *buf, uint32_t addr, uint32_t len)
{
    if (!buf || len == 0) return;
    if (addr + len > BSP_SPI_FLASH_CAPACITY) return;

    FLASH_CS_LOW();
    s_spi_byte(kCmdReadData);
    s_spi_byte((uint8_t)((addr >> 16) & 0xFF));
    s_spi_byte((uint8_t)((addr >> 8)  & 0xFF));
    s_spi_byte((uint8_t)( addr        & 0xFF));

    for (uint32_t i = 0; i < len; i++) {
        buf[i] = s_spi_byte(0xFF);
    }
    FLASH_CS_HIGH();
}

BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_Write(const uint8_t *buf, uint32_t addr, uint32_t len)
{
    if (!buf || len == 0) return BSP_SPI_FLASH_ERROR;
    if (addr + len > BSP_SPI_FLASH_CAPACITY) return BSP_SPI_FLASH_ERROR;

    BSP_SPI_FLASH_Status_t ret;
    uint32_t remaining  = len;
    uint16_t page_space = BSP_SPI_FLASH_PAGE_SIZE
                          - (uint16_t)(addr % BSP_SPI_FLASH_PAGE_SIZE);

    if (remaining <= page_space) {
        return s_flash_page_write(buf, addr, (uint16_t)remaining);
    }

    if (page_space > 0) {
        ret = s_flash_page_write(buf, addr, page_space);
        if (ret != BSP_SPI_FLASH_OK) return ret;
        buf       += page_space;
        addr      += page_space;
        remaining -= page_space;
    }

    while (remaining >= BSP_SPI_FLASH_PAGE_SIZE) {
        ret = s_flash_page_write(buf, addr, BSP_SPI_FLASH_PAGE_SIZE);
        if (ret != BSP_SPI_FLASH_OK) return ret;
        buf       += BSP_SPI_FLASH_PAGE_SIZE;
        addr      += BSP_SPI_FLASH_PAGE_SIZE;
        remaining -= BSP_SPI_FLASH_PAGE_SIZE;
    }

    if (remaining > 0) {
        return s_flash_page_write(buf, addr, (uint16_t)remaining);
    }

    return BSP_SPI_FLASH_OK;
}

BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_SectorErase(uint32_t addr)
{
    addr &= ~(BSP_SPI_FLASH_SECTOR_SIZE - 1U);

    s_flash_write_enable();

    FLASH_CS_LOW();
    s_spi_byte(kCmdSectorErase);
    s_spi_byte((uint8_t)((addr >> 16) & 0xFF));
    s_spi_byte((uint8_t)((addr >> 8)  & 0xFF));
    s_spi_byte((uint8_t)( addr        & 0xFF));
    FLASH_CS_HIGH();

    return s_flash_wait_busy(BSP_SPI_FLASH_TIMEOUT_SECTOR);
}

BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_BlockErase(uint32_t addr)
{
    addr &= ~(BSP_SPI_FLASH_BLOCK_SIZE - 1U);

    s_flash_write_enable();

    FLASH_CS_LOW();
    s_spi_byte(kCmdBlockErase64K);
    s_spi_byte((uint8_t)((addr >> 16) & 0xFF));
    s_spi_byte((uint8_t)((addr >> 8)  & 0xFF));
    s_spi_byte((uint8_t)( addr        & 0xFF));
    FLASH_CS_HIGH();

    return s_flash_wait_busy(BSP_SPI_FLASH_TIMEOUT_BLOCK);
}

BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_ChipErase(void)
{
    s_flash_write_enable();

    FLASH_CS_LOW();
    s_spi_byte(kCmdChipErase);
    FLASH_CS_HIGH();

    return s_flash_wait_busy(BSP_SPI_FLASH_TIMEOUT_CHIP);
}

void BSP_SPI_FLASH_PowerDown(void)
{
    FLASH_CS_LOW();
    s_spi_byte(kCmdPowerDown);
    FLASH_CS_HIGH();
}

void BSP_SPI_FLASH_Wakeup(void)
{
    FLASH_CS_LOW();
    s_spi_byte(kCmdReleasePowerDown);
    FLASH_CS_HIGH();
}
