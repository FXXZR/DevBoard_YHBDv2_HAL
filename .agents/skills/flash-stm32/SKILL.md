---
name: flash-stm32
description: PyOCD flash firmware to STM32F103ZET6 via DAP-Link. Default: instant flash of the Debug build. Add "release" to flash the Release build. Use "/flash-stm32 safe" for full pre-flight verification.
---

# STM32 Flash (PyOCD)

Burn firmware to STM32F103ZET6 via DAP-Link. **Default mode skips all checks for speed** — just fires the flash command.

## 选择固件

| 参数 | 固件 | 何时用 |
| --- | --- | --- |
| （默认） | `build/Debug/HAL_TEST_Board.elf` | 日常调试 |
| `release` | `build/Release/HAL_TEST_Board.elf` | 验证发布固件 |

参数可组合，顺序任意：`/flash-stm32 release safe`

### 烧 Release 前必须先告诉用户这两件事

Release 与 Debug 有两处**行为差异**，不知道的话很容易误判成故障：

1. **日志只留 WARN 及以上** —— 启动横幅是 `LOG_I`/`LOG_D`，全部被裁掉。
   **正常启动时串口一个字都没有**，这不代表死机。
   要确认它真的跑起来了，看 LED 心跳 / 屏幕，或挂调试器看 PC。
2. **panic 走 `NVIC_SystemReset()` 而不是停机** —— 持续性断言会导致**反复重启**
   （表现为"上电就重启"）。好在复位前的 `*** ASSERT` 会从串口打出来。

### Release 目录可能没配置过

Release 有独立的 CMakeCache。烧之前先确认产物存在：

```bash
test -f build/Release/HAL_TEST_Board.elf && echo OK || echo "需要先构建"
```

不存在 → 按 `/build-stm32` 里的 "Release 首次配置" 命令构建，**不要直接烧**。

## Modes

### Default — Fast flash (use 99% of the time)

Skip all checks. Run the flash command directly. Only investigate if it fails.

```bash
pyocd flash --target stm32f103ze --connect under-reset \
  -O reset_type=hw build/Debug/HAL_TEST_Board.elf 2>&1
```

Release 版只把末尾路径换成 `build/Release/HAL_TEST_Board.elf`。

**If it fails**: read the error message and match against the troubleshooting table below.

### Safe mode — Full verification (`/flash-stm32 safe`)

Only when user explicitly requests safe mode. Run checks in order, stop at first failure:

1. `test -f build/<Debug|Release>/HAL_TEST_Board.elf` — missing → auto `/build-stm32`
2. `pyocd --version` — missing → `pip install pyocd`
3. `pyocd list` — no probe → stop, tell user
4. `stat` ELF vs `main.c` — ELF stale → warn, ask whether to rebuild

Then flash. This mode is for debugging probe/board issues, not daily use.

## After Flash (both modes)

Report result briefly:
- ✅ Success: erase/program size and speed
- ❌ Failure: error snippet + matching fix from troubleshooting

## Troubleshooting

| Symptom | Fix |
|---|---|
| `No target connected` | Check USB cable + board power; hold RESET during flash |
| `DebugPortSetup ... Unexpected ACK '0'` | **探针到 MCU 之间断了**。先 `pyocd list` 确认探针本身在（在 = 探针没问题）。然后依次查：① 板子**独立电源开关**（DAP-Link 的 USB 不一定给板子供电，最常见）② SWD 排线松动 ③ 按住 RESET 再烧。降时钟 (`-f 500k`) 和换 `--connect halt` 都无效就基本确定是硬件侧 |
| `No probes found` | Re-plug DAP-Link USB |
| `ELF not found` | Run `/build-stm32`（Release 见上面的首次配置） |
| `pyocd: command not found` | `pip install pyocd` |
| `Multiple probes found` | `pyocd list`, add `--uid <uid>` to command |
| `Erase failed` | `pyocd commander --unlock` |
