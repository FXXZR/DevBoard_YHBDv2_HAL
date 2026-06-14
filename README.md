# LED_Binlk — STM32F103ZET6 开源工具链工程

## 项目概述

基于 **STM32F103ZET6**（野火霸道 V2 开发板）的 LED 闪烁示例工程，使用 **全开源工具链**（VS Code + CMake + Ninja + ARM GCC + Clangd + PyOCD + Cortex-Debug），彻底告别 Keil MDK 等商业 IDE。

工程由 STM32CubeMX 生成 CMake 骨架后，手工配置 clangd 语言服务器、VS Code 编译/烧录/调试任务，现已形成完整的嵌入式开发环境。

> **目标板**：野火霸道 V2（YHBDv2）  
> **主控**：STM32F103ZET6（Cortex-M3, 512KB Flash, 64KB RAM）  
> **调试器**：DAP-LINK（CMSIS-DAP）

---

## 硬件引脚

| 功能 | GPIO | 说明 |
|------|------|------|
| 红色 LED (R) | PB0 | 低电平点亮 |
| 绿色 LED (G) | PB1 | 低电平点亮（当前代码中已注释） |
| 蓝色 LED (B) | PB5 | 低电平点亮 |
| HSE 晶振 | 8 MHz | 外部高速晶振 |

当前固件行为：上电后 R 和 B 交替闪烁（各约 3 秒延时），G 未启用。

---

## 工具链版本

| 工具 | 版本 | 用途 |
|------|------|------|
| **VS Code** | ≥ 1.90 | 编辑器 / IDE |
| **CMake** | ≥ 3.22 | 跨平台构建系统 |
| **Ninja** | 任意 | 构建后端（比 make 更快） |
| **ARM GCC** | 14.2.Rel1 (`arm-none-eabi-`) | 交叉编译工具链 |
| **Clangd** | ≥ 18 | C/C++ 语言服务器（代码补全/跳转/诊断） |
| **PyOCD** | ≥ 0.36 | 调试探针驱动 + Flash 烧录 |
| **Cortex-Debug** | 1.12.1 (VS Code 扩展) | GDB 调试前端 |
| **STM32CubeMX** | ≥ 6.x | 仅用于生成 HAL 工程骨架（非运行时依赖） |

> ⚠️ `arm-none-eabi-*` 必须加入系统 PATH 环境变量。

---

## 项目结构

```
LED_Binlk/
├── CMakeLists.txt                  # 顶层 CMake 构建脚本
├── CMakePresets.json               # CMake 预设（Debug/Release）
├── led_binlk.ioc                   # STM32CubeMX 工程文件（备用，不再依赖）
├── .clangd                         # Clangd 语言服务器配置
│
├── Start/                          # 启动 + 系统初始化 + HAL 适配层
│   ├── CMakeLists.txt
│   ├── startup_stm32f103xe.s       # 启动汇编（向量表 + 复位流程）
│   ├── STM32F103XX_FLASH.ld        # GCC 链接脚本（内存布局）
│   ├── system_stm32f1xx.c          # CMSIS 系统初始化（SystemInit, SystemCoreClock）
│   ├── syscalls.c                  # newlib syscall 桩（_write/_read 等）
│   ├── sysmem.c                    # newlib 堆管理（_sbrk）
│   ├── stm32f1xx_it.c / .h         # HAL 中断服务例程
│   ├── stm32f1xx_hal_msp.c         # HAL 外设底层初始化（MSP）
│   └── stm32f1xx_hal_conf.h        # HAL 模块裁剪配置
│
├── Drivers/                        # 芯片厂商驱动（不改动）
│   ├── CMSIS/                      # ARM CMSIS 核心头文件
│   ├── STM32F1xx_HAL_Driver/       # STM32 HAL 库（按需裁剪）
│   └── svd/
│       └── STM32F103xx.svd         # SVD 外设寄存器描述（调试用）
│
├── BSP/                            # 板级支持包（唯一直接依赖 HAL 的模块）
│   ├── CMakeLists.txt
│   ├── Inc/
│   │   └── bsp_led.h               # LED 控制 API（平台无关）
│   └── Src/
│       └── bsp_led.c               # LED 实现（内部调用 HAL GPIO）
│
├── Application/                    # 应用层（业务逻辑）
│   ├── CMakeLists.txt
│   ├── main.c                      # 程序入口
│   ├── Config/                     # 应用层工程配置
│   ├── Common/                     # 公共类型、宏、数据结构
│   ├── Modules/                    # 独立硬件模块（每个模块一个子目录）
│   ├── DataManager/                # 全局数据管理
│   ├── Communication/              # 通信协议栈
│   └── Tasks/                      # RTOS 任务组织
│
├── Services/                       # 平台级服务（待扩展）
├── ThirdParty/                     # 第三方中间件（待扩展：FreeRTOS, FatFs...）
│
├── cmake/                          # 构建系统配置
│   ├── gcc-arm-none-eabi.cmake     # ARM GCC 交叉编译工具链定义
│   ├── starm-clang.cmake           # （备用）ARM Clang 工具链
│   └── stm32cubemx/
│       └── CMakeLists.txt          # HAL 厂商库编译规则（INTERFACE + OBJECT）
│
├── .vscode/
│   ├── settings.json               # VS Code 工作区设置
│   ├── launch.json                 # 调试启动配置（Cortex-Debug + PyOCD）
│   └── tasks.json                  # 任务配置（HEX/BIN 转换 + PyOCD 烧录）
│
└── build/
    └── Debug/                      # 构建输出目录
        ├── build.ninja             # Ninja 构建规则
        ├── compile_commands.json   # clangd 编译命令数据库
        ├── led_binlk.elf           # ELF 可执行文件（含调试信息）
        ├── led_binlk.hex           # Intel Hex（由 task 生成）
        └── led_binlk.bin           # Raw Binary（由 task 生成）
```

### 分层架构

```
Application/         应用层 ─ 只调用 BSP 接口，不直接使用 HAL API
    │
    ▼
BSP/                 板级层 ─ 封装硬件差异，唯一直接包含 HAL 头文件的层
    │
    ▼
Drivers/             驱动层 ─ STM32 HAL + CMSIS（芯片厂商库）
```

> 分层原则：应用层 `#include "bsp_led.h"` → 调用 `BSP_LED_On()`，绝不在应用层出现 `HAL_GPIO_WritePin()`。换芯片只改 BSP，应用层不动。

---

## 环境搭建步骤

### 1. 安装必需工具

| 工具 | 下载 / 安装方式 |
|------|----------------|
| VS Code | https://code.visualstudio.com/ |
| ARM GCC | https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads（选 `arm-none-eabi`，Windows 选 `.exe` 安装包） |
| CMake & Ninja | `pip install cmake ninja` 或从官网下载 |
| PyOCD | `pip install pyocd` |
| Clangd | VS Code 扩展 `llvm-vs-code-extensions.vscode-clangd` |
| Cortex-Debug | VS Code 扩展 `marus25.cortex-debug` |
| Task Buttons | VS Code 扩展 `actboy168.tasks`（状态栏快捷按钮，可选） |

### 2. VS Code 扩展

```bash
code --install-extension llvm-vs-code-extensions.vscode-clangd
code --install-extension marus25.cortex-debug
code --install-extension actboy168.tasks            # 可选
code --install-extension ms-vscode.cmake-tools       # CMake 集成
```

### 3. 验证环境

```powershell
arm-none-eabi-gcc --version     # 应显示 14.2.Rel1
cmake --version                  # ≥ 3.22
ninja --version
pyocd --version
```

### 4. 构建工程

在 VS Code 中打开本工程，按 `Ctrl+Shift+P` → `CMake: Configure`，选择 **Debug** preset。  
之后 `CMake: Build`（或 F7）即可编译。

或在终端中手动构建：

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

---

## 常用操作

### 编译

`CMake: Build` 快捷键（F7），或在状态栏点击 **Build** 按钮。

产物在 `build/Debug/led_binlk.elf`。

### 生成 HEX / BIN

点击状态栏 `GET HEX & BIN` 按钮，或运行任务：
- `Terminal` → `Run Task...` → `GET HEX & BIN`

生成 `build/Debug/led_binlk.hex` 和 `build/Debug/led_binlk.bin`。

### 烧录（PyOCD Flash）

1. 连接 DAP-LINK 调试器
2. 点击状态栏 `DAP-LINK Flash` 按钮
3. 或运行任务 `PYOCD Flash`

等效手动命令：

```powershell
pyocd flash --target stm32f103ze --connect under-reset -O reset_type=hw build/Debug/led_binlk.elf
```

### 调试（Cortex-Debug）

1. 按 `F5` 或点击 `Run and Debug` → `Cortex-Debug (PyOCD)`
2. 程序下载后自动停在 `main()` 函数入口
3. 使用断点、单步、变量监视、外设寄存器（SVD）等调试功能

> 如果调试启动超时，检查 DAP-LINK 连接。  
> 调试配置中 `overrideGDBServerStartedRegex` 已适配 PyOCD GDB Server 的输出格式。

---

## 配置要点说明

### CMakePresets.json

定义 **Debug** 和 **Release** 两个配置预设，均使用 `Ninja` 生成器和 `gcc-arm-none-eabi.cmake` 工具链文件。

Debug：`-O0 -g3`（无优化 + 完整调试符号）  
Release：`-Os -g0`（体积优化 + 无调试符号）

### CMake 工具链文件 (`cmake/gcc-arm-none-eabi.cmake`)

- 交叉编译器：`arm-none-eabi-gcc`
- CPU 参数：`-mcpu=cortex-m3`
- 链接脚本：`STM32F103XX_FLASH.ld`
- 链接选项：`--specs=nano.specs`（newlib-nano），`-Wl,--gc-sections`（无用段裁剪）
- 自动打印内存使用摘要（`--print-memory-usage`）

### Clangd 配置 (`.clangd`)

- 从 `build/Debug/compile_commands.json` 读取编译命令
- 强制指定目标为 `arm-none-eabi -mcpu=cortex-m3`（防止按 x86_64 解析 ARM 头文件）
- 预定义宏：`STM32F103xE`, `USE_HAL_DRIVER`, `DEBUG`, `__NVIC_PRIO_BITS=4U`, `__MPU_PRESENT=0`, `__Vendor_SysTickConfig=0`
- 移除 clangd 不兼容的 GCC 编译选项（`-fdata-sections`, `-fstack-usage` 等）
- 关闭 `unused-includes` 误报（HAL 头文件副作用包含导致）

### VS Code 设置 (`.vscode/settings.json`)

- 禁用 Microsoft C/C++ IntelliSense（由 clangd 完全替代）
- `clangd.arguments` 指定编译命令目录
- `cmake.parallelJobs: 0` 自动使用全部 CPU 核心编译
- `VsCodeTaskButtons.tasks` 在状态栏显示 **HEX/BIN 生成** 和 **PyOCD 烧录** 快捷按钮

### 调试配置 (`launch.json`)

- **servertype**：`pyocd`
- **device** / **targetId**：`stm32f103ze`（注意全小写）
- **interface**：`swd`
- **svdFile**：`Drivers/svd/STM32F103xx.svd`（调试时可查看外设寄存器）
- **serverArgs**：`--connect under-reset`（在复位状态下连接目标）
- **overrideGDBServerStartedRegex**：`"GDB server listening on port"`（匹配 PyOCD GDB Server 就绪信号，防止连接超时）
- **runToEntryPoint**：`main`（自动运行到 main 函数）

### FreeRTOS 调试（未来扩展）

在 `launch.json` 中添加 `"rtos": "FreeRTOS"` 即可启用 RTOS 感知调试：
- CALL STACK 面板显示所有任务
- 可切换不同任务的调用栈和局部变量

### 数据断点（Data Watchpoint）

Cortex-M3 提供 4 个 DWT 硬件比较器，支持：
- **读取断点**：CPU 读取该变量时触发
- **写入断点**：CPU 写入该变量时触发
- **访问断点**：读取或写入均触发（节省比较器）

在 VARIABLES 面板右键变量即可添加，也可在 BREAKPOINTS 面板直接用 GDB 表达式添加。

---

## 常见问题

| 问题 | 解决方案 |
|------|----------|
| `clangd: unknown type name 'HAL_StatusTypeDef'` | `.clangd` 中确保 `-mcpu=cortex-m3` 未被移除；确保 `-DSTM32F103xE` 和 `-DUSE_HAL_DRIVER` 在 Add 列表中 |
| PyOCD Flash 报错 `--hw-reset` 无效 | 应使用 `-O reset_type=hw`（session option 语法） |
| Cortex-Debug 启动超时 | 检查 `overrideGDBServerStartedRegex` 是否正确匹配；确认 `serverArgs` 中 `--target` 已指定 |
| 调试器无法连接 | 运行 `pyocd list` 确认探针被识别；检查杜邦线连接 |
| 编译时找不到 `arm-none-eabi-gcc` | 确认 ARM GCC 已加入 PATH，重启终端/VS Code |

---

## 技术参考

| 项目 | 说明 |
|------|------|
| **MCU** | STM32F103ZET6, Cortex-M3, 72 MHz |
| **Flash** | 512 KB @ `0x08000000` |
| **RAM** | 64 KB @ `0x20000000` |
| **系统时钟** | HSE 8 MHz → PLL ×9 → SYSCLK 72 MHz |
| **APB1** | 36 MHz (max) |
| **APB2** | 72 MHz |
| **HAL 库** | STM32F1xx HAL Driver（仅启用 GPIO/RCC/Cortex/FLASH/PWR/DMA/EXTI） |
| **C 标准** | C11 |
| **C 运行时** | newlib-nano (`--specs=nano.specs`) |
| **栈大小** | `0x400` (1 KB) |
| **堆大小** | `0x200` (512 B) |
