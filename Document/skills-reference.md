# Skills Reference — HAL_TEST_Board 项目技能 API

> Claude Code 技能详细参考。技能唯一源在 `.agents/skills/`；Claude Code 通过 `python tools/sync_skills.py` 生成的 `.claude/skills/` 使用。

## 快速索引

| 技能 | 命令 | 用途 |
|------|------|------|
| Build | `/build-stm32` | CMake + Ninja 编译 STM32 固件 |
| Flash | `/flash-stm32` | PyOCD 烧录固件到开发板 |
| Reset | `/reset-stm32` | 硬件复位 + 串口启动日志捕获 |
| Serial | `/serial-debug` | 实时串口日志监控 |

---

## `/build-stm32` — 固件编译

**描述**: CMake + Ninja + ARM GNU Toolchain 构建 STM32F103ZET6 固件。

**决策树**:
- `build/Debug/` 不存在 → 首次 configure + build
- CMakeLists.txt 或 .cmake 有变更 → reconfigure + build
- 用户说 clean/rebuild → 完全清理重建
- 用户说 hex/bin → 增量构建 + 生成 HEX/BIN
- 默认 → 增量构建

**命令**:
```bash
# 增量构建（默认）
cmake --build build/Debug 2>&1

# 首次配置 + 构建
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake 2>&1
cmake --build build/Debug 2>&1

# 完全重建
rm -rf build/Debug
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake 2>&1
cmake --build build/Debug 2>&1

# 生成 HEX/BIN
arm-none-eabi-objcopy -O ihex build/Debug/HAL_TEST_Board.elf build/Debug/HAL_TEST_Board.hex
arm-none-eabi-objcopy -O binary build/Debug/HAL_TEST_Board.elf build/Debug/HAL_TEST_Board.bin
```

**前置依赖**: `arm-none-eabi-gcc`, `cmake`, `ninja` 必须在 PATH 中。

**错误恢复**:
| 错误 | 处理 |
|------|------|
| `ninja: error: loading 'build.ninja'` | 构建目录损坏 → 完全重建 |
| `No such file: CMakeCache.txt` | 未配置 → 首次 configure |
| `arm-none-eabi-gcc: command not found` | 工具链不在 PATH → 报告并停止 |
| 链接错误 | 报告未定义符号, 建议检查 CMakeLists |
| `ninja: no work to do.` | 代码无变更 (成功) |

---

## `/flash-stm32` — 固件烧录

**描述**: 通过 PyOCD + DAP-Link 烧录固件到 STM32F103ZET6。

**两种模式**:

### 默认模式 (快速烧录, 99% 场景)
```bash
pyocd flash --target stm32f103ze --connect under-reset \
  -O reset_type=hw build/Debug/HAL_TEST_Board.elf 2>&1
```
跳过所有检查，直接执行烧录命令。失败时查阅错误表。

### 安全模式 (`/flash-stm32 safe`)
仅当用户明确请求时使用，按序检查:
1. ELF 文件存在性
2. PyOCD 可执行性
3. DAP-Link 探针可用性 (`pyocd list`)
4. ELF 时效性 (vs main.c 修改时间)

**错误恢复**:
| 错误 | 处理 |
|------|------|
| `No target connected` | 检查 USB 连接 + 板子供电; 按住 RESET 再试 |
| `No probes found` | 重新插拔 DAP-Link USB |
| `ELF not found` | 先执行 `/build-stm32` |
| `pyocd: command not found` | `pip install pyocd` |
| `Multiple probes found` | 用 `--uid <uid>` 指定探针 |
| `Erase failed` | `pyocd commander --unlock` |

---

## `/reset-stm32` — 硬件复位

**描述**: 通过 PyOCD 硬件复位 STM32F103ZET6，可选择捕获 USART1 启动日志 (自动识别 CH340, 115200)。

**快速复位 (不捕获日志)**:
```bash
pyocd reset -t stm32f103ze -m hw -M attach 2>&1
```

**复位 + 捕获启动 Banner**:
```bash
# 默认自动识别 CH340；需要指定时加 --port COMx
python tools/capture_boot.py --baud 115200 --wait 4.0 2>&1
```

**通用串口收发**:
```bash
# 发送并读取响应
python tools/serial_cli.py --send "hello\r\n" --read 1.0

# 仅读取 N 秒
python tools/serial_cli.py --read 3.0

# 自定义波特率（端口自动识别；需要时加 --port COMx）
python tools/serial_cli.py --baud 9600 --send "AT\r\n" --read 2.0
```

**注意事项**:
- `-M attach`: 连接后不暂停核心 (关键参数, 默认 halt 会冻结 MCU)
- `-m hw`: 通过 nRESET 引脚硬件复位

---

## `/serial-debug` — 串口监控

**描述**: 打开独立的 Git Bash 终端实现实时串口日志监控 (UTF-8, 无编码问题)。

**流程**:
1. 调用 `/serial-debug` → 打开监控窗口
2. 进行串口调试 → 所有 TX/RX 在监控窗口可见
3. 监控窗口跨会话保持打开直到用户关闭

**监控窗口启动**:
```powershell
Start-Process bash -ArgumentList '-c "echo Serial Monitor — watching build/serial.log && tail -f build/serial.log"'
```

**带日志记录的串口命令**:
```bash
# 发送 + 读取 (写入日志)
python tools/serial_cli.py --send "cmd\r\n" --read 2.0 --log build/serial.log 2>&1

# 复位 + 捕获启动 banner (写入日志)
python tools/capture_boot.py --wait 4.0 --log build/serial.log 2>&1
```

**错误恢复**:
| 错误 | 处理 |
|------|------|
| bash: command not found | 安装 Git Bash 或用 `Get-Content build/serial.log -Wait` |
| 监控窗口乱码 | 确保使用 Git Bash (非 PowerShell) |
| 日志文件不存在 | 先执行任意串口命令创建日志文件 |
