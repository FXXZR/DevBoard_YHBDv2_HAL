/**
 * @file    log.c
 * @brief   分级日志实现 —— 格式化后交给 bsp_usart 的 TX 环形缓冲
 * @note    本模块刻意保持"薄": 不做缓冲、不做任务、不做流控.
 *          非阻塞发送的全部机制都在 bsp_usart 里 (环形缓冲 + DMA 分段续传),
 *          这里只负责「加时间戳和标签、格式化、入队」三件事.
 *
 *          因此日志的可靠性上限由 bsp_usart 决定 —— 串口带宽不够时
 *          数据会丢, 用 LOG_GetDropped() 观察.
 */

#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/* 单行最大长度 (含时间戳/标签/正文/CRLF).
 * 注意: 这个缓冲在栈上 —— 会占用调用者任务栈 128 字节,
 *       FreeRTOS 任务栈分配时要把这部分算进去 (usStackDepth 以字为单位,
 *       128 字节 = 32 字). */
#define LOG_LINE_MAX    128U

/* ================================================================
 * 模块状态
 * ================================================================ */

static BSP_USART_Id_t     s_port    = BSP_USART1;
static Log_Level_t        s_level   = LOG_LVL_DEBUG;
static uint8_t            s_ready   = 0;

/* 丢弃字节数 —— 诊断用, 非精确统计 (增减不做原子保护, 只用于观察趋势) */
static volatile uint32_t  s_dropped = 0;

/* 级别 → 单字符标记, 下标对应 Log_Level_t */
static const char kLevelChar[LOG_LVL_COUNT] = { '?', 'E', 'W', 'I', 'D' };

/* ================================================================
 * 时间戳源
 *
 * HAL_GetTick() 在调度器启动前恒为 0 —— HAL_IncTick() 由
 * vApplicationTickHook() 驱动 (见 Common/rtos_port_hooks.c), 而 FreeRTOS 的
 * tick 中断要等 vTaskStartScheduler() 才跑起来.
 * 偏偏启动阶段最需要计时 (各外设 init 耗时 / Flash 与 I2C 超时),
 * 一整段全是 [0] 的日志等于没有时间信息.
 *
 * 所以启动前改用 DWT 周期计数器 (Cortex-M3 自带, 独立于 SysTick),
 * 在调度器启动的那一刻把启动耗时定格, 之后接回 HAL_GetTick() ——
 * 时间戳始终是「上电以来的毫秒数」, 单调且连续, 不会出现回跳.
 * ================================================================ */

static uint32_t s_dwt_base   = 0;   /* LOG_Init 时刻的 DWT->CYCCNT */
static uint32_t s_boot_ms    = 0;   /* 定格: 上电到调度器启动的耗时 */
static uint8_t  s_sched_seen = 0;

/** @brief 启动 DWT 周期计数器 */
static void log_dwt_start(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* 使能跟踪单元 (DWT 的总开关) */
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;            /* 启动周期计数 */
    s_dwt_base = DWT->CYCCNT;
}

/** @brief 调度器启动前的毫秒数 (基于 DWT 周期计数)
 *  @note  32 位 @72MHz 约 59.6s 回绕. 只用于启动阶段 (通常 <1s),
 *         不做回绕处理; 若 DWT 不可用则恒为 0, 退化成原行为. */
static uint32_t log_dwt_ms(void)
{
    return (DWT->CYCCNT - s_dwt_base) / (SystemCoreClock / 1000U);
}

/** @brief 上电以来的毫秒数 */
static uint32_t log_now_ms(void)
{
    if (!s_sched_seen) {
        if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
            return log_dwt_ms();
        }
        /* 不能用 log_dwt_ms() 直接当启动耗时: 本函数首次被调用时可能已远在
         * 调度器启动之后 (中间一段没有日志), 那时 DWT 早走过一大截, 直接定格
         * 会把这段重复计算 —— 表现为时间戳越走越快 (实测翻了一倍).
         * 改成用两个时钟的差值反推: DWT 从 LOG_Init 起算, HAL tick 从调度器
         * 启动起算, 两者之差恒为启动耗时, 与「何时发现」无关. */
        uint32_t dwt = log_dwt_ms();
        uint32_t hal = HAL_GetTick();
        s_boot_ms    = (dwt >= hal) ? (dwt - hal) : 0U;   /* DWT 不可用时退化为 0 */
        s_sched_seen = 1;
    }
    return s_boot_ms + HAL_GetTick();
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void LOG_Init(BSP_USART_Id_t port)
{
    s_port       = port;
    s_level      = (Log_Level_t)LOG_LEVEL;
    s_dropped    = 0;
    s_boot_ms    = 0;
    s_sched_seen = 0;
    s_ready      = 1;

    /* 必须在任何一条日志之前 —— 启动阶段的计时靠它 */
    log_dwt_start();
}

void LOG_SetLevel(Log_Level_t lvl)
{
    if (lvl >= LOG_LVL_NONE && lvl < LOG_LVL_COUNT) s_level = lvl;
}

Log_Level_t LOG_GetLevel(void)
{
    return s_level;
}

uint32_t LOG_GetDropped(void)
{
    return s_dropped;
}

void LOG_ClearDropped(void)
{
    s_dropped = 0;
}

void LOG_Output(Log_Level_t lvl, const char *tag, const char *fmt, ...)
{
    /* 运行期过滤是第二道闸门 —— 宏已在预处理期挡掉大部分 */
    if (!s_ready || !fmt || lvl <= LOG_LVL_NONE || lvl > s_level) return;

    char line[LOG_LINE_MAX];

    /* ---- 头部: "[" + 右对齐 tick + "] " + 级别 + " " + 标签 + ": " ---- */
    int n = snprintf(line, sizeof(line), "[%8lu] %c %s: ",
                     (unsigned long)log_now_ms(),
                     kLevelChar[lvl], tag ? tag : "?");
    if (n < 0 || (size_t)n >= sizeof(line) - 2U) return;

    /* ---- 正文: 预留 2 字节给 CRLF (room 含 vsnprintf 的 NUL 位) ---- */
    size_t room = sizeof(line) - (size_t)n - 2U;

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, room, fmt, ap);
    va_end(ap);
    if (m < 0) return;

    /* vsnprintf 返回的是"本该写入的长度", 截断时应取实际写入量 */
    size_t body = ((size_t)m < room) ? (size_t)m : (room - 1U);
    size_t len  = (size_t)n + body;

    line[len++] = '\r';
    line[len++] = '\n';

    /* 入队即返回 —— 满了也不阻塞, 记一笔丢弃量 */
    uint16_t sent = BSP_USART_Write(s_port, (const uint8_t *)line, (uint16_t)len);
    if (sent < len) s_dropped += (uint32_t)(len - sent);
}

/* ================================================================
 * printf 重定向 (newlib) —— Start/syscalls.c 的 _write 逐字符调用这里
 *
 * 放在本模块而不是 bsp_usart.c 的原因: 控制台端口由 LOG_Init() 决定,
 * printf 与 LOG 自然走同一路串口, 换口依然只需要改一处.
 *
 * 字符走 bsp_usart 的 TX 环形缓冲, 因此是非阻塞的. 
 * 但也因此使用之前需要确保LOG_Init() 已经被调用, 否则会写入无效串口.
 * 使用约定: printf 只用于临时排查, 提交前应改回 LOG_x 或删除.
 * 详见 Start/syscalls.c 中 _write 的说明.
 * ================================================================ */

int __io_putchar(int ch)
{
    if (ch == '\n') {
        static const uint8_t cr = '\r';
        BSP_USART_Write(s_port, &cr, 1);
    }
    uint8_t c = (uint8_t)ch;
    BSP_USART_Write(s_port, &c, 1);
    return ch;
}
