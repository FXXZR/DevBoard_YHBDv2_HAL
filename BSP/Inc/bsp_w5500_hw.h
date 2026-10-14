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
 *              INT    : PG8   低有效 (下降沿 = 芯片有事要报)
 *
 *          ⚠️ PG8 的 EXTI 线是 8 → 向量 EXTI9_5_IRQn, 与触摸屏的 PF9 共用。
 *             一个向量只能有一个处理函数, 所以向量在 bsp_exti.c, 本模块只
 *             提供 BSP_W5500_HW_ExtiHandler() 供它分发。
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
 * 类型
 * ================================================================ */

/**
 * @brief INT 引脚回调 —— 在 ISR 上下文中执行
 * @note  ISR 里【只该做"叫醒谁"这一件事】: 读 IR / 取数据都要走 SPI, 中断
 *        上下文里耗不起 (bsp_spi 里还有 __get_IPSR() 断言挡着)。所以本层
 *        只把"INT 来了"这个事实转出去, 干什么由上层决定 —— 与 bsp_usart
 *        的 rx_idle 回调是同一个套路 (BSP 不许反向依赖上层)。
 *
 *        回调里只能调 ...FromISR 系列 API, 且必须快速返回。
 */
typedef void (*BSP_W5500_IntCallback_t)(void);

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  配置 CS / RESET / INT 三根控制线并初始化 SPI2
 * @note   CS 与 RESET 初始为高 (未选中 / 不复位), 避免上电瞬间误动作。
 *         INT 配为输入上拉 —— 中断本身由 BSP_W5500_HW_EnableInt() 打开。
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
 * @note   ★ 这一位在中断收包路径里很关键: 芯片的 IR 只能被"读到它的那一方"
 *         清掉, 而清标志与引脚回到高之间有窗口, 那期间新到的数据只把线拉低、
 *         **不产生新的下降沿** —— 只靠中断就会漏掉这一拍。所以处理循环的
 *         收尾要重查这个电平, 见 task_net.c 的 net_socket_service()。
 */
uint8_t BSP_W5500_HW_IntLevel(void);

/**
 * @brief  使能 INT 中断 (PG8 / EXTI8 下降沿) 并注册回调
 * @param  cb  ISR 里被调用的函数, 不能为 NULL
 * @note   在 BSP_W5500_HW_Init() 之后调用 (复用它的 GPIO 时钟与上拉配置)。
 *         只开下降沿: W5500 的 INT 在有事件时【保持】低电平, 开双边沿或
 *         电平触发都会在标志没清干净时反复进中断。
 *
 *         优先级取 7, 与触摸/串口一致 —— 必须 ≥ FreeRTOS 的
 *         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5), 否则回调里的
 *         ...FromISR API 会踩坏内核临界区。
 */
void BSP_W5500_HW_EnableInt(BSP_W5500_IntCallback_t cb);

/**
 * @brief EXTI9_5 向量里的 W5500 分支
 * @note  由 bsp_exti.c 分发调用, 业务代码不要直接调。
 */
void BSP_W5500_HW_ExtiHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_W5500_HW_H__ */
