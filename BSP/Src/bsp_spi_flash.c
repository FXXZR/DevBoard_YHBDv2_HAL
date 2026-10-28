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
#include "bsp_assert.h"
#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

/* 本地延时封装 — 换平台只需改此行 */
static inline void delay_us(uint32_t us) { BSP_SYSTICK_DelayUS(us); }

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

static const struct {
    uint8_t write_enable;
    uint8_t read_status1;
    uint8_t read_data;
    uint8_t page_program;
    uint8_t sector_erase;
    uint8_t block_erase_64k;
    uint8_t chip_erase;
    uint8_t jedec_id;
    uint8_t power_down;
    uint8_t release_power_down;
} spi_flash_cmds = {
    .write_enable       = 0x06,
    .read_status1       = 0x05,
    .read_data          = 0x03,
    .page_program       = 0x02,
    .sector_erase       = 0x20,
    .block_erase_64k    = 0xD8,
    .chip_erase         = 0xC7,
    .jedec_id           = 0x9F,
    .power_down         = 0xB9,
    .release_power_down = 0xAB,
};

/* 状态寄存器 1 的 BUSY 位 (语义是位掩码, 不并进指令表) */
static const uint8_t spi_flash_status_busy = 0x01U;

/* ================================================================
 * 内部辅助函数
 * ================================================================ */

static inline uint8_t spi_flash_byte(uint8_t data)
{
    return BSP_SPI_Transfer(BSP_SPI_BUS_1, data, BSP_SPI_FLASH_TIMEOUT_STD);
}

static void spi_flash_write_enable(void)
{
    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.write_enable);
    FLASH_CS_HIGH();
}

static uint8_t spi_flash_read_status(void)
{
    uint8_t status;
    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.read_status1);
    status = spi_flash_byte(0xFF);
    FLASH_CS_HIGH();
    return status;
}

/**
 * @brief  等待 Flash 内部操作完成 (P0: 带超时)
 * @param  timeout_ms  超时 (ms)
 * @return BSP_SPI_FLASH_OK / BSP_SPI_FLASH_TIMEOUT
 */
static BSP_SPI_FLASH_Status_t spi_flash_wait_busy(uint32_t timeout_ms)
{
    uint32_t elapsed_us = 0;
    uint32_t limit_us   = timeout_ms * 1000U;
    while (spi_flash_read_status() & spi_flash_status_busy) {
        if (elapsed_us >= limit_us) {
            return BSP_SPI_FLASH_TIMEOUT;
        }
        delay_us(100);
        elapsed_us += 100;
    }
    return BSP_SPI_FLASH_OK;
}

/**
 * @brief  单页写入 (≤256B, 不跨页)
 * @note   前端条件 (addr % 256 + len) ≤ 256 由断言强制, 见下。
 */
static BSP_SPI_FLASH_Status_t spi_flash_page_write(const uint8_t *buf, uint32_t addr, uint16_t len)
{
    if (len == 0) return BSP_SPI_FLASH_OK;

    /* 把"调用者保证"从注释变成跑得起来的检查 —— 注释是给读代码的人看的,
     * 断言是给跑起来的程序看的.
     * 跨页写时 W25Q64 会从【页首绕回】覆盖前面的数据, 是 Flash 驱动最经典的
     * bug 之一, 而且现象是"数据莫名其妙被改了一部分", 极难定位.
     * 本函数是 static, 唯一调用者 BSP_SPI_FLASH_Write 已按页拆分,
     * 所以这里违反 == 拆分逻辑写错了. */
    BSP_ASSERT((addr % BSP_SPI_FLASH_PAGE_SIZE) + (uint32_t)len
               <= (uint32_t)BSP_SPI_FLASH_PAGE_SIZE);

    spi_flash_write_enable();

    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.page_program);
    spi_flash_byte((uint8_t)((addr >> 16) & 0xFF));
    spi_flash_byte((uint8_t)((addr >> 8)  & 0xFF));
    spi_flash_byte((uint8_t)( addr        & 0xFF));

    for (uint16_t i = 0; i < len; i++) {
        spi_flash_byte(buf[i]);
    }
    FLASH_CS_HIGH();

    return spi_flash_wait_busy(BSP_SPI_FLASH_TIMEOUT_PAGE);
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
    spi_flash_byte(spi_flash_cmds.jedec_id);
    id  = (uint32_t)spi_flash_byte(0xFF) << 16;
    id |= (uint32_t)spi_flash_byte(0xFF) << 8;
    id |= (uint32_t)spi_flash_byte(0xFF);
    FLASH_CS_HIGH();

    return id;
}

void BSP_SPI_FLASH_Read(uint8_t *buf, uint32_t addr, uint32_t len)
{
    BSP_ASSERT(buf != NULL);

    /* 注: 下面两处是【范围检查】而不是编程错误, 保持静默返回 / 状态码.
     * 若将来确认地址只由代码产生 (不来自外部数据), 也可以提升为断言. */
    if (len == 0) return;
    if (addr + len > BSP_SPI_FLASH_CAPACITY) return;

    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.read_data);
    spi_flash_byte((uint8_t)((addr >> 16) & 0xFF));
    spi_flash_byte((uint8_t)((addr >> 8)  & 0xFF));
    spi_flash_byte((uint8_t)( addr        & 0xFF));

    for (uint32_t i = 0; i < len; i++) {
        buf[i] = spi_flash_byte(0xFF);
    }
    FLASH_CS_HIGH();
}

BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_Write(const uint8_t *buf, uint32_t addr, uint32_t len)
{
    BSP_ASSERT(buf != NULL);

    if (len == 0) return BSP_SPI_FLASH_ERROR;
    if (addr + len > BSP_SPI_FLASH_CAPACITY) return BSP_SPI_FLASH_ERROR;

    BSP_SPI_FLASH_Status_t ret;
    uint32_t remaining  = len;
    uint16_t page_space = BSP_SPI_FLASH_PAGE_SIZE
                          - (uint16_t)(addr % BSP_SPI_FLASH_PAGE_SIZE);

    if (remaining <= page_space) {
        return spi_flash_page_write(buf, addr, (uint16_t)remaining);
    }

    if (page_space > 0) {
        ret = spi_flash_page_write(buf, addr, page_space);
        if (ret != BSP_SPI_FLASH_OK) return ret;
        buf       += page_space;
        addr      += page_space;
        remaining -= page_space;
    }

    while (remaining >= BSP_SPI_FLASH_PAGE_SIZE) {
        ret = spi_flash_page_write(buf, addr, BSP_SPI_FLASH_PAGE_SIZE);
        if (ret != BSP_SPI_FLASH_OK) return ret;
        buf       += BSP_SPI_FLASH_PAGE_SIZE;
        addr      += BSP_SPI_FLASH_PAGE_SIZE;
        remaining -= BSP_SPI_FLASH_PAGE_SIZE;
    }

    if (remaining > 0) {
        return spi_flash_page_write(buf, addr, (uint16_t)remaining);
    }

    return BSP_SPI_FLASH_OK;
}

BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_SectorErase(uint32_t addr)
{
    addr &= ~(BSP_SPI_FLASH_SECTOR_SIZE - 1U);

    spi_flash_write_enable();

    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.sector_erase);
    spi_flash_byte((uint8_t)((addr >> 16) & 0xFF));
    spi_flash_byte((uint8_t)((addr >> 8)  & 0xFF));
    spi_flash_byte((uint8_t)( addr        & 0xFF));
    FLASH_CS_HIGH();

    return spi_flash_wait_busy(BSP_SPI_FLASH_TIMEOUT_SECTOR);
}

BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_BlockErase(uint32_t addr)
{
    addr &= ~(BSP_SPI_FLASH_BLOCK_SIZE - 1U);

    spi_flash_write_enable();

    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.block_erase_64k);
    spi_flash_byte((uint8_t)((addr >> 16) & 0xFF));
    spi_flash_byte((uint8_t)((addr >> 8)  & 0xFF));
    spi_flash_byte((uint8_t)( addr        & 0xFF));
    FLASH_CS_HIGH();

    return spi_flash_wait_busy(BSP_SPI_FLASH_TIMEOUT_BLOCK);
}

BSP_SPI_FLASH_Status_t BSP_SPI_FLASH_ChipErase(void)
{
    spi_flash_write_enable();

    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.chip_erase);
    FLASH_CS_HIGH();

    return spi_flash_wait_busy(BSP_SPI_FLASH_TIMEOUT_CHIP);
}

void BSP_SPI_FLASH_PowerDown(void)
{
    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.power_down);
    FLASH_CS_HIGH();
}

void BSP_SPI_FLASH_Wakeup(void)
{
    FLASH_CS_LOW();
    spi_flash_byte(spi_flash_cmds.release_power_down);
    FLASH_CS_HIGH();
}
