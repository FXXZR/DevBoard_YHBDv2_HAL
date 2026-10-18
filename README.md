# HAL_TEST_Board — STM32F103ZET6 工程

基于 **STM32F103ZET6**（野火霸道 V2 开发板）的嵌入式工程：HAL 外设驱动、FreeRTOS、
LVGL 显示，以及 **W5500 硬件 TCP/IP** 网络通路。**全开源工具链**（VS Code + CMake + Ninja
+ ARM GCC + Clangd + PyOCD + Cortex-Debug）。

> **目标板**: 野火霸道 V2 (YHBDv2) | **调试器**: DAP-LINK (CMSIS-DAP / SWD)
> 项目参考: [Document/project-reference.md](Document/project-reference.md) · 编码规范: [Document/conventions.md](Document/conventions.md) · 疑难问题: 本地维护（不入库）

---

## 特性

- **分层架构**：`Application → Services → { Middleware, BSP } → Drivers`，依赖只允许向下；
  第三方库源码放在 `Middleware/`（保持上游源码，构建脚本与配置按项目调整），移植层归 `Services/devices/`
- **W5500 网络**：SPI2 驱动 + ioLibrary_Driver；TCP 客户端/服务端、UDP、INT 中断驱动收包；
  socket 封装全部带超时（ioLibrary 原生的 `send`/`recv`/`close` 是无界自旋）
- **DHCP / DNS**：在 W2 中完成 DORA 与公网域名解析实测（测试脚手架已清理，结论见文档）
- **分级日志**：`LOG_E/W/I/D` 预处理期过滤，USART1 环形缓冲 + DMA，非阻塞、任务与 ISR 均可调用
- **FreeRTOS**：单网络任务持有 socket 层（socket 层不可重入）；中断只置事件位，不碰 SPI
- **LVGL 8.3**：显示缓冲放外部 SRAM，DMA flush + 触摸读取
- **外设覆盖**：LED/KEY/BEEP、ADC、RTC、I2C + EEPROM、SPI Flash、SDIO + FatFS、RS485、ESP8266

---

## 架构

```
Application/            业务逻辑、RTOS 任务、移植钩子
  ├─ Tasks/             task_init / led / disp / net
  ├─ Common/            FreeRTOS 移植钩子
  └─ Test/              各外设验证模块 (默认不参与运行, --gc-sections 丢弃)
        ↓
Services/               硬件抽象之上、业务逻辑之下
  ├─ log/               分级日志
  ├─ bus/               SPI 总线互斥 (锁的边界 = 不变量边界)
  └─ devices/           器件驱动 + 第三方库移植层 (w5500_*/diskio/lvgl_port)
        ↓
{ Middleware/,  BSP/ }  平级, 互不依赖
  Middleware/           FreeRTOS / FatFs / LVGL / ioLibrary (只读)
  BSP/                  板级资源 + raw 传输 (唯一 include HAL 的层)
        ↓
Drivers/                ST HAL + CMSIS
```

完整的模块清单、CMake 目标依赖、引脚与时钟见
[project-reference.md](Document/project-reference.md#architecture)。

### 模块速览

| 层 | 关键模块 | 职责 |
| --- | --- | --- |
| Application | `Tasks/task_net.c` | W5500 全生命周期：上电、静态配网、PHY 监控、INT 事件循环 |
| Application | `Tasks/task_disp.c` | LVGL 渲染 + 触摸校准 |
| Services | `devices/w5500_drv.c` | 芯片上电 / 版本校验 / PHY 配置 / socket 缓冲分配 |
| Services | `devices/w5500_net.c` | 网络参数、socket 超时封装、中断状态 |
| Services | `devices/w5500_port.c` | ioLibrary 移植回调（CS/SPI/临界区，含总线锁） |
| Services | `log/` `bus/` | 分级日志 / SPI 总线互斥 |
| Middleware | `ioLibrary/` | WIZnet W5500 协议栈（MIT，原样） |
| BSP | `bsp_w5500_hw.c` | CS=PG9 / RST=PG15 / INT=PG8，SPI2 |

---

## 工具链

| 工具          | 版本                      | 用途            |
| ------------- | ------------------------- | --------------- |
| VS Code       | ≥ 1.90                   | 编辑器          |
| CMake + Ninja | ≥ 3.22                   | 构建系统        |
| ARM GCC       | 14.2 (`arm-none-eabi-`) | 交叉编译        |
| Clangd        | ≥ 18                     | 语言服务器      |
| PyOCD         | ≥ 0.36                   | 烧录 + 调试探针 |
| Cortex-Debug  | 1.12.1                    | GDB 调试前端    |

## 快速开始

### 安装

```bash
pip install cmake ninja pyocd pyserial
code --install-extension llvm-vs-code-extensions.vscode-clangd
code --install-extension marus25.cortex-debug
```

Claude Code 用户首次 clone 后运行 `python tools/sync_skills.py`，把 `.agents/skills/` 链接到
`.claude/skills/`（Windows junction / Unix symlink）；`.claude/skills/` 不入库。

ARM GCC 从 [ARM Developer](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
下载，确保 `arm-none-eabi-gcc` 在 PATH 中。

### 构建

```bash
# VS Code: Ctrl+Shift+B → BUILD (Debug) / BUILD (Release)
# 或终端:
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build build/Debug
```

产物: `build/Debug/HAL_TEST_Board.elf`。Debug / Release 是两套独立构建目录
（`-O0 -g3` / `-Os -g0`，日志级别与 panic 行为不同）。

### 烧录 / 调试

```bash
pyocd flash --target stm32f103ze --connect under-reset -O reset_type=hw build/Debug/HAL_TEST_Board.elf
```

VS Code 按 F5 → **Flash & Debug**，自动停在 `main()`。

---

## 基础硬件连接

| 功能          | GPIO     | 说明                        |
| ------------- | -------- | --------------------------- |
| 红色 LED (R)  | PB5      | 低电平点亮                  |
| 绿色 LED (G)  | PB0      | 低电平点亮                  |
| 蓝色 LED (B)  | PB1      | 低电平点亮                  |
| 蜂鸣器        | PC0      | 高电平鸣响                  |
| KEY1          | PA0      | 无上下拉                    |
| KEY2          | PC13     | 无上下拉                    |
| USART1 (串口) | PA9/PA10 | 115200-8-N-1, printf 重定向 |
| W5500         | PG9/PG15/PG8 + PB13/14/15 | CS / RST / INT + SPI2 |

> 完整 Pin Map 与时钟树见 [Document/project-reference.md](Document/project-reference.md#pin-map)

---

## 文档

| 文档 | 内容 |
| --- | --- |
| [project-reference.md](Document/project-reference.md) | 架构、引脚、时钟、HAL 模块、内存布局、W5500 实测数据 |
| [conventions.md](Document/conventions.md) | 分层、宏、API、日志、Git 提交规范 |
| 疑难问题记录 | 已定位根因的疑难问题与排查教训（本地维护，不入库） |
| [skills-reference.md](Document/skills-reference.md) | Claude Code 技能参考 |

---

## 常见问题

| 问题                                     | 解决                                                        |
| ---------------------------------------- | ----------------------------------------------------------- |
| `arm-none-eabi-gcc: command not found` | ARM GCC 未加入 PATH                                         |
| PyOCD Flash 失败                         | 检查连接/板子电源, 按住 RESET 再试;`pyocd list` 确认探针   |
| 调试启动超时                             | 检查 `launch.json` 中 `overrideGDBServerStartedRegex`   |
| Clangd 报 unknown type                   | 确保 `-DSTM32F103xE -DUSE_HAL_DRIVER` 已在 `.clangd` 中 |

---

## License

MIT —— 见 [LICENSE](LICENSE)。

`Middleware/` 下的第三方库各保留其原始许可：FreeRTOS (MIT)、FatFs (BSD 风格)、
LVGL (MIT)、WIZnet ioLibrary_Driver (MIT)。
