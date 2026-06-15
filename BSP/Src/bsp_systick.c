/**
 * @file    bsp_systick.c
 * @brief   BSP SysTick 延时与系统时钟实现
 * @note    系统时钟: HSE 8MHz → PLL ×9 → SYSCLK 72MHz
 *          us 延时:  SysTick->VAL 轮询（利用 HAL 已配置的 1ms 自动重载计数器）
 *          ms 延时:  HAL_GetTick() 轮询
 *
 *          SysTick 在 HAL_Init() 中已被配置为:
 *            时钟源 = SystemCoreClock (72MHz)
 *            LOAD   = SystemCoreClock/1000 - 1 = 71999
 *            模式   = 自动重载 + 中断使能
 *            每 1ms 触发一次 SysTick_Handler → HAL_IncTick()
 *
 *          us 延时原理：
 *            读两次 SysTick->VAL（递减计数器），差值即为经过的 CPU 周期数。
 *            自动处理计数器回绕（每 1ms 一次），支持任意长度延时。
 *            不修改 SysTick 配置，不影响 HAL 时基。
 *
 *          与原 STD 方案对比：
 *            旧: SysTick->LOAD=0xFFFFFF, 不用中断, 纯轮询
 *            新: 复用 HAL 的 SysTick 配置, 不关中断, 与 HAL/RTOS 共存
 */

#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

/* 由 main.c 实现的致命错误处理（关中断 → 死循环） */
extern void Error_Handler(void);

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief  初始化系统时钟 + 使 SysTick 延时就绪
 *
 * @note   系统时钟配置（原 SystemClock_Config）已内聚于此。
 *         HAL_Init() 在调用本函数之前已将 SysTick 配置为 1ms 中断模式，
 *         本函数仅需确保系统时钟切换到 HSE+PLL 72MHz。
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

    /* HAL_RCC_ClockConfig 内部已调用 SystemCoreClockUpdate(),
     * 此后 SystemCoreClock = 72000000。
     * SysTick 仍在 HAL_Init() 配置的 1ms 中断模式下运行，
     * 唯重装值需随新频率更新 —— HAL_RCC_ClockConfig 已自动处理。
     */
}

/* ----------------------------------------------------------------
 * us 延时 — SysTick->VAL 轮询
 *
 * SysTick 为 24 位递减计数器，每个 SystemCoreClock 周期 -1。
 * HAL 配置: LOAD = SystemCoreClock/1000 - 1，自动重载，约 1ms 回绕一次。
 *
 * 实现要点:
 *   - 通过 (last - now) 处理递减差值
 *   - 通过 (now > last) 检测计数器回绕
 *   - 回绕时补上: reload - now + last + 1
 *   - 支持多轮回绕（长延时），通过累积 elapsed 实现
 * ---------------------------------------------------------------- */

void BSP_SYSTICK_DelayUS(uint32_t us)
{
    uint32_t reload  = SysTick->LOAD;                     /* HAL 1ms 重装值    */
    uint32_t ticks   = us * (SystemCoreClock / 1000000U); /* 目标 CPU 周期数   */
    uint32_t last    = SysTick->VAL;                      /* 起始计数值        */
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
 * ms 延时 — HAL_GetTick 轮询
 *
 * HAL_GetTick() 返回自启动以来的毫秒数（由 SysTick 中断驱动）。
 * uwTick 为 32-bit 无符号数，差值减法自动处理溢出回绕。
 * ---------------------------------------------------------------- */

void BSP_SYSTICK_DelayMS(uint32_t ms)
{
    uint32_t tickstart = HAL_GetTick();

    while ((HAL_GetTick() - tickstart) < ms) {
        /* spin */
    }
}
