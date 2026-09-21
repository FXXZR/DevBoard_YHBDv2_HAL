/**
 * @file    task_w5500.c
 * @brief   W5500 链路监控任务 — 轮询 PHYCFGR, 值变化时打日志
 *
 * 为什么需要这个任务:
 *   启动自检在复位后 502ms 读 PHYCFGR, 实测那时链路还没建立 ——
 *   以太网自协商要 1~2 秒才完成。本任务每秒读一次, 值一变就打出来,
 *   插拔网线也能立刻看到。
 *
 *   顺带它也是 W2 链路监控的基础 —— 届时 "网线掉了" 要能触发上层处理。
 *
 * 关于总线互斥 —— 本任务【不需要】自己加锁:
 *   SPI2 总线锁由 Services/devices/w5500_port.c 的片选回调负责, 粒度是
 *   一次完整的寄存器事务 (select…deselect)。所以任务层直接调
 *   W5500_Drv_* 即可, 加锁是驱动内部的事。
 *
 *   ⚠️ 反过来说: 千万不要在这里再 SpiBus_Lock 一次 —— FreeRTOS 的普通
 *      互斥量不可递归, 同一个任务重复获取会【立即死锁】。
 *      这也正是把锁下沉到 port 层的好处: 调用方根本不需要知道有锁。
 */

#define LOG_TAG "W5500"
#include "log.h"

#include "task_w5500.h"
#include "w5500_drv.h"
#include "FreeRTOS.h"
#include "task.h"

/* 轮询周期 —— 1Hz 足够观察自协商过程.
 * 开销: 每周期一帧 4 字节 SPI 事务 (18MHz 下约 2us) + 一次任务唤醒. */
#define W5500_POLL_PERIOD_MS    1000

void vW5500Task(void *pvParameters)
{
    (void) pvParameters;

    uint8_t last  = 0;
    uint8_t first = 1;

    for (;;) {
        uint8_t phy = W5500_Drv_ReadPhyCfg();

        if (first) {
            /* 打一次初始值作为对照基线 */
            LOG_I("PHYCFGR 初始 = 0x%02X", phy);
            last  = phy;
            first = 0;
        } else if (phy != last) {
            LOG_I("PHYCFGR 变化: 0x%02X -> 0x%02X", last, phy);
            last = phy;
        }

        vTaskDelay(pdMS_TO_TICKS(W5500_POLL_PERIOD_MS));
    }
}
