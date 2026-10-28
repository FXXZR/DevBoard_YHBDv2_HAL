/**
 * @file    bsp_w5500_hw.c
 * @brief   W5500 板级硬件资源实现 —— CS / RESET / INT + SPI2
 * @note    只碰 GPIO 和 bsp_spi, 不认识 W5500 的任何寄存器。
 */

#include "bsp_w5500_hw.h"
#include "bsp_spi.h"
#include "bsp_systick.h"
#include "stm32f1xx_hal.h"

#include <stddef.h>     /* NULL */

/* ================================================================
 * 板级资源
 * ================================================================ */

#define W5500_CS_PORT       GPIOG
#define W5500_CS_PIN        GPIO_PIN_9
#define W5500_RST_PORT      GPIOG
#define W5500_RST_PIN       GPIO_PIN_15
#define W5500_INT_PORT      GPIOG
#define W5500_INT_PIN       GPIO_PIN_8

/* 复位时序 —— 见 bsp_w5500_hw.h 的说明 */
static const uint32_t w5500_reset_low_ms    = 1U;
static const uint32_t w5500_reset_settle_ms = 100U;

/* 一条总线 —— SPI2 (PB13/14/15, 模式 3, 见 bsp_spi.c 的硬件映射表) */
#define W5500_SPI_BUS           BSP_SPI_BUS_2

/* INT 中断: 线 8 落在 EXTI9_5 向量里 (与触摸的 PF9 共用, 分发在 bsp_exti.c)。
 * 优先级 7 —— 与触摸/串口同级, 且满足 FreeRTOS 的 ISR 调用门槛 (≥5)。 */
#define W5500_INT_IRQ           EXTI9_5_IRQn
static const uint32_t w5500_int_irq_prio = 7U;

/* ================================================================
 * 状态
 * ================================================================ */

/* 上层注册的 ISR 回调。只在 BSP_W5500_HW_EnableInt() 里被写过一次, 之后
 * ISR 只读 —— 所以不需要临界区。 */
static BSP_W5500_IntCallback_t s_w5500_int_cb;

/* ================================================================
 * 公开 API
 * ================================================================ */

void BSP_W5500_HW_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* ---- GPIO: CS / RESET / INT 都在 GPIOG ----
     * 注: bsp_spi 只使能 SCK/MISO/MOSI 所在端口的时钟 (这里是 GPIOB),
     *     CS/RST/INT 所在的 GPIOG 必须由本模块自己开。 */
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* CS 与 RESET: 推挽输出, 初值都是"无效" —— 避免上电瞬间被误选中
     * 或产生一次无谓的复位。 */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin  = W5500_CS_PIN | W5500_RST_PIN;
    HAL_GPIO_Init(W5500_CS_PORT, &gpio);
    BSP_W5500_HW_CsHigh();
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_SET);

    /* INT: 输入上拉 (W5500 的 INT 是低有效输出, 空闲为高) */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = W5500_INT_PIN;
    HAL_GPIO_Init(W5500_INT_PORT, &gpio);

    /* ---- SPI2 ---- */
    /* APB1 = 36MHz, DIV_2 = 18MHz。
     * 注意和 SPI1 上 Flash 的 DIV_4 不是同一个枚举值, 却同样是 18MHz ——
     * 见 bsp_spi.h 里那段分频说明。 */
    BSP_SPI_Init(W5500_SPI_BUS, BSP_SPI_BAUD_DIV_2);
}

void BSP_W5500_HW_CsLow(void)
{
    /* 直写 BSRR/BRR 而非 HAL_GPIO_WritePin: CS 每帧两次, 且在 SCK 空闲沿
     * 插入, 走 HAL 的"查引脚号→算位→assert"链路没必要。 */
    W5500_CS_PORT->BRR = W5500_CS_PIN;
}

void BSP_W5500_HW_CsHigh(void)
{
    W5500_CS_PORT->BSRR = W5500_CS_PIN;
}

void BSP_W5500_HW_Reset(void)
{
    W5500_RST_PORT->BRR  = W5500_RST_PIN;
    BSP_SYSTICK_DelayMS(w5500_reset_low_ms);
    W5500_RST_PORT->BSRR = W5500_RST_PIN;
    BSP_SYSTICK_DelayMS(w5500_reset_settle_ms);
}

uint8_t BSP_W5500_HW_IntLevel(void)
{
    return (HAL_GPIO_ReadPin(W5500_INT_PORT, W5500_INT_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

void BSP_W5500_HW_EnableInt(BSP_W5500_IntCallback_t cb)
{
    GPIO_InitTypeDef gpio = {0};

    if (cb == NULL) {
        return;                     /* 没有回调就没有唤醒对象, 开了也只是空转 */
    }
    s_w5500_int_cb = cb;

    /* 把 PG8 从"普通输入"改成"下降沿触发中断"。上拉保持不变 —— INT 是
     * 低有效输出, 空闲时靠上拉维持高电平。 */
    gpio.Pin  = W5500_INT_PIN;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(W5500_INT_PORT, &gpio);

    HAL_NVIC_SetPriority(W5500_INT_IRQ, w5500_int_irq_prio, 0);
    HAL_NVIC_EnableIRQ(W5500_INT_IRQ);
}

void BSP_W5500_HW_ExtiHandler(void)
{
    /* 同一个向量里还挂着 PF9 (触摸), 先认领自己这一条线 */
    if (__HAL_GPIO_EXTI_GET_IT(W5500_INT_PIN) == 0) {
        return;
    }
    __HAL_GPIO_EXTI_CLEAR_IT(W5500_INT_PIN);

    /* ⚠️ 这里【不】去清芯片的 IR 标志: 那要发 SPI 帧, 中断上下文里不行
     *    (bsp_spi 有 __get_IPSR() 断言)。所以 INT 线会在处理完之前一直保持
     *    低 —— 这正是下面这个回调必须"只叫醒、不处理"的原因。 */
    s_w5500_int_cb();
}
