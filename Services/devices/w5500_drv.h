/**
 * @file    w5500_drv.h
 * @brief   W5500 设备服务 —— 芯片上电、通路校验、PHY 配置
 * @note    分层位置:
 *
 *              Application/Tasks/task_net.c   ← 业务: 什么时候连、发什么
 *                    ↓
 *              Services/devices/w5500_net.c   ← 网络参数 + socket 超时封装
 *                    ↓
 *              Services/devices/w5500_drv.c   ← 本模块: 把芯片带到可用状态
 *                    ↓ 注册回调
 *              Middleware/ioLibrary           ← 协议栈 (不认识硬件)
 *                    ↓
 *              BSP/bsp_w5500_hw.c             ← 板级: CS/RESET/INT 引脚
 *
 *          本模块只负责 ioLibrary 不管的那部分: 板级初始化、复位时序、PHY
 *          工作模式配置。socket 封装在 w5500_net.c。
 */

#ifndef __W5500_DRV_H__
#define __W5500_DRV_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 常量
 * ================================================================ */

/** @brief VERSIONR 的期望值 —— W5500 固定返回 0x04 */
enum { W5500_VERSION_EXPECTED = 0x04 };

/**
 * @brief PHYCFGR 的 RST 位 (bit7)
 * @note  ioLibrary 只给了 AND 掩码 PHYCFGR_RST = ~(1<<7) (用于"复位 PHY"),
 *        没给置位用的值, 所以这里补一个。实测 0xFA / 0xFF 都印证了 bit7=1
 *        是"PHY 正常工作"。
 */
enum { W5500_PHYCFGR_RST_SET = 0x80 };

/** @brief PHYCFGR 的只读状态位 (与 ioLibrary 的 PHYCFGR_xxx 同值, 短名字) */
enum {
    W5500_PHY_LNK = 0x01,   /* 链路已建立 */
    W5500_PHY_SPD = 0x02,   /* 1 = 100Mbps */
    W5500_PHY_DPX = 0x04,   /* 1 = 全双工  */
};

/* ================================================================
 * 状态码
 * ================================================================ */

/**
 * @brief 初始化状态
 * @note  区分 "SPI 完全没响应" 和 "有响应但版本不对" —— 前者查接线/供电/片选,
 *        后者查芯片型号, 排查方向完全不同。
 */
typedef enum {
    W5500_OK = 0,
    W5500_ERR_SPI,          /* MISO 恒定 (读回全 0x00 或全 0xFF) —— 总线无响应 */
    W5500_ERR_NOT_FOUND,    /* 有响应但 VERSIONR != 0x04 */
    W5500_ERR_MEMCFG,       /* socket 缓冲分配被 ioLibrary 拒绝 */
} W5500_Status_t;

/* ================================================================
 * API
 * ================================================================ */

/**
 * @brief  把 ioLibrary 接到本工程的 SPI 总线上 (注册三组回调)
 * @note   一般由 W5500_Drv_Init() 调用, 不需要单独调 —— 除非你要重新注册。
 *         必须在【任何】WIZCHIP_READ/WRITE 之前完成。
 */
void W5500_Port_Register(void);

/**
 * @brief  初始化 W5500 到"可以开 socket"的状态
 * @return W5500_OK / 错误码
 * @note   完整顺序 (顺序有讲究, 见下):
 *           ① 板级引脚 + SPI2
 *           ② 注册 ioLibrary 回调
 *           ③ 硬件复位 (RESET 引脚)
 *           ④ 读 VERSIONR 校验通路
 *           ⑤ wizchip_init —— 分配 socket 收发缓冲
 *           ⑥ PHY 工作模式配置
 *           ⑦ 打一行结果日志 (VERSIONR / PHYCFGR)
 *
 *         ⚠️ ⑤ 必须在 ⑥ 之前: wizchip_init() 内部会做一次软件复位,
 *            而软件复位会把 PHYCFGR 打回默认值 (OPMD=0, 由硬件引脚决定模式)。
 *            顺序反了的话 PHY 配置会被悄悄抹掉, 现象是"链路一直起不来",
 *            而看代码完全合理。
 *
 *         成功与失败都由本函数自己打进日志 (LOG_TAG "W5500"), 调用方只需
 *         看返回值决定要不要继续往下走 —— 不用为了打日志再读一遍寄存器。
 *
 *         内部含约 100ms 阻塞 (复位后等 PLL 锁定), 由 vNetTask 在任务上下文
 *         调用 —— 起不来它会 1Hz 重试 (见 task_net.c)。
 */
W5500_Status_t W5500_Drv_Init(void);

/**
 * @brief  读芯片版本寄存器, W5500 应返回 0x04
 * @note   Init 内部已经读过并打进日志了; 本函数供"事后重新探一次芯片还在不在"
 *         (排查热插拔/掉电) 用。
 */
uint8_t W5500_Drv_ReadVersion(void);

/**
 * @brief  读 PHY 配置/状态寄存器 (原始值)
 * @note   位含义见 W5500_PHY_xxx 与 ioLibrary 的 PHYCFGR_OPMD/OPMDC_xxx。
 *         实测参考值: 0xFA = 配置就绪但链路未建立; 0xFF = 100M 全双工已连通。
 *
 *         注意自协商需要 1~2 秒 —— 复位后立刻读多半还是 0xFA, 要过一会儿
 *         才变 0xFF。所以链路状态适合轮询观察, 不适合在 Init 里等。
 */
uint8_t W5500_Drv_ReadPhyCfg(void);

/** @brief 状态码 → 可读字符串 (Init 用它拼失败日志; 调用方也可用于自己的错误路径) */
const char *W5500_Drv_StatusStr(W5500_Status_t st);

#ifdef __cplusplus
}
#endif

#endif /* __W5500_DRV_H__ */
