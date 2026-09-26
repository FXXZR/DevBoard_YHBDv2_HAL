/**
 * @file    bsp_w5500_hw.h
 * @brief   W5500 板级硬件资源 —— 引脚与控制线
 * @note    本模块【只】管板子上的物理资源: CS / RESET / INT 三根控制线,
 *          以及 SPI2 总线本身的初始化。
 *
 *          它不管 W5500 的寄存器怎么访问 —— 那是 Services/devices 里
 *          w5500_port.c 的事 (通过注册回调告诉 ioLibrary 怎么说话)。
 *          这个切分让本文件只描述"板子上有什么", 不掺杂芯片协议。
 *
 *          硬件接线 (野火霸道 V2 / STM32F103ZET6):
 *              SPI2   : PB13=SCK, PB14=MISO, PB15=MOSI (模式 3)
 *              CS     : PG9   软件控制
 *              RESET  : PG15
 *              INT    : PG8   低有效
 *
 *          来源: 野火官方 W5500 例程 w5500_conf.h 的 STM32F103ZET6 分支。
 *          旁证: 本工程 GPIOG 上 PG0-PG5(FSMC地址) / PG6(LCD背光) / PG11(LCD复位)
 *                / PG12(FSMC NE4) / PG13,PG14(ESP8266) 已被占用, PG8/9/15 恰为空档。
 *          已由实机验证: VERSIONR 读出 0x04, PHY 链路建立 100M 全双工。
 */

#ifndef __BSP_W5500_HW_H__
#define __BSP_W5500_HW_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  配置 CS / RESET / INT 三根控制线并初始化 SPI2
 * @note   CS 与 RESET 初始为高 (未选中 / 不复位), 避免上电瞬间误动作。
 *         INT 配为输入上拉 —— W1 只把它准备好, EXTI 与收包在 W2 接。
 *         约 100ms 阻塞 (复位后等 PHY PLL 锁定), 只在启动时调一次。
 */
void BSP_W5500_HW_Init(void);

/** @brief 拉低片选 (选中) */
void BSP_W5500_HW_CsLow(void);

/** @brief 拉高片选 (取消选中) */
void BSP_W5500_HW_CsHigh(void);

/**
 * @brief  硬件复位时序
 * @note   RESET 拉低 1ms → 拉高 → 等 100ms 让内部 PLL 锁定。
 *
 *         野火参考例程用的是 2us + 1600ms: 2us 短于通常要求的 500us,
 *         1600ms 又太长 (开机白等 1.6 秒), 故取中间值。
 *         实测这组参数可正常读到 VERSIONR=0x04 并完成 PHY 自协商。
 *
 *         真正的验证是复位后的版本寄存器读回, 而不是延时本身。
 */
void BSP_W5500_HW_Reset(void);

/**
 * @brief  读 INT 引脚电平
 * @return 0 = 低 (W5500 有事件待处理); 1 = 高 (空闲)
 * @note   W1 只提供读取, 中断与收包在 W2。
 */
uint8_t BSP_W5500_HW_IntLevel(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_W5500_HW_H__ */
