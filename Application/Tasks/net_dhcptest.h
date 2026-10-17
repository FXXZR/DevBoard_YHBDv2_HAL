/**
 * @file    net_dhcptest.h
 * @brief   D5 脚手架 —— DHCP 要地址 (DORA) + DNS 解析公网域名
 *
 * ⚠️ 与 net_selftest.* 同命: W2 的验证代码, D6 清理。
 *
 * 与 net_selftest 的接线差别 —— 这一组要【板子 ↔ 路由器】:
 *   DHCP 得有服务器, DNS 得有出口。PC 接在同一个路由器上时, DORA 四步全是
 *   广播, Wireshark 仍然抓得到 (过滤器 bootp); DNS 是单播, PC 侧看不到,
 *   以板子的日志为准。
 *
 * ⚠️ 必须在【网络任务】里调用 —— socket 层不可重入, 内部也用到了 vTaskDelay。
 */

#ifndef __NET_DHCPTEST_H__
#define __NET_DHCPTEST_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  DHCP 要一个地址, 再用 DHCP 下发的 DNS 服务器解析公网域名
 *
 * @note   阻塞式。顺利时 DHCP 数秒 + 每个域名数十毫秒; DHCP 无人应答时最长
 *         约 60 秒 (期限见 .c 的 NET_DHCP_GIVEUP_MS)。
 *
 *         ★ 它会【改掉板子的网络参数】(地址/掩码/网关/DNS 全部来自路由器),
 *         之后板子的地址以日志里打出来的为准 —— net_peer.py 的 --peer 和
 *         Wireshark 的 ip.addr == 都要跟着换。
 */
void NetDhcpTest_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* __NET_DHCPTEST_H__ */
