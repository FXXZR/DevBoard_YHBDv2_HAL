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

/* Magic 值 —— 冷/热启动判定 (BKP_DR1 的内容) */
static const uint16_t rtc_magic_value = 0xA5A5U;

/* ================================================================
 * 默认时间 (冷启动时写入)
 *
 * 直接复用头文件里的 BSP_RTC_DateTime_t, 不再另造一个同构类型。
 * weekday 不写死: 由 BSP_RTC_CalcWeekday 在初始化时按日期算出, 免得
 * 改默认日期时还要手工同步星期。
 * ================================================================ */

static const BSP_RTC_DateTime_t rtc_default = {
    .sec     = 0U,
    .min     = 0U,
    .hour    = 0U,
    .date    = 1U,
    .month   = 1U,
    .year    = 20U,              /* 20 → 2020 */
    .century = BSP_RTC_CENTURY,
};

/* ================================================================
 * 时间戳转换常量
 * ================================================================ */
static const uint32_t sec_per_min  = 60U;
static const uint32_t sec_per_hour = 3600U;
static const uint32_t sec_per_day  = 86400U;

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

    return days * sec_per_day + (uint32_t)dt->hour * sec_per_hour
           + (uint32_t)dt->min * sec_per_min + dt->sec;
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
 * 内部: RTC 时钟源配置 (冷/热启动均需调用)
 *
 * LSI 在 VDD 主电源域, 每次系统复位后硬件自动清零 LSION 位;
 * LSE 在 VBAT 备份域, 复位后保持。因此无论冷启动还是热启动,
 * 都必须重新使能所选时钟源并设置 RTC 时钟选择。
 * ================================================================ */

static void rtc_config_clocksource(void)
{
    RCC_OscInitTypeDef osc = {0};

#if defined(BSP_RTC_CLK_LSE)
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState       = RCC_LSE_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        /* LSE 启动失败 (超时或未焊接晶振), 回退到 LSI */
        osc.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE;
        osc.LSIState       = RCC_LSI_ON;
        osc.LSEState       = RCC_LSE_OFF;
        HAL_RCC_OscConfig(&osc);
        RCC_PeriphCLKInitTypeDef pclk = {0};
        pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
        pclk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSI;
        HAL_RCCEx_PeriphCLKConfig(&pclk);
    } else {
        RCC_PeriphCLKInitTypeDef pclk = {0};
        pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
        pclk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
        HAL_RCCEx_PeriphCLKConfig(&pclk);
        __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
    }
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
}

/* ================================================================
 * 内部: 冷启动完整配置 (含默认值写入 + BKP 初始化)
 * ================================================================ */

static void rtc_cold_init(void)
{
    /* ---- 1. 配置时钟源 ---- */
    rtc_config_clocksource();

    /* ---- 2. 使能并初始化 RTC ---- */
    __HAL_RCC_RTC_ENABLE();
    rtc_handle.Instance          = RTC;
    rtc_handle.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
    rtc_handle.Init.OutPut       = RTC_OUTPUTSOURCE_NONE;
    HAL_RTC_Init(&rtc_handle);

    /* ---- 3. 写入默认时间 (通过 HAL Time+Date) ---- */
    RTC_TimeTypeDef t = { .Hours = rtc_default.hour,
                          .Minutes = rtc_default.min,
                          .Seconds = rtc_default.sec };
    HAL_RTC_SetTime(&rtc_handle, &t, RTC_FORMAT_BIN);

    /* HAL RTC_DateTypeDef: {WeekDay, Month, Date, Year} */
    RTC_DateTypeDef d = { .WeekDay = 0, .Month = rtc_default.month,
                          .Date = rtc_default.date, .Year = rtc_default.year };
    HAL_RTC_SetDate(&rtc_handle, &d, RTC_FORMAT_BIN);

    /* ---- 4. Backup 寄存器保存日期 ---- */
    BSP_RTC_DateTime_t def = rtc_default;
    def.weekday = BSP_RTC_CalcWeekday(def.year, def.month, def.date);
    rtc_backup_date(&def);

    /* ---- 5. 标记已配置 ---- */
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_MAGIC, rtc_magic_value);
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

    uint32_t magic = HAL_RTCEx_BKUPRead(&rtc_handle, RTC_BKP_MAGIC);
    if (magic != rtc_magic_value) {
        /* 冷启动: VBAT 掉电过, 完整配置 时钟源 + RTC + 默认值 + BKP */
        rtc_cold_init();
    } else {
        /* 热启动: RTC 一直在跑, 重新使能时钟源 + 重建 APB 接口 + 恢复日期
         *
         * 注意: 无论 LSE 还是 LSI, 系统复位后都必须重新调用
         * rtc_config_clocksource().
         *   - LSI: 在 VDD 主域, 复位后 LSION 位被硬件清零 → 必须重新使能
         *   - LSE: 虽然在 VBAT 备份域可保留, 但某些复位场景下仍可能丢失,
         *     且 HAL_RCC_OscConfig(LSE) 对已使能的 LSE 是幂等的
         */
        rtc_config_clocksource();
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
