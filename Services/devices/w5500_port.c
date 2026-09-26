/**
 * @file    w5500_port.c
 * @brief   ioLibrary 移植层 —— 把 W5500 接到本工程的 SPI 总线上
 * @note    ioLibrary 是【片外】芯片的协议栈, 它自己不认识 SPI/GPIO, 必须由
 *          宿主告诉它怎么访问硬件。这就是那几组回调存在的全部原因。
 *
 *          这些回调【不能】放在 Middleware/ioLibrary 里 —— 那样 Middleware
 *          就要反向依赖 BSP 了。放在 Services 层, 全是向下依赖:
 *
 *              Services/devices/w5500_port.c
 *                    ↓ bsp_w5500_hw (CS)  ↓ bsp_spi (字节)  ↓ spi_bus (锁)
 *                  BSP
 *
 *          注册时机由 W5500_Drv_Init() 负责, 早于任何一次 WIZCHIP_READ。
 */

#include <stddef.h>             /* NULL */

#include "w5500_drv.h"
#include "spi_bus.h"
#include "bsp_spi.h"
#include "bsp_w5500_hw.h"
#include "bsp_assert.h"

#include "wizchip_conf.h"       /* ioLibrary 的移植层接口 */

/* ================================================================
 * 配置
 * ================================================================ */

#define W5500_SPI_BUS               BSP_SPI_BUS_2

/* SPI 传输超时 (单字节与 burst 共用). 18MHz 下一字节约 0.44us, 2KB 事务
 * 纯线速也只有 0.91ms —— 10ms已经够多了 */
#define W5500_SPI_TIMEOUT_MS        10

/* 等总线锁的超时. 事务都不长 (寄存器访问 4 字节; socket 缓冲最多 2KB),
 * 正常情况锁总是立即到手。给个宽裕但有限的值 —— 超时说明有任务卡住或
 * 死锁, 那种情况必须响, 不能静默继续。
 *
 * 2KB 事务的持锁时长: 18MHz 下纯线速 0.91ms, 见 W5500_SPI_TIMEOUT_MS 的说明. */
#define W5500_LOCK_TIMEOUT_MS       100

/* ================================================================
 * 临界区回调
 *
 * ★ 刻意留空 —— 这不是偷懒, 理由必须写清楚, 否则后人会"顺手补上"。
 *
 * ioLibrary 的调用顺序是:
 *
 *     WIZCHIP_CRITICAL_ENTER()      ← cris_en  (最外层)
 *     WIZCHIP.CS._select()          ← cs_sel   (锁在这里)
 *     传输...
 *     WIZCHIP.CS._deselect()        ← cs_desel
 *     WIZCHIP_CRITICAL_EXIT()       ← cris_ex
 *
 * 即 cris 把整个事务 (含 CS) 包在里面。如果 cris_en 真去关中断, 就会出现:
 *
 *   ① 关着中断去等互斥量 —— 中断里阻塞是非法操作, 直接死锁;
 *   ② 就算不等锁, 一次 socket 缓冲读有 2KB, 18MHz 下约 1ms,
 *      关中断 1ms 会毁掉整个系统的时基与实时性。
 *
 * 串行化由加在 CS 上的总线互斥量负责; 而 ISR 不访问 W5500 (bsp_spi 里有
 * __get_IPSR() 断言挡着), 所以这里不需要再做临界区。
 * ================================================================ */

static void port_cris_enter(void) { }
static void port_cris_exit(void)  { }

/* ================================================================
 * 片选回调
 * ================================================================ */

static void port_cs_select(void)
{
    /* ★ 锁的边界 = CS 的边界。
     *
     * ioLibrary 的每一个寄存器事务都是 select…deselect 包起来的, 所以把
     * 锁加在这里就【自动】获得了"整帧事务"的粒度 —— 一行都不用改 ioLibrary
     * 源码。这正是注册模式的价值。
     *
     * 失败就断言: 回调签名没有返回错误的通道, 静默继续会导致两个任务的帧
     * 在 MOSI 上交错, 现象是"偶发读到错误寄存器值", 极难定位。宁可当场响。 */
    BSP_ASSERT(SpiBus_Lock(W5500_SPI_BUS, W5500_LOCK_TIMEOUT_MS) == 0);
    BSP_W5500_HW_CsLow();
}

static void port_cs_deselect(void)
{
    BSP_W5500_HW_CsHigh();
    SpiBus_Unlock(W5500_SPI_BUS);
}

/* ================================================================
 * SPI 字节收发回调
 *
 * SPI 是全双工: 每发一个字节必然同时收一个字节。不收走的话 RXNE 会一直
 * 置位并最终 OVR 溢出, 所以两个方向都走 BSP_SPI_Transfer (完整的一次交换),
 * 写方向直接丢弃收到的字节。
 * ================================================================ */

static void port_spi_write(uint8_t wb)
{
    (void)BSP_SPI_Transfer(W5500_SPI_BUS, wb, W5500_SPI_TIMEOUT_MS);
}

static uint8_t port_spi_read(void)
{
    /* 0xFF 是 dummy —— W5500 在读方向忽略 MOSI。
     * 用 0xFF 而不是 0x00 是习惯: MOSI 空闲为高。 */
    return BSP_SPI_Transfer(W5500_SPI_BUS, 0xFF, W5500_SPI_TIMEOUT_MS);
}

/* ================================================================
 * SPI burst 回调 —— socket 数据面
 *
 * 上面的单字节回调只够访问寄存器 (每帧 4 字节). 一旦收发 socket 数据,
 * ioLibrary 走的是 WIZCHIP_READ_BUF/WRITE_BUF，提供适配能提高效率；
 * ================================================================ */

static void port_spi_read_burst(uint8_t *pBuf, uint16_t len)
{
    /* 读方向 不关心写什么，采用原地传输, 不用为 dummy 再开一块缓冲 */
    BSP_SPI_TransferBuffer(W5500_SPI_BUS, pBuf, pBuf, len, W5500_SPI_TIMEOUT_MS);
}

static void port_spi_write_burst(uint8_t *pBuf, uint16_t len)
{
    /* 写方向不关心收到的字节 —— rx 传 NULL, BSP 会走 HAL_SPI_Transmit */
    BSP_SPI_TransferBuffer(W5500_SPI_BUS, pBuf, NULL, len, W5500_SPI_TIMEOUT_MS);
}

/* ================================================================
 * 注册
 * ================================================================ */

void W5500_Port_Register(void)
{
    reg_wizchip_cris_cbfunc(port_cris_enter, port_cris_exit);
    reg_wizchip_cs_cbfunc  (port_cs_select,   port_cs_deselect);
    reg_wizchip_spi_cbfunc (port_spi_read,    port_spi_write);

    /* socket 数据面 —— 见上方 burst 回调的说明. 不注册的话 ioLibrary 会
     * 退回逐字节循环, 功能一样但吞吐差 2~3 倍。 */
    reg_wizchip_spiburst_cbfunc(port_spi_read_burst, port_spi_write_burst);
}
