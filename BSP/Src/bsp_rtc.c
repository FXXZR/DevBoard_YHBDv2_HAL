/**
 * @file    bsp_rtc.c
 * @brief   BSP RTC 驱动实现 —— 统一 DateTime + UNIX 时间戳
 * @note    F1 HAL 只有 Time / Date 两个分离接口，本层封装为:
 *            统一 DateTime 读写 (Init / GetDateTime / SetDateTime)
 *            UNIX 时间戳读写   (GetTimestamp / SetTimestamp / ts↔dt 互转)
 *
 *          Backup 寄存器分配:
 *            BKP_DR1 — Magic 值 0xA5A5 (冷/热启动判定)
 *            BKP_DR2 — Year  (0~99)
 *            BKP_DR3 — Month (1~12)
 *            BKP_DR4 — Date  (1~31)
 */

#include "bsp_rtc.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * Backup 寄存器定义
 * ================================================================ */
#define RTC_BKP_MAGIC   RTC_BKP_DR1   /* 已配置标志       */
#define RTC_BKP_YEAR    RTC_BKP_DR2   /* Year  0~99      */
#define RTC_BKP_MONTH   RTC_BKP_DR3   /* Month 1~12      */
#define RTC_BKP_DATE    RTC_BKP_DR4   /* Date  1~31      */
#define RTC_MAGIC_VALUE 0xA5A5U

/* 默认值 (冷启动时写入) */
#define RTC_DEFAULT_YEAR    20U    /* 2020 */
#define RTC_DEFAULT_MONTH   1U
#define RTC_DEFAULT_DATE    1U
#define RTC_DEFAULT_HOUR    0U
#define RTC_DEFAULT_MIN     0U
#define RTC_DEFAULT_SEC     0U

/* ================================================================
 * 时间戳转换常量
 * ================================================================ */
#define SEC_PER_MIN   60U
#define SEC_PER_HOUR  3600U
#define SEC_PER_DAY   86400U

/* 1970-01-01 (Unix epoch) 到 2000-01-01 的天数 (30 年含 7 个闰年) */
#define DAYS_1970_2000  10957U

/* ================================================================
 * 运行时上下文
 * ================================================================ */
static RTC_HandleTypeDef rtc_handle;
static uint8_t          rtc_ready = 0;

/* ================================================================
 * 内部: 时间戳 ↔ 日期时间互转
 * ================================================================ */

/** @brief 判断是否为闰年 */
static int is_leap(uint8_t y)
{
    uint16_t year = 2000U + y;
    return (year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U));
}

/** @brief 每月天数 (非闰年) */
static const uint8_t days_in_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

/* ================================================================
 * 公开: 星期计算 (Tomohiko Sakamoto 算法)
 * ================================================================ */

uint8_t BSP_RTC_CalcWeekday(uint8_t year, uint8_t month, uint8_t date)
{
    if (month < 1 || month > 12 || date < 1 || date > 31) return 0;
    /* 将 1/2 月视为上一年的 13/14 月 */
    uint8_t m = month;
    if (m < 3) m += 12;
    int y = 2000 + year - (month < 3 ? 1 : 0);
    int w = (date + (13 * (m + 1)) / 5 + (y % 100) + (y % 100) / 4
             + (y / 100) / 4 + 5 * (y / 100)) % 7;
    /* 结果 0=Sat..6=Fri, 转为 1=Mon..7=Sun */
    return (uint8_t)((w + 6) % 7 + 1);
}

/** @brief UNIX 时间戳 → DateTime */
void BSP_RTC_ts_to_dt(uint32_t ts, BSP_RTC_DateTime_t *dt)
{
    dt->century = BSP_RTC_CENTURY;

    /* 时间部分 */
    dt->sec  = ts % 60U;   ts /= 60U;
    dt->min  = ts % 60U;   ts /= 60U;
    dt->hour = ts % 24U;

    /* 日期部分: 从 1970-01-01 起算的天数 */
    uint32_t days = ts / 24U;
    uint16_t y = 1970;
    while (1) {
        uint16_t yd = (is_leap((uint8_t)(y - 2000))) ? 366U : 365U;
        if (days < yd) break;
        days -= yd;
        y++;
    }
    dt->year = (uint8_t)(y - 2000U);
    dt->month = 1;
    for (int m = 0; m < 12; m++) {
        uint8_t md = days_in_month[m];
        if (m == 1 && is_leap(dt->year)) md = 29;
        if (days < md) { dt->month = (uint8_t)(m + 1); break; }
        days -= md;
    }
    dt->date = (uint8_t)(days + 1);
    dt->weekday = BSP_RTC_CalcWeekday(dt->year, dt->month, dt->date);
}

/** @brief DateTime → UNIX 时间戳 */
uint32_t BSP_RTC_dt_to_ts(const BSP_RTC_DateTime_t *dt)
{
    /* 从 1970 年到 dt 年的总天数 */
    uint32_t days = 0;
    for (uint16_t y = 1970; y < 2000U + dt->year; y++) {
        days += (y % 4U == 0U && (y % 100U != 0U || y % 400U == 0U)) ? 366U : 365U;
    }
    for (uint8_t m = 1; m < dt->month; m++) {
        uint8_t md = days_in_month[m - 1];
        if (m == 2 && is_leap(dt->year)) md = 29;
        days += md;
    }
    days += (uint32_t)(dt->date - 1);

    return days * SEC_PER_DAY + (uint32_t)dt->hour * SEC_PER_HOUR
           + (uint32_t)dt->min * SEC_PER_MIN + dt->sec;
}

/* ================================================================
 * 内部: Backup 寄存器辅助
 * ================================================================ */

static void rtc_backup_date(const BSP_RTC_DateTime_t *dt)
{
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_YEAR,  dt->year);
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_MONTH, dt->month);
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_DATE,  dt->date);
}

static void rtc_restore_date_to_hal(void)
{
    rtc_handle.DateToUpdate.Year  = (uint8_t)HAL_RTCEx_BKUPRead(&rtc_handle, RTC_BKP_YEAR);
    rtc_handle.DateToUpdate.Month = (uint8_t)HAL_RTCEx_BKUPRead(&rtc_handle, RTC_BKP_MONTH);
    rtc_handle.DateToUpdate.Date  = (uint8_t)HAL_RTCEx_BKUPRead(&rtc_handle, RTC_BKP_DATE);
    rtc_handle.DateToUpdate.WeekDay = 1; /* 不使用周几 */
}

/* ================================================================
 * 内部: 冷启动完整配置
 * ================================================================ */

static void rtc_cold_init(void)
{
    /* ---- 1. 配置时钟源 ---- */
    RCC_OscInitTypeDef osc = {0};
#if defined(BSP_RTC_CLK_LSE)
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState       = RCC_LSE_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&osc);

    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    HAL_RCCEx_PeriphCLKConfig(&pclk);
    __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
#elif defined(BSP_RTC_CLK_LSI)
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE;
    osc.LSIState       = RCC_LSI_ON;
    osc.LSEState       = RCC_LSE_OFF;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&osc);

    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSI;
    HAL_RCCEx_PeriphCLKConfig(&pclk);
#endif

    /* ---- 2. 使能 RTC ---- */
    __HAL_RCC_RTC_ENABLE();
    rtc_handle.Instance          = RTC;
    rtc_handle.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
    rtc_handle.Init.OutPut       = RTC_OUTPUTSOURCE_NONE;
    HAL_RTC_Init(&rtc_handle);

    /* ---- 3. 写入默认时间 (通过 HAL Time+Date) ---- */
    RTC_TimeTypeDef t = { RTC_DEFAULT_HOUR, RTC_DEFAULT_MIN, RTC_DEFAULT_SEC };
    HAL_RTC_SetTime(&rtc_handle, &t, RTC_FORMAT_BIN);

    /* HAL RTC_DateTypeDef: {WeekDay, Month, Date, Year} */
    RTC_DateTypeDef d = { .WeekDay = 0, .Month = RTC_DEFAULT_MONTH,
                          .Date = RTC_DEFAULT_DATE, .Year = RTC_DEFAULT_YEAR };
    HAL_RTC_SetDate(&rtc_handle, &d, RTC_FORMAT_BIN);

    /* ---- 4. Backup 寄存器保存日期 ---- */
    BSP_RTC_DateTime_t def = {
        .weekday = BSP_RTC_CalcWeekday(RTC_DEFAULT_YEAR, RTC_DEFAULT_MONTH, RTC_DEFAULT_DATE),
        .sec = RTC_DEFAULT_SEC, .min = RTC_DEFAULT_MIN, .hour = RTC_DEFAULT_HOUR,
        .date = RTC_DEFAULT_DATE, .month = RTC_DEFAULT_MONTH, .year = RTC_DEFAULT_YEAR,
        .century = BSP_RTC_CENTURY,
    };
    rtc_backup_date(&def);

    /* ---- 5. 标记已配置 ---- */
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_MAGIC, RTC_MAGIC_VALUE);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_RTC_Init(void)
{
    if (rtc_ready) return;

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    if (HAL_RTCEx_BKUPRead(&rtc_handle, RTC_BKP_MAGIC) != RTC_MAGIC_VALUE) {
        /* 冷启动: VBAT 掉电过 */
        rtc_cold_init();
    } else {
        /* 热启动: RTC 一直在跑 */
        rtc_handle.Instance = RTC;
        __HAL_RCC_RTC_ENABLE();
        HAL_RTC_WaitForSynchro(&rtc_handle);
        rtc_restore_date_to_hal();
    }

    rtc_ready = 1;
}

/* ----------------------------------------------------------------
 * DateTime 读写
 * ---------------------------------------------------------------- */

void BSP_RTC_GetDateTime(BSP_RTC_DateTime_t *dt)
{
    if (!rtc_ready || !dt) return;

    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    HAL_RTC_GetTime(&rtc_handle, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&rtc_handle, &d, RTC_FORMAT_BIN);

    dt->sec     = t.Seconds;
    dt->min     = t.Minutes;
    dt->hour    = t.Hours;
    dt->date    = d.Date;
    dt->month   = d.Month;
    dt->year    = d.Year;
    dt->century = BSP_RTC_CENTURY;
    dt->weekday = d.WeekDay;   /* HAL 内部已保证与日期一致 */
}

void BSP_RTC_SetDateTime(const BSP_RTC_DateTime_t *dt)
{
    if (!rtc_ready || !dt) return;

    /* HAL 层写入 Time + Date */
    RTC_TimeTypeDef t = { dt->hour, dt->min, dt->sec };
    HAL_RTC_SetTime(&rtc_handle, &t, RTC_FORMAT_BIN);

    /* HAL RTC_DateTypeDef: {WeekDay, Month, Date, Year} */
    RTC_DateTypeDef d = { .WeekDay = 0, .Month = dt->month,
                          .Date = dt->date, .Year = dt->year };
    HAL_RTC_SetDate(&rtc_handle, &d, RTC_FORMAT_BIN);

    /* 同步写入 Backup 寄存器，复位后不丢 */
    rtc_backup_date(dt);
}

/* ----------------------------------------------------------------
 * UNIX 时间戳读写
 * ---------------------------------------------------------------- */

uint32_t BSP_RTC_GetTimestamp(void)
{
    BSP_RTC_DateTime_t dt;
    BSP_RTC_GetDateTime(&dt);
    return BSP_RTC_dt_to_ts(&dt);
}

void BSP_RTC_SetTimestamp(uint32_t ts)
{
    BSP_RTC_DateTime_t dt;
    BSP_RTC_ts_to_dt(ts, &dt);
    BSP_RTC_SetDateTime(&dt);
}
