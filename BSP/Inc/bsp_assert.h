/**
 * @file    bsp_assert.h
 * @brief   BSP 断言 —— 零依赖的 panic 通道
 * @note    设计原则: **panic 路径必须比它要诊断的故障更底层**.
 *          它不能依赖任何可能正是故障原因的东西 —— 缓冲、DMA、中断、RTOS,
 *          一个都不能有.
 *
 *          因此本模块刻意【不 include】log.h / bsp_usart.h / FreeRTOS.h,
 *          而是直接轮询操作 USART1 的寄存器:
 *            - 不走 log 的 TX 环形缓冲 —— 那是非阻塞的, 缓冲满时静默丢弃.
 *              而断言恰恰最容易在"系统已不正常、日志刷屏"时触发, 那时候
 *              缓冲多半是满的, 最关键的一条会被丢掉.
 *            - 不走 DMA —— DMA 可能正卡着, 或者正在和你抢 DR.
 *            - 不调任何 FreeRTOS API —— panic 时内核状态可能已经损坏.
 *
 *          输出口硬编码 USART1: 断言只在调试/故障阶段使用, 这个口不会变.
 *
 *          停机策略 (编译期选择, 见根 CMakeLists.txt):
 *            Debug   (定义 BSP_ASSERT_HALT) → 关中断后死循环, 等调试器看现场
 *            Release (未定义)                → 日志同步发完后 NVIC_SystemReset()
 *                                              现场设备死循环 = 变砖, 必须自恢复
 *
 *          层级: BSP 层基础设施, Application / Middleware / BSP 自身都可调用
 *                (全部是向下依赖, 不构成环).
 *
 *          参考: ST RM0008 §27 USART
 */

#ifndef __BSP_ASSERT_H__
#define __BSP_ASSERT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FreeRTOSConfig.h 会 include 本文件, 而它有可能被汇编文件 (portasm.s)
 * 引用 —— 汇编器看不懂 C 声明, 所以用各工具链的汇编宏把它们挡掉.
 * 汇编代码不会调用断言, 挡掉不影响任何功能. */
#if !defined(__ASSEMBLER__) && !defined(__ASM) && !defined(__IASMARM__)

#if defined(__GNUC__)
  #define BSP_NORETURN  __attribute__((noreturn))
#else
  #define BSP_NORETURN
#endif

/* ================================================================
 * 断言
 * ================================================================ */

/**
 * @brief  断言失败处理 —— 一般通过 BSP_ASSERT 宏调用, 不直接使用
 * @param  file 源文件名 (__FILE__, 已由 -ffile-prefix-map 缩短)
 * @param  line 行号 (__LINE__)
 * @param  expr 失败的表达式原文 (#x 字符串化)
 * @note   不返回: Debug 停机 / Release 复位
 */
BSP_NORETURN void BSP_AssertFailed(const char *file, int line, const char *expr);

/**
 * @brief  断言宏
 * @note   只用于【编程错误】—— 即外部世界无法触发、只可能来自代码写错的条件.
 *
 *         环境错误 (SPI 超时 / Flash BUSY / CRC 错 / 器件不在) 请用
 *         "返回值 + 日志 + 降级": 把它做成断言会导致现场设备一超时就死机.
 *
 *         判断准则:
 *           编程错误 → BSP_ASSERT        (传错枚举 / 忘了 Init / ISR 里调阻塞 API)
 *           环境错误 → 返回值 + LOG_E    (超时 / 器件故障 / 干扰)
 *           坏输入   → 返回值            (用户操作 / 网络包 / 坏数据)
 */
#define BSP_ASSERT(x)                                                   \
    do {                                                                \
        if (!(x)) { BSP_AssertFailed(__FILE__, __LINE__, #x); }         \
    } while (0)

/* ================================================================
 * 通用 panic 原语
 *
 * 供 FreeRTOS 的 hook (栈溢出 / 内存耗尽) 复用, 避免那些地方各自
 * 复制一遍"关中断 + 夺 UART"的逻辑.
 * 用法: BSP_PanicBegin(); BSP_PanicPuts(...); BSP_PanicPutInt(...); BSP_PanicEnd();
 * ================================================================ */

/** @brief 关中断并夺取 USART1 (幂等, 可重复调用) */
void BSP_PanicBegin(void);

/** @brief 轮询直写一个字符串到 USART1 (不经过任何缓冲; NULL 安全) */
void BSP_PanicPuts(const char *s);

/** @brief 轮询直写一个十进制整数 */
void BSP_PanicPutInt(int32_t v);

/**
 * @brief 结束 panic —— 【不返回】
 * @note  Debug: 死循环等调试器; Release: NVIC_SystemReset()
 *        输出是轮询直写的, 调用本函数时日志已完整发出.
 */
BSP_NORETURN void BSP_PanicEnd(void);

#endif /* 非汇编 */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ASSERT_H__ */
