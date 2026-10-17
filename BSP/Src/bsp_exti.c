/**
 * @file    bsp_exti.c
 * @brief   共享 EXTI 向量的分发 —— 见 bsp_exti.h 的规则说明
 * @note    这里【只做分发】, 不碰任何引脚寄存器: 认领与清除各自的 pending
 *          位是各驱动自己的事 (它们才知道自己关心哪条线)。
 */

#include "bsp_exti.h"
#include "bsp_xpt2046.h"
#include "bsp_w5500_hw.h"

/* ================================================================
 * EXTI9_5 —— 覆盖 EXTI 线 5~9
 * ================================================================ */

void EXTI9_5_IRQHandler(void)
{
    BSP_XPT2046_ExtiHandler();      /* PF9: 触摸 */
    BSP_W5500_HW_ExtiHandler();     /* PG8: W5500 INT */
}
