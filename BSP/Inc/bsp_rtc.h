/**
 * @file    bsp_rtc.h
 * @brief   BSP RTC 驱动接口 —— 平台无关的实时时钟 API
 * @note    本文件只暴露抽象接口，不包含任何 HAL/寄存器依赖。
 *          更换 MCU 时只需修改对应的 .c 实现文件。
 *
 *          时钟源通过宏选择:
 *            BSP_RTC_CLK_LSE — 外部 32.768KHz 晶振 (默认，高精度)
 *            BSP_RTC_CLK_LSI — 内部 ~40KHz RC 振荡器 (无需外部晶振，精度低)
 *
 *          RTC 底层由 HAL 的 Time + Date 两个 HAL 接口驱动，
 *          本层将其合并为统一的 DateTime 结构体，并提供
 *          UNIX 时间戳 (UTC+0) 的互转及读写接口。
 *
 *          首次上电 (VBAT 掉电) 后自动写入默认值
 *          2020-01-01 00:00:00 (ts=1577836800)。
 *
 *          注意:
 *            (a) 本模块内部时间基准为 UTC+0
 *            (b) UNIX 时间戳使用 32-bit 无符号数，范围 1970~2106
 *            (c) 世纪字段固定为 20 (本模块仅覆盖 2000~2099)
 */

#ifndef __BSP_RTC_H__
#define __BSP_RTC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 时钟源选择 (二选一，默认 LSE)
 * ================================================================ */
// #define BSP_RTC_CLK_LSE       /* 外部 32.768KHz 晶振 */
#define BSP_RTC_CLK_LSI    /* 内部 ~40KHz RC (取消上行注释并注释本行切换) */

/* ================================================================
 * 统一的日期时间结构体 (8 字节, 自然对齐)
 * ================================================================ */

typedef struct {
    uint8_t weekday;   /* 周几 1~7 (1=Mon..7=Sun)       */
    uint8_t sec;       /* 秒   0~59                     */
    uint8_t min;       /* 分   0~59                     */
    uint8_t hour;      /* 时   0~23                     */
    uint8_t date;      /* 日   1~31                     */
    uint8_t month;     /* 月   1~12                     */
    uint8_t year;      /* 年   0~99 (2000~2099)         */
    uint8_t century;   /* 世纪 固定 20 (20xx 年)         */
} BSP_RTC_DateTime_t;

/* ================================================================
 * 配置常量
 * ================================================================ */
#define BSP_RTC_CENTURY     20U   /* 世纪固定值 (2000~2099) */

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化 RTC (含时钟源选择和首次上电默认值写入)
 * @note   必须在 HAL_Init() + BSP_SYSTICK_DelayInit() 之后调用。
 *         首次上电 (VBAT 掉电) 后自动写入默认值 2020-01-01 00:00:00。
 *         仅需调用一次。
 */
void BSP_RTC_Init(void);

/**
 * @brief  读取当前日期时间
 * @param  dt  输出参数，存放当前日期时间
 */
void BSP_RTC_GetDateTime(BSP_RTC_DateTime_t *dt);

/**
 * @brief  设置当前日期时间
 * @param  dt  待设置的日期时间
 */
void BSP_RTC_SetDateTime(const BSP_RTC_DateTime_t *dt);

/**
 * @brief  读取当前 UNIX 时间戳 (UTC+0, 32-bit)
 * @return UNIX 时间戳 (秒, 1970-01-01 00:00:00 起算, 范围 ~2106 年)
 */
uint32_t BSP_RTC_GetTimestamp(void);

/**
 * @brief  设置当前 UNIX 时间戳 (UTC+0, 32-bit)
 * @param  ts  UNIX 时间戳
 * @note   若 ts 对应日期超出 2000-2099 范围，函数静默忽略
 */
void BSP_RTC_SetTimestamp(uint32_t ts);

/**
 * @brief  UNIX 时间戳 → DateTime (纯算法, 不访问 RTC 硬件)
 * @param  ts  UNIX 时间戳 (秒, UTC+0)
 * @param  dt  输出参数，存放转换后的日期时间
 * @note   结果始终为 UTC+0。调用方可在此基础上加时区偏移后再传入。
 */
void BSP_RTC_ts_to_dt(uint32_t ts, BSP_RTC_DateTime_t *dt);

/**
 * @brief  DateTime → UNIX 时间戳 (纯算法, 不访问 RTC 硬件)
 * @param  dt  待转换的日期时间 (UTC+0)
 * @return UNIX 时间戳 (秒, 1970-01-01 00:00:00 起算)
 */
uint32_t BSP_RTC_dt_to_ts(const BSP_RTC_DateTime_t *dt);

/**
 * @brief  根据日期计算星期 (纯算法, 不访问 RTC 硬件)
 * @param  year   年 (0~99, 2000~2099)
 * @param  month  月 (1~12)
 * @param  date   日 (1~31)
 * @return 星期 (1=Mon..7=Sun), 非法输入返回 0
 */
uint8_t BSP_RTC_CalcWeekday(uint8_t year, uint8_t month, uint8_t date);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RTC_H__ */
