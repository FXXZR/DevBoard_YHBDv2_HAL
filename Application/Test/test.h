/**
 * @file    test.h
 * @brief   硬件测试模块 —— 统一测试头文件
 * @note    本文件聚合所有 BSP 驱动头文件，并声明各外设的测试函数。
 *          应用层只需 #include "test.h" 即可调用任意测试。
 *
 *          测试函数通常按外设分组，但允许跨外设组合以方便观察
 *          (如按键测试用 LED 反馈, ADC 测试用串口打印)。
 */

#ifndef __TEST_H__
#define __TEST_H__

#include <stdint.h>
#include <stdio.h>

/* ---- BSP 驱动 ---- */
#include "bsp_systick.h"
#include "bsp_led.h"
#include "bsp_usart.h"
#include "bsp_beep.h"
#include "bsp_key.h"
#include "bsp_adc.h"
#include "bsp_rtc.h"
#include "bsp_eeprom.h"
#include "bsp_spi_flash.h"
#include "bsp_sdcard.h"
#include "bsp_rs485.h"
#include "bsp_esp8266.h"
#include "bsp_sram.h"
#include "bsp_fonts.h"
#include "bsp_lcd_ili9341.h"
#include "bsp_xpt2046.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 测试函数声明
 * ================================================================ */

/**
 * @brief  蜂鸣器 + 按键联动测试
 * @note   KEY1 按一下 → 蜂鸣 200ms → 停
 *         主循环每 10ms 扫描一次，不阻塞其他逻辑。
 */
void Test_BEEP_KeyTrigger(void);

/**
 * @brief  按键扫描测试（带 LED 反馈 + 串口打印）
 * @note   KEY1 → 红 LED, KEY2 → 绿 LED
 *         按键状态变化时 printf 输出。
 */
void Test_KEY_Scan(void);

/**
 * @brief  ADC 连续采样测试（串口打印电压值）
 * @note   PC1 电压每 500ms 通过 printf 输出一次 (mV)。
 *         需在调用前连接好被测电压 (0~3.3V)。
 */
void Test_ADC_Continuous(void);

/**
 * @brief  RTC 交互式测试（定时打印 + 串口命令）
 * @note   默认每 5s printf 当前日期时间。
 *         串口命令: time / date / interval / help
 */
void Test_RTC_Interactive(void);

/**
 * @brief  EEPROM 读写测试（AT24C02 全片写入 + 读出校验）
 * @note   通过 I2C (默认软件 I2C) 操作 AT24C02。
 *         测试流程：全片擦除 → 写入测试模式 → 读回校验 → 打印结果。
 *         若校验失败会打印错误地址和期望/实际值。
 */
void Test_EEPROM_ReadWrite(void);

/**
 * @brief  SPI Flash 读写测试（W25Q64 ID + 扇区擦写 + 校验）
 * @note   通过 SPI1 操作 W25Q64。
 *         测试流程：读取 JEDEC ID → 扇区擦除 → 写入测试数据 → 读回校验。
 *         若 ID 不匹配会打印错误并跳过后续测试。
 */
void Test_SPI_FLASH_ReadWrite(void);

/**
 * @brief  SD Card + FatFS 读写验证测试
 * @note   测试流程: SD 卡初始化 → 挂载 FatFS → 创建 test.txt →
 *         读回校验 → 打印 PASS/FAIL
 *         需要插入 FAT32 格式化的 SD 卡。
 *         不自动格式化，保护已有数据。
 */
void Test_SDCARD_FatFS(void);

/**
 * @brief  RS485 ↔ USART1 透传测试
 * @note   将 USART1 (PC 串口终端) 与 RS485 (USART2 + SIT3088EESA)
 *         之间的数据双向透明转发, 实现 PC ↔ RS485 设备的透传通信。
 *         默认 115200-8-N-1, 可通过 PASSTHROUGH_BAUDRATE 宏修改。
 */
void Test_RS485_Passthrough(void);

/**
 * @brief  ESP8266 透传测试 — USART1 ↔ ESP8266 (USART3) 双向桥接
 * @note   将 USART1 (PC 串口终端) 与 ESP8266 (USART3) 之间的数据
 *         双向透明转发, 实现 PC ↔ ESP8266 的 AT 命令交互。
 *         支持本地控制命令: !help / !reset / !info
 *         默认 115200-8-N-1, 可通过 ESP8266_BAUDRATE 宏修改。
 */
void Test_ESP8266_Passthrough(void);

/**
 * @brief  USART1 DMA 收发回显测试
 * @note   RX:DMA + TX:DMA. 半满中断提前发送, IDLE 发送剩余.
 *         帧长 >256B 时分段回显 (先发 128B, IDLE 后发剩余).
 */
void Test_DMA_Echo(void);

/**
 * @brief  外部 SRAM 读写测试
 * @note   通过 FSMC Bank3 内存映射直接读写 SRAM (IS62WV51216, 1MB).
 *         测试 8 位和 16 位两种访问方式, 打印 PASS/FAIL.
 */
void Test_SRAM_ReadWrite(void);

/**
 * @brief  LCD + 触摸综合显示测试
 * @note   测试流程: 颜色条 → 几何图形(线/矩形/圆) → 字体(3套) → 触摸画板.
 *         触摸画板中按 KEY1 退出.
 */
void Test_LCD_Display(void);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_H__ */
