/**
 * @file    w5500_drv.c
 * @brief   W5500 设备服务实现 —— 上电 / 通路校验 / PHY 配置
 * @note    寄存器访问一律走 ioLibrary 的 WIZCHIP_READ/WRITE ——
 *          它们最终会调用 w5500_port.c 注册进来的回调, 而回调里带着
 *          总线互斥锁和片选控制。本文件不直接碰 SPI。
 */

#define LOG_TAG "W5500"
#include "log.h"

#include "w5500_drv.h"

#include "bsp_w5500_hw.h"
#include "bsp_systick.h"
#include "bsp_assert.h"

#include "wizchip_conf.h"
#include "w5500.h"              /* VERSIONR / PHYCFGR / PHYCFGR_xxx 掩码 */

/* ================================================================
 * 常量
 * ================================================================ */

/* socket 收发缓冲分配 —— 8 个 socket 各 2KB, 正好用完 W5500 的
 * 16KB TX + 16KB RX。(如果将来要某个 socket 传大包, 从别的 socket 挪。) */
static const uint8_t socket_tx_size[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };
static const uint8_t socket_rx_size[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };

/* VERSIONR 的期望值 —— W5500 固定返回 0x04 */
static const uint8_t version_expected = 0x04U;

/* PHYCFGR 的 RST 位 (bit7)。
 * ioLibrary 只给了 AND 掩码 PHYCFGR_RST = ~(1<<7) (用于"复位 PHY"),
 * 没给置位用的值, 所以这里补一个。实测 0xFA / 0xFF 都印证了 bit7=1
 * 是"PHY 正常工作"。 */
static const uint8_t phy_cfgr_rst_set = 0x80U;

/* 复位后 PHY 配置寄存器稳定所需的时间 (经验值, 不是自协商完成时间) */
static const uint32_t phy_reg_settle_ms = 10U;

/* ================================================================
 * 内部
 * ================================================================ */

/**
 * @brief  把 PHY 设成"全能力自协商"并复位 PHY 使其生效
 *
 * 为什么必须做:
 *   芯片复位后 PHYCFGR 默认值是 0b10111XXX, 其中 OPMD(bit6) = 0 —— 意思是
 *   PHY 的工作模式由【硬件引脚 PMODE[2:0]】决定, 寄存器里的 OPMDC 写了也
 *   不生效。实测该默认状态下 LNK 恒为 0: 网线插好、RJ45 指示灯亮着,
 *   PHYCFGR 依然是 0xBA (默认值), 等多久都不变。
 *
 *   手册给的标准流程: 置 OPMD=1 并写好 OPMDC, 再把 RST 拉低一次复位 PHY,
 *   最后拉高释放 —— 新配置在 RST 的下降沿被锁存。
 *
 * 注: 本函数只负责"把模式设对", 不负责等自协商完成 —— 那要 1~2 秒,
 *     由上层轮询 PHYCFGR 观察 LNK 位。
 */
static void w5500_phy_config(void)
{
    uint8_t v;

    /* ① 置 OPMD=1 (改用寄存器配置) + OPMDC=111 (全能力/自协商), RST 保持 1 */
    v  = WIZCHIP_READ(PHYCFGR);
    v |= (uint8_t)(PHYCFGR_OPMD | PHYCFGR_OPMDC_ALLA | phy_cfgr_rst_set);
    WIZCHIP_WRITE(PHYCFGR, v);
    BSP_SYSTICK_DelayMS(1);

    /* ② RST=0: 拉低复位 PHY, 新配置在这一刻生效。
     *    ioLibrary 的 PHYCFGR_RST 是 AND 掩码 (~(1<<7)), 不是位值。 */
    v  = WIZCHIP_READ(PHYCFGR);
    v &= (uint8_t)PHYCFGR_RST;
    WIZCHIP_WRITE(PHYCFGR, v);
    BSP_SYSTICK_DelayMS(1);

    /* ③ RST=1: 释放, PHY 开始自协商 */
    v  = WIZCHIP_READ(PHYCFGR);
    v |= phy_cfgr_rst_set;
    WIZCHIP_WRITE(PHYCFGR, v);
    BSP_SYSTICK_DelayMS(phy_reg_settle_ms);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

W5500_Status_t W5500_Drv_Init(void)
{
    uint8_t ver;

    /* ① 板级: CS/RESET/INT 引脚 + SPI2 */
    BSP_W5500_HW_Init();

    /* ② 告诉 ioLibrary 怎么访问硬件 —— 必须早于任何 WIZCHIP_READ */
    W5500_Port_Register();

    /* ③ 硬件复位 */
    BSP_W5500_HW_Reset();

    /* ④ 读版本寄存器校验通路 —— 这一步本身就是通路测试, 不需要示波器 */
    ver = W5500_Drv_ReadVersion();
    if (ver != version_expected) {
        /* 全 0 / 全 1 说明 MISO 恒定, SPI 根本没有从机在应答,
         * 方向是查接线/供电/片选, 而不是查芯片型号。 */
        W5500_Status_t st = (ver == 0x00 || ver == 0xFF) ? W5500_ERR_SPI
                                                         : W5500_ERR_NOT_FOUND;
        LOG_E("init 失败: %s (VERSIONR=0x%02X)", W5500_Drv_StatusStr(st), ver);
        return st;
    }

    /* ⑤ 分配 socket 收发缓冲。⚠️ 内部含一次软件复位, 会把 PHYCFGR 打回
     *    默认值 —— 所以必须在 ⑥ 之前做, 顺序反了 PHY 配置会被悄悄抹掉。 */
    if (wizchip_init((uint8_t *)socket_tx_size, (uint8_t *)socket_rx_size) != 0) {
        LOG_E("init 失败: %s", W5500_Drv_StatusStr(W5500_ERR_MEMCFG));
        return W5500_ERR_MEMCFG;
    }

    /* ⑥ PHY 工作模式 —— 得到这里做, 否则会被 ⑤ 的软复位清掉 */
    w5500_phy_config();

    /* ⑦ 结果快照 —— 此刻自协商还没完成 (要 1~2 秒), 所以多半是 0xFA,
     *    稍后由 vNetTask 报出 0xFF */
    LOG_I("init ok, VERSIONR=0x%02X, PHYCFGR=0x%02X", ver, W5500_Drv_ReadPhyCfg());

    return W5500_OK;
}

uint8_t W5500_Drv_ReadVersion(void)
{
    return WIZCHIP_READ(VERSIONR);
}

uint8_t W5500_Drv_ReadPhyCfg(void)
{
    return WIZCHIP_READ(PHYCFGR);
}

const char *W5500_Drv_StatusStr(W5500_Status_t st)
{
    switch (st) {
    case W5500_OK:            return "OK";
    case W5500_ERR_SPI:       return "SPI 无响应 (MISO 恒定, 查接线/供电/片选)";
    case W5500_ERR_NOT_FOUND: return "有响应但 VERSIONR != 0x04 (查芯片型号)";
    case W5500_ERR_MEMCFG:    return "socket 缓冲分配被拒 (检查各 socket 之和是否超过 16KB)";
    default:                  return "未知状态";
    }
}
