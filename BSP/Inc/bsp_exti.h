/**
 * @file    bsp_exti.h
 * @brief   共享 EXTI 向量的分发入口
 * @note    NVIC 里 EXTI 不是"一条线一个向量": EXTI5~9 挤在 EXTI9_5_IRQn,
 *          EXTI10~15 挤在 EXTI15_10_IRQn。一个向量只能有一个处理函数, 所以
 *          当同一段里挂了两个模块时, 向量不能归其中任何一个 —— 由本模块
 *          统一收下, 再按引脚分发给各自的驱动。
 *
 *          本工程目前落在共享向量里的:
 *              PF9  触摸 XPT2046  (bsp_xpt2046.c)
 *              PG8  W5500 INT     (bsp_w5500_hw.c)
 *
 *          ⚠️ 新增这两个向量上的引脚时, 在 bsp_exti.c 里加一行分发即可,
 *             不要各自去定义 EXTI9_5_IRQHandler / EXTI15_10_IRQHandler ——
 *             重复定义会撞符号, 链接不过。
 */

#ifndef __BSP_EXTI_H__
#define __BSP_EXTI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 向量本体在 bsp_exti.c, 头文件这里没有对外 API —— 它只在中断向量表里
 * 被 startup 代码引用。本文件存在的意义是把上面对应的规则写下来。 */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_EXTI_H__ */
