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

#ifdef __cplusplus
}
#endif

#endif /* __TEST_H__ */
