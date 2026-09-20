/**
 * @file    bsp_assert.c
 * @brief   BSP 断言实现 —— 零依赖 panic 通道 (轮询直写 USART1)
 * @note    本文件刻意【不 include】log.h / bsp_usart.h / FreeRTOS.h, 原因见
 *          bsp_assert.h 的文件头说明. 这里只依赖 CMSIS 寄存器定义.
 *
 *          关中断用 CMSIS 的 __disable_irq() 而不是 FreeRTOS 的
 *          taskDISABLE_INTERRUPTS():
 *            ① 保持本模块零 RTOS 依赖;
 *            ② 后者只是 vPortRaiseBASEPRI() —— 把 BASEPRI 抬到
 *               configMAX_SYSCALL_INTERRUPT_PRIORITY, 只屏蔽 NVIC 优先级
 *               >= 5 的中断, 优先级 0~4 的高优先级中断【照常触发】.
 *               panic 要的是全冻结, 所以用 cpsid i.
 *
 *          注意 __disable_irq() 管不着 DMA —— DMA 是独立的总线主设备,
 *          中断关了它照样搬数据. 所以必须显式关掉 USART1_TX 的 DMA 通道.
 *
 *          参考: ST RM0008 §27 USART
 */

#include "bsp_assert.h"
#include "stm32f1xx_hal.h"      /* 只为 CMSIS 寄存器定义, 不使用任何 HAL API */

/* ================================================================
 * 硬件约定
 * ================================================================ */

/* 输出口 —— 与 Application/main.c 的 LOG_Init(BSP_USART1) 保持一致 */
#define PANIC_UART          USART1

/* USART1_TX 的 DMA 通道 —— 见 BSP/Src/bsp_usart.c 的 usart_hw_map[].
 * ★ 若那边改了映射, 这里必须同步改. */
#define PANIC_UART_TX_DMA   DMA1_Channel4

/* 单字符轮询重试上限 —— 约几十毫秒 @72MHz.
 * 有这个上限, USART1 未使能时不会死等: 一个卡死的 panic 处理器比没有更糟,
 * 因为此时中断已关, 连看门狗都救不回来. */
#define PANIC_SPIN_LIMIT    400000U

/* ================================================================
 * 内部状态
 * ================================================================ */

/* 0 = UART 不可用, 后续所有输出直接放弃 (避免每个字符都白等一轮) */
static uint8_t s_uart_usable = 1U;

/* ================================================================
 * 底层输出原语
 * ================================================================ */

static void panic_putc(char c)
{
    uint32_t guard;

    if (!s_uart_usable) return;

    guard = PANIC_SPIN_LIMIT;
    while (!(PANIC_UART->SR & USART_SR_TXE)) {
        if (--guard == 0U) { s_uart_usable = 0U; return; }
    }

    PANIC_UART->DR = (uint16_t)(uint8_t)c;
}

/**
 * @brief  夺取 UART —— 停掉 log 模块的 DMA
 * @note   不停的话两边都在写 DR, 输出会交错成乱码.
 *         只动 DMAT 位和它对应的通道, 不碰 bsp_usart 的任何软件状态
 *         (panic 路径不能要求别的模块状态是好的).
 */
static void panic_uart_takeover(void)
{
    uint32_t guard;

    /* 检查外设是否已上电/使能 —— 极早期启动阶段 (LOG_Init 之前) 会走进这里.
     * 直接判定不可用, 比等 400000 次循环快得多. */
    if (!(RCC->APB2ENR & RCC_APB2ENR_USART1EN) ||
        !(PANIC_UART->CR1 & USART_CR1_UE)) {
        s_uart_usable = 0U;
        return;
    }

    /* ① 断 DMA 请求 + 停通道本身 */
    if (PANIC_UART->CR3 & USART_CR3_DMAT) {
        PANIC_UART->CR3 &= ~USART_CR3_DMAT;         /* 先断请求 */
        PANIC_UART_TX_DMA->CCR &= ~DMA_CCR_EN;      /* 再停通道 */
    }

    /* ② 等当前字节真正移出, 保证接管前的输出不被截断 (最多一字符时间) */
    guard = PANIC_SPIN_LIMIT;
    while (!(PANIC_UART->SR & USART_SR_TC)) {
        if (--guard == 0U) break;
    }

    /* ③ 读 SR + DR 清掉 ORE/RXNE 等标志, 避免残留状态影响后续发送 */
    (void)PANIC_UART->SR;
    (void)PANIC_UART->DR;
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_PanicBegin(void)
{
    /* 顺序: 先冻结, 再夺权.
     * 反过来的话, log 的 DMA 完成中断可能在输出途中插进来重装 DMA,
     * 把 UART 抢回去, 输出就断在半截. */
    __disable_irq();
    panic_uart_takeover();
}

void BSP_PanicPuts(const char *s)
{
    if (!s) return;
    while (*s) panic_putc(*s++);
}

void BSP_PanicPutInt(int32_t v)
{
    char     buf[12];
    int      i = 0;
    uint32_t u;

    if (v < 0) { panic_putc('-'); u = (uint32_t)(-(int64_t)v); }
    else       { u = (uint32_t)v; }

    if (u == 0U) { panic_putc('0'); return; }

    while (u) { buf[i++] = (char)('0' + (u % 10U)); u /= 10U; }
    while (i > 0) panic_putc(buf[--i]);
}

BSP_NORETURN void BSP_AssertFailed(const char *file, int line, const char *expr)
{
    BSP_PanicBegin();

    BSP_PanicPuts("\r\n*** ASSERT  ");
    BSP_PanicPuts(file ? file : "?");
    BSP_PanicPuts(":");
    BSP_PanicPutInt(line);
    BSP_PanicPuts("  [");
    BSP_PanicPuts(expr ? expr : "?");
    BSP_PanicPuts("]\r\n");

    BSP_PanicEnd();
}

BSP_NORETURN void BSP_PanicEnd(void)
{
#ifdef BSP_ASSERT_HALT
    /* Debug: 挂住等调试器. 此时输出已轮询直写完毕, 不会丢. */
    for (;;) { }
#else
    /* Release: 现场设备死循环 = 变砖, 必须自恢复.
     * 轮询写完才走到这里, 复位不会带走未发出的日志. */
    NVIC_SystemReset();

    for (;;) { }        /* 不可达, 仅为满足 noreturn */
#endif
}
