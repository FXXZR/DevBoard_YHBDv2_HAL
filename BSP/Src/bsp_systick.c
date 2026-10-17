/**
 * @file    bsp_systick.c
 * @brief   BSP SysTick 延时与系统时钟实现
 * @note    系统时钟: HSE 8MHz → PLL ×9 → SYSCLK 72MHz
 *
 *          SysTick 在 BSP_SYSTICK_DelayInit() 中手动配置寄存器:
 *            LOAD = SystemCoreClock/1000 - 1 = 71999 (1ms)
 *            计数器启用, 中断关闭
 *            (HAL_SYSTICK_Config / SysTick_Config 会强制开中断, 故不用)
 *
 *          us 延时:  SysTick->VAL 轮询 (不依赖中断)
 *          ms 延时:  BSP_SYSTICK_DelayUS 循环包装 (不依赖 HAL_GetTick)
 *
 *          裸机: 取消 BSP_SYSTICK_DelayInit() 中 TICKINT 注释
 *          RTOS: FreeRTOS 通过 xPortStartScheduler 接管中断
 */

#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

/* 由 main.c 实现的致命错误处理（关中断 → 死循环） */
extern void Error_Handler(void);

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief  初始化系统时钟 + 配置 SysTick (计数器启用, 中断关闭)
 *
 * @note   系统时钟切换至 HSE+PLL 72MHz, SysTick 配置为 1ms 时基.
 *         HAL_Init / HAL_RCC_ClockConfig 内部不再调用 HAL_InitTick,
 *         此处直接写 SysTick 寄存器, 不经过 SysTick_Config (它强制开中断).
 *
 *         裸机使用时取消注释 SysTick->CTRL |= TICKINT 行.
 */
void BSP_SYSTICK_DelayInit(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* ---- HSE + PLL 振荡器配置 ---- */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* ---- CPU / AHB / APB 总线分频 ---- */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK
                                     | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1
                                     | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }

    /* ---- SysTick 手动配置 (1ms 时基, 计数器启动, 中断关闭) ----
     * 不用 HAL_SYSTICK_Config → SysTick_Config, 因为它强制开 TICKINT.
     * FreeRTOS 的 xPortStartScheduler 会在调度器启动时接管中断.
     * 裸机: 取消注释下一行启用中断 → HAL_IncTick / HAL_Delay 恢复.
     */
    SysTick->LOAD = SystemCoreClock / 1000U - 1U;          /* 72MHz → 71999 */
    SysTick->VAL  = 0U;
    // SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;           /* 裸机取消注释可激活HAL_Gettick */
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk             /* CPU 时钟  */
                   | SysTick_CTRL_ENABLE_Msk;              /* 启动计数器 */
}

/* ----------------------------------------------------------------
 * us 延时 — SysTick->VAL 轮询
 *
 * SysTick 为 24 位递减计数器，每个 SystemCoreClock 周期 -1。
 * LOAD = 71999, 约 1ms 回绕一次。
 *
 * 实现要点:
 *   - 通过 (last - now) 处理递减差值
 *   - 通过 (now > last) 检测计数器回绕
 *   - 回绕时补上: reload - now + last + 1
 *   - 支持多轮回绕（长延时），通过累积 elapsed 实现
 * ---------------------------------------------------------------- */

void BSP_SYSTICK_DelayUS(uint32_t us)
{
    uint32_t reload  = SysTick->LOAD;                     /* 1ms 重装值      */
    uint32_t ticks   = us * (SystemCoreClock / 1000000U); /* 目标 CPU 周期数 */
    uint32_t last    = SysTick->VAL;                      /* 起始计数值      */
    uint32_t elapsed = 0;

    while (elapsed < ticks) {
        uint32_t now = SysTick->VAL;

        if (last >= now) {
            /* 正常递减，无回绕 */
            elapsed += last - now;
        } else {
            /* 计数器回绕: last→0→reload→now */
            elapsed += reload - now + last + 1;
        }

        last = now;
    }
}

/* ----------------------------------------------------------------
 * ms 延时 — BSP_SYSTICK_DelayUS 循环包装
 *
 * 不依赖 HAL_GetTick / HAL_IncTick → 不依赖 SysTick 中断.
 * 每次调用 DelayUS(1000) 误差 < 5μs, 循环 ms 次累积可忽略.
 * ---------------------------------------------------------------- */

void BSP_SYSTICK_DelayMS(uint32_t ms)
{
    while (ms--) {
        BSP_SYSTICK_DelayUS(1000);
    }
}

/* ----------------------------------------------------------------
 * 微秒时间戳 — HAL_GetTick() 整毫秒 + SysTick->VAL 余数
 *
 * 与 DelayUS 的区别: 这个【读一次就走】, 用来量耗时 (两次调用相减),
 * 不是用来等。
 *
 * 为什么不能用 HAL_GetTick 直接量: 1ms 分辨率量不出亚毫秒的 SPI 事务
 * (2KB @18MHz 只有 0.9ms, 量化误差比被测对象还大)。
 *
 * 竞态: 读完 tick 再读 VAL 之间, SysTick 可能正好进位 —— 那会算出
 * "毫秒部分已进位、余数还是上一毫秒" 的错值 (差 1ms)。所以读完 VAL
 * 再读一次 tick, 两次相同才认。
 *
 * 回绕: 返回值约 71.6 分钟回绕一次。两次读数【相减】仍然正确
 * (无符号减法), 前提是间隔远小于 71 分钟。
 * ---------------------------------------------------------------- */

uint32_t BSP_SYSTICK_GetUs(void)
{
    uint32_t ms_before, ms_after, val;
    uint32_t ticks_per_us = SystemCoreClock / 1000000U;

    do {
        ms_before = HAL_GetTick();
        val       = SysTick->VAL;           /* 递减计数器: 越大越早 */
        ms_after  = HAL_GetTick();
    } while (ms_before != ms_after);

    /* 计数从 LOAD 递减到 0, 已走过的周期数 = LOAD - VAL */
    return ms_before * 1000U + (SysTick->LOAD - val) / ticks_per_us;
}
