#!/usr/bin/env python3
"""
串口自动枚举 —— 供 capture_boot.py / serial_cli.py 共用。

板子的 USB 转串口是 CH340（沁恒），但 Windows 把它枚举成 COM 几并不固定：
换 USB 口、插拔顺序、重装驱动都会变。所以这里按 USB VID/PID 找设备，
不关心它这次叫 COM 几。

仍然可以显式指定覆盖：`--port COM7`
"""

import sys

import serial.tools.list_ports

# 沁恒 CH340/CH341 系列的 USB ID（不同封装/批次可能落在不同 PID）
_CH_VID = 0x1A86
_CH_PIDS = (0x7523, 0x5523, 0x7522, 0x55D3)


def list_ports():
    """当前所有串口，按设备名排序（COM2 在 COM10 前面）"""
    return sorted(serial.tools.list_ports.comports(),
                  key=lambda p: (len(p.device), p.device))


def find_ch340():
    """返回匹配到的 CH340 串口列表（可能不止一个）"""
    ports = list_ports()

    hits = [p for p in ports if p.vid == _CH_VID and p.pid in _CH_PIDS]
    if hits:
        return hits

    # VID 匹配不上时退回描述串匹配 —— 某些克隆芯片/魔改驱动不报标准 PID
    return [p for p in ports if "CH34" in (p.description or "").upper()]


def resolve_port(explicit=None, log=print):
    """
    决定这次用哪个串口。

    :param explicit: 用户显式指定的串口名；给了就用它，不做任何猜测
    :param log:      日志回调（脚本各自传自己的 log，保持输出格式统一）
    :return:         设备名（如 "COM3"）；无法确定时返回 None
    """
    if explicit:
        log(f"port: 使用显式指定的 {explicit}")
        return explicit

    hits = find_ch340()

    if len(hits) == 1:
        log(f"port: 自动识别 CH340 → {hits[0].device} ({hits[0].description})")
        return hits[0].device

    if not hits:
        log("port ERROR: 没找到 CH340 串口。当前系统可见的串口：")
        ports = list_ports()
        if ports:
            for p in ports:
                log(f"           {p.device}  {p.description}")
        else:
            log("           (一个都没有 —— 板子 USB 线没插?)")
        log("         排查: 板子 USB 线 / 驱动 / 或用 --port COMx 显式指定")
        return None

    # 多个 CH340：不猜，但也不卡住 —— 取第一个并把全部列出来
    log(f"port WARN: 找到 {len(hits)} 个 CH340，先取第一个；不对就用 --port 指定：")
    for p in hits:
        log(f"           {p.device}  {p.description}  [{p.hwid}]")
    return hits[0].device


if __name__ == "__main__":
    # 直接运行 = 查当前情况, 方便排错
    sys.exit(0 if resolve_port() else 1)
