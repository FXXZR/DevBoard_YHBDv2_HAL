/**
 * @file    test_ap3216c.c
 * @brief   AP3216C 环境光/接近/红外三合一传感器连续读取测试
 * @note    测试流程: 初始化 → 死循环每 500ms 读取一次
 *          并 printf ALS(lux) / PS / IR。
 *
 *          I2C: 默认软件 I2C, PB6=SCL, PB7=SDA, 400KHz。
 *          初版纯轮询模式，不使用 INT 引脚 (PF8)。
 *
 *          验证: 环境光随光照变化；手靠近传感器时 PS 值增大、approaching=1。
 */

#include "test.h"

void Test_AP3216C_Read(void)
{
    BSP_AP3216C_Data_t data;

    /* ---- 初始化传感器 (ALS 默认量程 20661 lux, PS 默认增益) ---- */
    BSP_AP3216C_Status_t init_ret = BSP_AP3216C_Init(BSP_AP3216C_ALS_RANGE_20661);

#if defined(BSP_AP3216C_USE_HW_I2C)
    printf("\r\n[AP3216C Test] HW I2C, PB6=SCL, PB7=SDA, 400KHz\r\n");
#else
    printf("\r\n[AP3216C Test] Soft I2C, PB6=SCL, PB7=SDA, 400KHz\r\n");
#endif

    /* ---- 初始化结果 ---- */
    if (init_ret != BSP_AP3216C_OK) {
        printf("  FAIL: Init error (status=%d)\r\n", init_ret);
        printf("  Check: I2C wiring (PB6=SCL, PB7=SDA).\r\n");
        return;
    }

    printf("  AP3216C initialized OK.\r\n");
    printf("  Range: 20661 lux (default) | PS: default gain\r\n");
    printf("  Reading at 2Hz (500ms interval)...\r\n\r\n");

    /* ---- 连续读取循环 ---- */
    while (1) {
        if (BSP_AP3216C_ReadAll(&data) == BSP_AP3216C_OK) {
            printf("  ALS: %7.2f lux | PS: %4u | IR: %4u",
                   data.als_lux, data.ps, data.ir);

            /* 方向 */
            if (data.approaching) {
                printf(" [Approaching]");
            } else {
                printf(" [Moving away]");
            }

            /* IR 饱和警告 */
            if (data.ir_saturated) {
                printf(" !!IR SAT!!");
            }

            printf("\r\n");
        } else {
            printf("  Read error!\r\n");
        }

        BSP_SYSTICK_DelayMS(500);
    }
}
