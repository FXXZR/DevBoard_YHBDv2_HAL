/**
 * @file    test_mpu6050.c
 * @brief   MPU6050 6轴运动传感器连续读取测试
 * @note    测试流程: 初始化 → WHO_AM_I 校验 → 死循环每 500ms 读取一次
 *          并 printf 转换后的物理量 (加速度 g / 角速度 °/s / 温度 °C)。
 *
 *          I2C: 默认软件 I2C, PB6=SCL, PB7=SDA, 400KHz。
 *
 *          验证: 静止状态下加速度应接近 (0, 0, ~1.0) g,
 *               角速度应接近 (0, 0, 0) °/s。
 */

#include "test.h"

void Test_MPU6050_Read(void)
{
    BSP_MPU6050_Data_t data;

    /* ---- 初始化传感器 ---- */
    BSP_MPU6050_Status_t init_ret = BSP_MPU6050_Init();

#if defined(BSP_MPU6050_USE_HW_I2C)
    printf("\r\n[MPU6050 Test] HW I2C, PB6=SCL, PB7=SDA, 400KHz\r\n");
#else
    printf("\r\n[MPU6050 Test] Soft I2C, PB6=SCL, PB7=SDA, 400KHz\r\n");
#endif

    /* ---- 初始化结果 ---- */
    if (init_ret == BSP_MPU6050_ERR_ID_MISMATCH) {
        printf("  FAIL: WHO_AM_I mismatch! (expected 0x68)\r\n");
        printf("  Check: I2C wiring (PB6=SCL, PB7=SDA), AD0=GND.\r\n");
        return;
    }
    if (init_ret != BSP_MPU6050_OK) {
        printf("  FAIL: Init error (status=%d)\r\n", init_ret);
        printf("  Check: I2C wiring (PB6=SCL, PB7=SDA).\r\n");
        return;
    }

    /* ---- 设备就绪确认 ---- */
    if (BSP_MPU6050_IsReady() != BSP_MPU6050_OK) {
        printf("  FAIL: MPU6050 not found at addr 0x%02X\r\n",
               BSP_MPU6050_DEV_ADDR);
        return;
    }

    printf("  MPU6050 detected OK.\r\n");
    printf("  Range: Accel ±4g | Gyro ±2000°/s | DLPF 5Hz\r\n");
    printf("  Reading at 2Hz (500ms interval)...\r\n\r\n");

    /* ---- 连续读取循环 ---- */
    while (1) {
        if (BSP_MPU6050_ReadAll(&data) == BSP_MPU6050_OK) {
            float ax = BSP_MPU6050_Accel_ToG(data.accel_x);
            float ay = BSP_MPU6050_Accel_ToG(data.accel_y);
            float az = BSP_MPU6050_Accel_ToG(data.accel_z);
            float gx = BSP_MPU6050_Gyro_ToDegPerSec(data.gyro_x);
            float gy = BSP_MPU6050_Gyro_ToDegPerSec(data.gyro_y);
            float gz = BSP_MPU6050_Gyro_ToDegPerSec(data.gyro_z);

            printf("  Accel(g): X=%+7.3f Y=%+7.3f Z=%+7.3f | "
                   "Gyro(°/s): X=%+8.2f Y=%+8.2f Z=%+8.2f | "
                   "Temp: %.2f°C\r\n",
                   ax, ay, az, gx, gy, gz, data.temp_c);
        } else {
            printf("  Read error!\r\n");
        }

        BSP_SYSTICK_DelayMS(500);
    }
}
