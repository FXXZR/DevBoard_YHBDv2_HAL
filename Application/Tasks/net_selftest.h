/**
 * @file    net_selftest.h
 * @brief   网络自检脚手架 —— W2 的验证代码, 不是产品流程
 *
 * ⚠️ 生命周期只到 W2 结束: D5 接上 DHCP/DNS 后, 这个位置由业务流程取代,
 *    届时整个 net_selftest.c 删掉即可 (task_net.c 里只有一行调用)。
 *
 * 必须在【网络任务】里调用 —— socket 层不可重入, 内部也用到了 vTaskDelay。
 */

#ifndef __NET_SELFTEST_H__
#define __NET_SELFTEST_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 依次跑完 TCP 客户端 / UDP 收包统计 / TCP 服务端三组自检
 * @note  阻塞式, 全程数十秒。链路起来之后调用一次即可。
 *        每一组都要 PC 侧的 tools/net_peer.py 配合, 具体命令打在日志里。
 */
void NetSelfTest_Run(void);

/* ================================================================
 * D4: 中断驱动的 UDP 收包
 *
 * 与上面那组"阻塞式自检"是两种形态 —— 这一组【不阻塞】, 它挂在网络任务的
 * 事件循环上: 平时任务睡着, W5500 的 INT 拉低 → ISR 置事件位 → 任务醒来
 * 收包。正是产品代码该有的样子。
 *
 * 观察点: ISR 次数 vs 实际收包数。前者远小于后者是正常的 —— 一次唤醒把
 * 缓冲里的包全搬走, 中断被"合并"了; 反过来 (ISR 多于包) 说明有空唤醒,
 * 要回去查标志清除顺序。
 * ================================================================ */

/** @brief 打开演示 socket 并打一条 PC 侧命令提示 (链路起来后调一次) */
void NetIntTest_Start(void);

/** @brief ISR 里的一句记账 (次数 + 时间戳), 只读几个变量, 不做别的事 */
void NetIntTest_OnIsr(void);

/** @brief 事件循环里发现某个 socket 有数据时调用 (非本演示的 socket 会被忽略) */
void NetIntTest_OnRecv(uint8_t sn);

/** @brief 周期服务 (1Hz): 一次突发结束时打总账 */
void NetIntTest_Report(void);

#ifdef __cplusplus
}
#endif

#endif /* __NET_SELFTEST_H__ */
