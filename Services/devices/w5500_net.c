/**
 * @file    w5500_net.c
 * @brief   W5500 网络层 —— 网络参数配置 + socket 封装
 * @note    寄存器访问一律走 ioLibrary 的 WIZCHIP_READ/WRITE, 它们最终调用
 *          w5500_port.c 注册进来的回调 —— 而回调里带着总线锁和片选控制。
 *          本文件不直接碰 SPI。
 */

#define LOG_TAG "W5500"
#include "log.h"

#include "w5500_net.h"

#include "wizchip_conf.h"

#include <string.h>     /* memcmp */

/* ================================================================
 * 静态网络参数
 *
 * 场景: 板子直连 PC 网口 (PC 网卡配 192.168.1.1/24)。这样 Wireshark 既是
 * 对端又是抓包点, 板子的每个报文都看得到 —— 经过路由器就只能抓广播了。
 *
 * MAC 首字节 0x02 = 本地管理 + 单播, 不会撞到任何真实厂商的 OUI。
 * 换环境只改这张表。
 * ================================================================ */

static const wiz_NetInfo kNetStatic = {
    .mac  = { 0x02, 0x08, 0xDC, 0x01, 0x02, 0x03 },
    .ip   = { 192, 168, 1, 100 },
    .sn   = { 255, 255, 255, 0 },
    .gw   = { 192, 168, 1, 1 },
    .dns  = { 192, 168, 1, 1 },     /* 接 DNS 时改成真实 DNS 服务器 */
    .dhcp = NETINFO_STATIC,
};

/* ================================================================
 * 内部
 * ================================================================ */

/**
 * @brief  比较两份 netinfo 是否一致
 * @note   逐字段比, 【不】memcmp 整个结构体: 结构体里有填充字节, 而读回的那份
 *         是栈上未初始化的, 填充内容不确定 —— memcmp 会误报不一致。
 */
static int netinfo_equal(const wiz_NetInfo *a, const wiz_NetInfo *b)
{
    return memcmp(a->mac, b->mac, sizeof(a->mac)) == 0
        && memcmp(a->ip,  b->ip,  sizeof(a->ip))  == 0
        && memcmp(a->sn,  b->sn,  sizeof(a->sn))  == 0
        && memcmp(a->gw,  b->gw,  sizeof(a->gw))  == 0
        && memcmp(a->dns, b->dns, sizeof(a->dns)) == 0
        && a->dhcp == b->dhcp;
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void W5500_Net_ConfigStatic(void)
{
    wiz_NetInfo back;

    /* 上游签名是非 const 指针 (它只读不改), 这里投一次去掉 const */
    wizchip_setnetinfo((wiz_NetInfo *)&kNetStatic);
    wizchip_getnetinfo(&back);      /* 从寄存器读回 —— 顺便验证写入真的落地了 */

    LOG_I("mac %02X:%02X:%02X:%02X:%02X:%02X",
          back.mac[0], back.mac[1], back.mac[2],
          back.mac[3], back.mac[4], back.mac[5]);
    LOG_I("static ip %u.%u.%u.%u / %u.%u.%u.%u, gw %u.%u.%u.%u, dns %u.%u.%u.%u",
          back.ip[0], back.ip[1], back.ip[2], back.ip[3],
          back.sn[0], back.sn[1], back.sn[2], back.sn[3],
          back.gw[0], back.gw[1], back.gw[2], back.gw[3],
          back.dns[0], back.dns[1], back.dns[2], back.dns[3]);

    if (!netinfo_equal(&kNetStatic, &back)) {
        LOG_W("netinfo 读回与写入不一致 —— SPI 帧有错时寄存器写入会被静默丢弃");
    }
}
