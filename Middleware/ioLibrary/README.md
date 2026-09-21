# ioLibrary_Driver (WIZnet)

W5500 硬件 TCP/IP 协议栈。**第三方只读代码 —— 不要修改本目录下的上游文件。**

## 来源

| | |
| --- | --- |
| 上游 | https://github.com/Wiznet/ioLibrary_Driver |
| 分支 | `master` |
| 引入日期 | 2026-09-21 |
| 许可证 | **MIT**（见 [license.txt](license.txt)）—— 可自由使用、修改、再分发，保留版权声明即可 |

## 本工程实际引入的文件

只取了 W5500 需要的部分，其余芯片（W5100/W5100S/W5200/W5300/W6100/W6300）
和用不到的应用协议（HTTP Server / MQTT / SNMP / SNTP / TFTP / AAC）没有引入。

```
Ethernet/
  wizchip_conf.c/h   移植层 —— 回调注册与应用协议无关的部分
  socket.c/h         BSD Socket 风格 API
  W5500/w5500.c/h    W5500 寄存器驱动
Internet/
  DHCP/dhcp.c/h      DHCP 客户端
  DNS/dns.c/h        DNS 解析
```

## 与硬件怎么接上

ioLibrary 是【片外】芯片的协议栈，它自己不认识 SPI，必须由宿主告诉它怎么访问：

```c
void reg_wizchip_cris_cbfunc(void(*cris_en)(void), void(*cris_ex)(void));   // 临界区
void reg_wizchip_cs_cbfunc  (void(*cs_sel)(void), void(*cs_desel)(void));   // 片选
void reg_wizchip_spi_cbfunc (uint8_t (*spi_rb)(void), void (*spi_wb)(uint8_t wb));  // SPI 字节
```

**回调实现在 [Services/devices/w5500_port.c](../../Services/devices/w5500_port.c)**，
不放在本目录 —— 否则 Middleware 就要反向依赖 BSP 了。

## 选型是用编译定义覆盖的，没有改上游文件

`wizchip_conf.h` 里 `_WIZCHIP_` 和 `_WIZCHIP_IO_MODE_` 两处都用 `#ifndef` 守卫，
所以型号选择放在 [CMakeLists.txt](CMakeLists.txt) 里：

```cmake
target_compile_definitions(iolibrary PUBLIC
    _WIZCHIP_=W5500
    _WIZCHIP_IO_MODE_=_WIZCHIP_IO_MODE_SPI_VDM_
)
```

> ⚠️ 上游默认 `_WIZCHIP_` 是 **W6300**，不覆盖会编译错误或行为完全不对。

## 怎么更新

1. 从上游重新下载
2. 只覆盖上表列出的那些文件
3. 检查 `wizchip_conf.h` 的 `#ifndef` 守卫是否还在（在的话选型仍然不需改上游）
4. 编译 + 实测

因为选型走编译定义、硬件访问走注册回调，**理论上更新是纯粹的覆盖操作**，
不需要手工合并。这是刻意保持的 —— 一旦开始改上游文件，这个性质就没了。
