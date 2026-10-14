/**
 * @file    spi_bus.c
 * @brief   SPI 总线服务实现 —— 每总线一把互斥量
 *
 * 依赖说明: 本文件直接使用 FreeRTOS, 不做 OSAL 包装。
 *   约定是 RTOS 依赖只出现在 .c 里 —— spi_bus.h 不含任何 FreeRTOS 类型,
 *   所以上下两层都不会被"传染"。换 RTOS 时改动范围限于本文件, 而不是
 *   散落到所有调用点的头文件里。
 */

#include "spi_bus.h"
#include "bsp_assert.h"

/* RTOS 依赖仅限本文件 */
#include "FreeRTOS.h"
#include "semphr.h"

/* ================================================================
 * 内部状态
 * ================================================================ */

/* 每条总线一把锁。用互斥量 (而非二值信号量) 以获优先级继承,
 * 理由见 spi_bus.h 的 SpiBus_Unlock 说明。 */
static SemaphoreHandle_t s_spi_mutex[BSP_SPI_BUS_COUNT];

/* ================================================================
 * 公开 API
 * ================================================================ */

void SpiBus_Init(void)
{
    for (int i = 0; i < (int)BSP_SPI_BUS_COUNT; i++) {
        if (s_spi_mutex[i] != NULL) continue;   /* 幂等 */

        s_spi_mutex[i] = xSemaphoreCreateMutex();

        /* 创建失败只可能是 FreeRTOS 堆不够。这里必须当场炸 ——
         * 否则后续每次 SpiBus_Lock 都会在断言上失败, 现象离原因很远。 */
        BSP_ASSERT(s_spi_mutex[i] != NULL);
    }
}

int SpiBus_Lock(BSP_SPI_Bus_Id_t bus, uint32_t timeout_ms)
{
    TickType_t ticks;

    BSP_ASSERT(bus < BSP_SPI_BUS_COUNT);
    BSP_ASSERT(!xPortIsInsideInterrupt());      /* ISR 里不能阻塞等锁 */

    /* ★ 调度器还没启动 —— 直接放行, 不碰互斥量。
     *
     * 两个理由, 缺一不可:
     *   ① 此时只有一个执行上下文 (main), 不存在并发, 锁本来就是多余的;
     *   ② 更要命的是, 调度器启动前调带超时的 xSemaphoreTake 会触发
     *      FreeRTOS 自己的断言 (queue.c: "not (SUSPENDED && xTicksToWait != 0)"),
     *      而本工程的 configASSERT 接的是 panic 通道 —— 会直接停机/复位。
     *
     * 现状: 调度器启动前已经没有 SPI 使用者 (W5500 上电归 vNetTask),
     * 这一段留作兜底 —— 将来谁在 main() 里加一次 SPI 自检, 不至于撞上那个
     * 断言。 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return 0;

    BSP_ASSERT(s_spi_mutex[bus] != NULL);       /* 忘了调 SpiBus_Init */

    ticks = (timeout_ms == SPI_BUS_WAIT_FOREVER)
            ? portMAX_DELAY
            : pdMS_TO_TICKS(timeout_ms);

    return (xSemaphoreTake(s_spi_mutex[bus], ticks) == pdTRUE) ? 0 : -1;
}

void SpiBus_Unlock(BSP_SPI_Bus_Id_t bus)
{
    BSP_ASSERT(bus < BSP_SPI_BUS_COUNT);

    /* 与 SpiBus_Lock 的提前放行配对 —— 没拿过就不释放 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return;

    BSP_ASSERT(s_spi_mutex[bus] != NULL);

    /* 互斥量只能由持有者释放。give 失败 = 解锁位置写错了 —— 比如某个
     * 提前 return 的分支把锁漏放、或 A 任务释放了 B 任务持有的锁。
     * 这类错误靠人眼很难发现, 而且现象是"另一个任务莫名卡住",
     * 离原因非常远。断言能当场抓住。 */
    BSP_ASSERT(xSemaphoreGive(s_spi_mutex[bus]) == pdTRUE);
}
