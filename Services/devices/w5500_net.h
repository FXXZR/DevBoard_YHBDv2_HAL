/**
 * @file    w5500_net.h
 * @brief   W5500 网络层 —— 网络参数 + socket 封装
 * @note    分层位置:
 *
 *              Application/Tasks/task_net.c   ← 业务: 什么时候连、发什么
 *                    ↓
 *              Services/devices/w5500_net.c   ← 本模块: 网络参数 + socket 超时封装
 *                    ↓
 *              Services/devices/w5500_drv.c   ← 把芯片带到"可以开 socket"
 *                    ↓ 注册回调
 *              Middleware/ioLibrary           ← 协议栈 (不认识硬件)
 *                    ↓
 *              BSP/bsp_w5500_hw.c             ← 板级: CS/RESET/INT 引脚
 *
 *          与 w5500_drv 的分工: drv 管"芯片能不能用" (上电/复位/PHY/版本),
 *          本模块管"这张网怎么用" (地址参数、连接、收发超时)。
 *
 *          ⚠️ socket 层不可重入, 本模块的 API 只在网络任务里调用 —— 见
 *             [task_net.c](../../Application/Tasks/task_net.c)。
 */

#ifndef __W5500_NET_H__
#define __W5500_NET_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  写入静态网络参数 (默认路径 —— 板子直连 PC 网口的固定地址)
 * @note   参数表在 .c 内部, 换环境改那张表。
 *
 *         写完会读回校验并打日志: 不一致说明 SPI 帧出错、寄存器写入被静默
 *         丢弃。W5500 不会为此报任何错, 现象只是"ping 不通" —— 所以这一步
 *         是必要的, 不是装饰。
 *
 *         须在 W5500_Drv_Init() 之后调用 (它内部含一次软复位, 会清掉参数)。
 */
void W5500_Net_ConfigStatic(void);

#ifdef __cplusplus
}
#endif

#endif /* __W5500_NET_H__ */
