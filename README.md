# HAL_TEST_Board — STM32F103ZET6 开源工具链工程

基于 **STM32F103ZET6**（野火霸道 V2 开发板）的 HAL 外设测试固件，使用 **全开源工具链**（VS Code + CMake + Ninja + ARM GCC + Clangd + PyOCD + Cortex-Debug）。

> **目标板**: 野火霸道 V2 (YHBDv2) | **调试器**: DAP-LINK (CMSIS-DAP / SWD)
> 完整项目参考: [Document/project-reference.md](Document/project-reference.md) | 编码规范: [Document/conventions.md](Document/conventions.md)
> Claude Code 技能: [Document/skills-reference.md](Document/skills-reference.md)

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

---

## 快速开始

### 安装

```bash
pip install cmake ninja pyocd
code --install-extension llvm-vs-code-extensions.vscode-clangd
code --install-extension marus25.cortex-debug
```

ARM GCC 从 [ARM Developer](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 下载，确保 `arm-none-eabi-gcc` 在 PATH 中。

### 构建

```bash
# VS Code: Ctrl+Shift+P → CMake: Configure (选 Debug), 然后 CMake: Build (F7)
# 或终端:
cmake --preset Debug
cmake --build --preset Debug
```

产物: `build/Debug/HAL_TEST_Board.elf`。

### 烧录

连接 DAP-LINK，状态栏点击 `DAP-LINK Flash` 或:

```bash
pyocd flash --target stm32f103ze --connect under-reset -O reset_type=hw build/Debug/HAL_TEST_Board.elf
```

### 调试

按 F5 → Flash & Debug，自动停在 `main()`。

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

> 完整 Pin Map 见 [Document/project-reference.md](Document/project-reference.md)

---

## 常见问题

| 问题                                     | 解决                                                        |
| ---------------------------------------- | ----------------------------------------------------------- |
| `arm-none-eabi-gcc: command not found` | ARM GCC 未加入 PATH                                         |
| PyOCD Flash 失败                         | 检查连接, 按住 RESET 再试;`pyocd list` 确认探针           |
| 调试启动超时                             | 检查 `launch.json` 中 `overrideGDBServerStartedRegex`   |
| Clangd 报 unknown type                   | 确保 `-DSTM32F103xE -DUSE_HAL_DRIVER` 已在 `.clangd` 中 |
