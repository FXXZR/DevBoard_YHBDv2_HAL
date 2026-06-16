/**
 * @file    test_rtc.c
 * @brief   RTC 交互式测试 —— 定时打印 DateTime + UNIX 时间戳
 * @note    默认每 5s printf 输出。支持串口命令 (USART1):
 *            T HH:MM:SS        仅设置时间 (保留日期)
 *            D YY-MM-DD        仅设置日期 (保留时间)
 *            S YY-MM-DD HH:MM:SS  同时设置日期时间
 *            U                 显示当前 UNIX 时间戳
 *            I N               调整输出间隔 (秒, 1~999)
 *            H                 显示帮助
 *          命令输入有回显，回车执行。
 */

#include "test.h"
#include "stm32f1xx_hal.h"

/* ================================================================
 * 命令行缓冲
 * ================================================================ */

#define CMD_BUF_SIZE  48

static char    cmd_buf[CMD_BUF_SIZE];
static uint8_t cmd_idx = 0;

static int cmd_feed(uint8_t ch)
{
    if (ch == '\r' || ch == '\n') {
        if (cmd_idx > 0) {
            cmd_buf[cmd_idx] = '\0';
            cmd_idx = 0;
            printf("\r\n");
            return 1;
        }
        return 0;
    }
    if (ch == 0x08 || ch == 0x7F) {
        if (cmd_idx > 0) { cmd_idx--; printf("\b \b"); }
        return 0;
    }
    if (ch >= 0x20 && ch < 0x7F && cmd_idx < CMD_BUF_SIZE - 1) {
        cmd_buf[cmd_idx++] = (char)ch;
        BSP_USART_Send(BSP_USART1, &ch, 1);
    }
    return 0;
}

/* ================================================================
 * 手工解析
 * ================================================================ */

static const char *skip_space(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int parse_2dig(const char **sp)
{
    const char *s = *sp;
    if (*s < '0' || *s > '9') return -1;
    int v = (*s++ - '0');
    if (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    *sp = s;
    return v;
}

static int parse_time(const char *s, int *h, int *m, int *sec)
{
    *h = 0; *m = 0; *sec = 0;
    s = skip_space(s);
    if ((*h = parse_2dig(&s)) < 0) return 0;
    if (*s++ != ':') return 0;
    if ((*m = parse_2dig(&s)) < 0) return 0;
    if (*s++ != ':') return 0;
    if ((*sec = parse_2dig(&s)) < 0) return 0;
    return 3;
}

static int parse_date(const char *s, int *y, int *m, int *d)
{
    *y = 0; *m = 0; *d = 0;
    s = skip_space(s);
    if ((*y = parse_2dig(&s)) < 0) return 0;
    if (*s++ != '-') return 0;
    if ((*m = parse_2dig(&s)) < 0) return 0;
    if (*s++ != '-') return 0;
    if ((*d = parse_2dig(&s)) < 0) return 0;
    return 3;
}

static int parse_interval(const char *s, int *n)
{
    *n = 0;
    s = skip_space(s);
    if (*s < '0' || *s > '9') return 0;
    while (*s >= '0' && *s <= '9') {
        *n = *n * 10 + (*s - '0');
        if (*n > 999) return 0;
        s++;
    }
    return 1;
}

/* ================================================================
 * 命令执行
 * ================================================================ */

static void cmd_execute(const char *line)
{
    line = skip_space(line);
    if (!*line) return;
    char cmd = (char)(*line++);

    switch (cmd) {
    /* ---- T: 仅设置时间 (保留日期) ---- */
    case 'T': case 't': {
        int h, m, s;
        if (parse_time(line, &h, &m, &s) == 3) {
            if (h <= 23 && m <= 59 && s <= 59) {
                BSP_RTC_DateTime_t dt;
                BSP_RTC_GetDateTime(&dt);
                dt.hour = (uint8_t)h; dt.min = (uint8_t)m; dt.sec = (uint8_t)s;
                BSP_RTC_SetDateTime(&dt);
                printf("OK: %02d:%02d:%02d\r\n", h, m, s);
            } else printf("ERR: Time range\r\n");
        } else printf("ERR: Usage: T HH:MM:SS\r\n");
        break;
    }

    /* ---- D: 仅设置日期 (保留时间) ---- */
    case 'D': case 'd': {
        int y, m, d;
        if (parse_date(line, &y, &m, &d) == 3) {
            if (y <= 99 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
                BSP_RTC_DateTime_t dt;
                BSP_RTC_GetDateTime(&dt);
                dt.year = (uint8_t)y; dt.month = (uint8_t)m; dt.date = (uint8_t)d;
                BSP_RTC_SetDateTime(&dt);
                printf("OK: 20%02d-%02d-%02d\r\n", y, m, d);
            } else printf("ERR: Date range\r\n");
        } else printf("ERR: Usage: D YY-MM-DD\r\n");
        break;
    }

    /* ---- S: 同时设置日期和时间 ---- */
    case 'S': case 's': {
        int y, mo, d, h, mi, sec;
        line = skip_space(line);
        const char *p = line;
        if (parse_date(p, &y, &mo, &d) == 3) {
            while (*p && *p != ' ') p++;
            if (parse_time(p, &h, &mi, &sec) == 3) {
                if (y <= 99 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 &&
                    h <= 23 && mi <= 59 && sec <= 59) {
                    BSP_RTC_DateTime_t dt = {
                        .weekday = BSP_RTC_CalcWeekday((uint8_t)y, (uint8_t)mo, (uint8_t)d),
                        .sec = (uint8_t)sec, .min = (uint8_t)mi, .hour = (uint8_t)h,
                        .date = (uint8_t)d, .month = (uint8_t)mo, .year = (uint8_t)y,
                        .century = BSP_RTC_CENTURY,
                    };
                    BSP_RTC_SetDateTime(&dt);
                    printf("OK: 20%02d-%02d-%02d %02d:%02d:%02d  ts=%lu\r\n",
                           y, mo, d, h, mi, sec,
                           (unsigned long)BSP_RTC_GetTimestamp());
                } else printf("ERR: Range\r\n");
            } else printf("ERR: Usage: S YY-MM-DD HH:MM:SS\r\n");
        } else printf("ERR: Usage: S YY-MM-DD HH:MM:SS\r\n");
        break;
    }

    /* ---- U: 显示当前 UNIX 时间戳 ---- */
    case 'U': case 'u':
        printf("UNIX ts = %lu\r\n", (unsigned long)BSP_RTC_GetTimestamp());
        break;

    /* ---- I: 调整打印间隔 ---- */
    case 'I': case 'i': {
        int n;
        if (parse_interval(line, &n) && n >= 1 && n <= 999) {
            extern uint32_t rtc_print_interval_s;
            rtc_print_interval_s = (uint32_t)n;
            printf("OK: Interval = %d s\r\n", n);
        } else printf("ERR: Usage: I N  (N=1..999)\r\n");
        break;
    }

    /* ---- H: 帮助 ---- */
    case 'H': case 'h': case '?':
        printf("--- RTC Test Commands ---\r\n");
        printf("  T HH:MM:SS             Set time only\r\n");
        printf("  D YY-MM-DD             Set date only\r\n");
        printf("  S YY-MM-DD HH:MM:SS    Set datetime\r\n");
        printf("  U                      Show UNIX timestamp\r\n");
        printf("  I N                    Print interval (s)\r\n");
        printf("  H                      Help\r\n");
        break;

    default:
        printf("ERR: Unknown '%c'. H=Help\r\n", cmd);
        break;
    }
}

/* ================================================================
 * 测试入口
 * ================================================================ */

uint32_t rtc_print_interval_s = 5;

void Test_RTC_Interactive(void)
{
    BSP_RTC_Init();

    printf("\r\n==== RTC Interactive Test ====\r\n");
    printf(" Commands: T/D/S/U/I/H\r\n");
    printf(" Interval: %lu s\r\n\r\n", (unsigned long)rtc_print_interval_s);

    uint32_t last_tick = 0;

    while (1) {
        uint32_t now = HAL_GetTick();

        /* 定时打印 */
        if (now - last_tick >= rtc_print_interval_s * 1000U) {
            last_tick = now;
            BSP_RTC_DateTime_t dt;
            BSP_RTC_GetDateTime(&dt);
            uint32_t ts = BSP_RTC_dt_to_ts(&dt);  /* 纯算法, 不重复读 RTC */
            printf("[RTC] 20%02d-%02d-%02d %02d:%02d:%02d  ts=%lu\r\n",
                   dt.year, dt.month, dt.date,
                   dt.hour, dt.min, dt.sec, (unsigned long)ts);
        }

        /* 串口命令 */
        uint8_t ch;
        while (BSP_USART_Read(BSP_USART1, &ch) == 0) {
            if (cmd_feed(ch)) {
                cmd_execute(cmd_buf);
            }
        }

        BSP_SYSTICK_DelayMS(100);
    }
}
