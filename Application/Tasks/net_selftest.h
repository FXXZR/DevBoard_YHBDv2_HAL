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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 依次跑完 TCP 客户端 / UDP 收包统计 / TCP 服务端三组自检
 * @note  阻塞式, 全程数十秒。链路起来之后调用一次即可。
 *        每一组都要 PC 侧的 tools/net_peer.py 配合, 具体命令打在日志里。
 */
void NetSelfTest_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* __NET_SELFTEST_H__ */
