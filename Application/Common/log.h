/**
 * @file    log.h
 * @brief   分级日志 —— 基于 bsp_usart TX 环形缓冲的薄封装
 * @note    输出格式: [   tick] L TAG: message\r\n
 *                       │      │  │
 *                       │      │  └─ 模块标签 (LOG_TAG)
 *                       │      └──── 级别字符 E/W/I/D
 *                       └─────────── HAL_GetTick() 毫秒时间戳
 *
 *          用法 —— 每个 .c 文件顶部定义自己的标签, 之后直接写 LOG_x:
 *              #define LOG_TAG "NET"
 *              #include "log.h"
 *              ...
 *              LOG_I("socket %d connected", sock);
 *              LOG_E("timeout after %lu ms", (unsigned long)ms);
 *
 *          零开销: 级别过滤在【预处理期】完成, 低于 LOG_LEVEL 的语句
 *          整条被替换成 ((void)0) —— 连格式化字符串都不进 Flash.
 *          Release 构建加 -DLOG_LEVEL=LOG_LVL_WARN 即可全部编掉.
 *
 *          上下文: 任务和 ISR 都可以调用. 非阻塞 —— 格式化后拷入 USART
 *          环形缓冲立即返回, 由 DMA/IT 在后台发出.
 *          注: 在 ISR 里调用会占用中断上下文约 100us (vsnprintf 开销),
 *          高频中断里请克制使用.
 *
 *          支持 %f —— 工程在 Debug 构建链接了 -Wl,-u,_printf_float
 *          (约 10.8KB Flash, 见根 CMakeLists.txt), newlib-nano 因此获得
 *          浮点格式化能力, LOG 与 printf 都能直接打印小数.
 *
 *          printf: 本模块同时提供 __io_putchar, 把 printf 重定向到与 LOG
 *          相同的串口 (端口由 LOG_Init() 决定), 走同一个 TX 环形缓冲,
 *          因此 printf 也是非阻塞的.
 *          但 printf 只应用于临时排查 —— 它没有分级、没有时间戳、没有模块
 *          标签, 在 ISR 里还会长时间占用中断上下文. 提交前请改回 LOG_x.
 */

#ifndef __LOG_H__
#define __LOG_H__

#include <stdint.h>
#include "bsp_usart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 级别
 * ================================================================ */

typedef enum {
    LOG_LVL_NONE = 0,   /* 全部关闭 */
    LOG_LVL_ERROR,      /* 错误 —— 功能不可用 */
    LOG_LVL_WARN,       /* 警告 —— 可继续但需关注 */
    LOG_LVL_INFO,       /* 关键流程节点 */
    LOG_LVL_DEBUG,      /* 细节, 默认级别 */
    LOG_LVL_COUNT
} Log_Level_t;

/* 编译期默认级别 —— Release: -DLOG_LEVEL=LOG_LVL_WARN */
#ifndef LOG_LEVEL
#define LOG_LEVEL   LOG_LVL_DEBUG
#endif

/* 模块标签 —— 每个 .c 在自己文件顶部 #define, 不定义则用 "APP" */
#ifndef LOG_TAG
#define LOG_TAG     "APP"
#endif

/* ================================================================
 * 日志宏 (预处理期分级)
 * ================================================================ */

#if LOG_LEVEL >= LOG_LVL_ERROR
#define LOG_E(...)  LOG_Output(LOG_LVL_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOG_E(...)  ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_WARN
#define LOG_W(...)  LOG_Output(LOG_LVL_WARN, LOG_TAG, __VA_ARGS__)
#else
#define LOG_W(...)  ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_INFO
#define LOG_I(...)  LOG_Output(LOG_LVL_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOG_I(...)  ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_DEBUG
#define LOG_D(...)  LOG_Output(LOG_LVL_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#define LOG_D(...)  ((void)0)
#endif

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  初始化日志模块
 * @param  port 输出串口 (当前工程约定 USART1)
 * @note   必须在对应的 BSP_USART_Init/InitEx 之后调用.
 *         未调用时所有日志被静默丢弃.
 */
void LOG_Init(BSP_USART_Id_t port);

/**
 * @brief  运行期调整级别 (覆盖编译期 LOG_LEVEL 的默认值)
 * @param  lvl LOG_LVL_NONE 关闭全部; 不能高于编译期 LOG_LEVEL
 *             (高于的部分已被编译掉, 调也调不出来)
 */
void LOG_SetLevel(Log_Level_t lvl);

/** @brief 查询当前运行期级别 */
Log_Level_t LOG_GetLevel(void);

/**
 * @brief  查询因 USART 缓冲满而丢弃的字节数
 * @note   持续增长说明日志量超过了串口带宽, 或波特率需要提高.
 *         排错时先看这个数, 确认你看到的日志是完整的.
 */
uint32_t LOG_GetDropped(void);

/** @brief 清零丢弃计数 */
void LOG_ClearDropped(void);

/**
 * @brief  输出一条日志 (一般通过 LOG_E/W/I/D 宏调用, 不直接使用)
 * @param  lvl 级别
 * @param  tag 模块标签
 * @param  fmt printf 风格格式串 (支持 %f)
 */
void LOG_Output(Log_Level_t lvl, const char *tag, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __LOG_H__ */
