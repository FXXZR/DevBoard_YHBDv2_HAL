---
name: reset-stm32
description: Reset STM32F103ZET6 via PyOCD (hardware reset, no halt). Captures serial boot banner on auto-detected CH340 115200.
---

# STM32 Reset + Serial Capture

Hardware-reset the STM32F103ZET6 via DAP-Link and capture the USART1 boot banner.

## 串口自动识别（不用再查 COM 几）

**不要写死端口号。** 板子的 CH340 枚举成 COM 几是不固定的（换 USB 口、
插拔顺序、重装驱动都会变），所以 `tools/serial_port.py` 按 USB VID/PID
自动找，两个脚本都默认走这条路：

```bash
python tools/serial_port.py                 # 单跑一次 = 看当前识别到什么
python tools/capture_boot.py --wait 4.0     # 自动识别, 无需 --port
```

需要指定时再显式覆盖：`--port COMx`（给了就不猜）。

识别失败时会列出当前所有可见串口，方便判断是驱动问题还是线的问题。

> ⚠️ 如果报 `PermissionError: 拒绝访问` —— 说明**端口被别的程序占着**，
> 不是识别失败。常见占用者：串口助手、VSCode 串口监视器、
> `/serial-debug` 开的 tail 窗口。关掉再来。

## TL;DR

```bash
python tools/serial_cli.py --baud 115200 --read 3.0 &
pyocd reset -t stm32f103ze -m hw -M attach 2>&1
wait
```

But use the coordinated Python script below for reliable banner capture.

## Quick Reset (no capture)

```bash
pyocd reset -t stm32f103ze -m hw -M attach 2>&1
```

- `-m hw`: hardware reset via nRESET pin (same as pressing reset button)
- `-M attach`: connect without halting the core (critical — default `halt` will freeze MCU!)

## Reset + Capture Boot Banner

```bash
python tools/capture_boot.py --baud 115200 --wait 4.0 2>&1
```

> 启动流程里 W5500 自检含约 500ms 复位等待，抓包时 `--wait` 建议给到 6.0。

This script:
1. 自动识别 CH340 端口，在后台线程里读取
2. Runs `pyocd reset -t stm32f103ze -m hw -M attach`
3. Collects all serial output for `--wait` seconds
4. Prints captured text to stdout

## General Serial TX/RX

```bash
# Send and read response
python tools/serial_cli.py --send "hello\r\n" --read 1.0

# Just read for N seconds
python tools/serial_cli.py --read 3.0

# 换波特率 (端口仍自动识别)
python tools/serial_cli.py --baud 9600 --send "AT\r\n" --read 2.0

# 显式指定端口 —— 只在自动识别不对/有多个 CH340 时才需要
python tools/serial_cli.py --port COMx --read 2.0
```

> 两类错误要分清：
> - `port ERROR: 没找到 CH340` + 列出其他串口 → **识别问题**（线/驱动/非 CH340 芯片）
> - `PermissionError: 拒绝访问` → **端口被占用**，识别是对的，去关掉占用的程序

## Troubleshooting

| Symptom | Fix |
|---|---|
| LED stops blinking after reset | PyOCD left MCU halted → power-cycle board |
| `No probes found` | Re-plug DAP-Link USB |
| Serial returns nothing | ⚠️ **先确认烧的是哪一版**: Release 构建日志只留 WARN 及以上, 启动横幅 (`LOG_I`/`LOG_D`) 全被裁掉 —— **正常启动就是完全静默的**, 别误判成死机。判断依据看 LED 心跳/屏幕。若烧的是 Debug 才按下面查: Verify port with `[System.IO.Ports.SerialPort]::GetPortNames()` |
| Wrong baud/noise | Default is 115200-8-N-1 on USART1 (CH340) |
