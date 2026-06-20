# CLAUDE.md — HAL_TEST_Board STM32F103ZET6

> 野火霸道 V2 (YHBDv2) — STM32F103ZET6, Cortex-M3, 512KB Flash, 64KB RAM.
> 工具链: `arm-none-eabi-gcc` 14.2 | CMake 3.22+ + Ninja | C11 (gnu11) | DAP-Link SWD.

## Quick Build

```bash
cmake --build build/Debug                          # 增量编译 (默认)
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake   # 首次配置
```

---

## Conventions (必须遵守)

### 分层规则

Application 代码 **禁止** 直接 `#include` HAL 外设头文件。通过 `test.h` 获取测试函数，通过 BSP 头文件访问硬件。

### 宏使用规范

- 错误码/状态码: ❌ `#define` → ✅ `typedef enum`
- 模块内部常量: ❌ `#define` → ✅ `static const` (在 .c)
- 暴露给应用层的配置常量: ❌ `#define` → ✅ 匿名 `enum`
- 条件编译、头文件保护、寄存器位操作、核心功能配置选项开关: ✅ 允许

### API 设计规范 (多外设总线)

- 枚举 ID 代替 `void*` 句柄: `typedef enum { BSP_SPI_BUS_1, BSP_SPI_BUS_COUNT } BSP_SPI_Bus_Id_t;`
- `static const` 硬件映射表 + `static` 上下文数组 (.c 内部, 不对外暴露)
- API 传枚举 ID 而非指针: `void BSP_SPI_Init(BSP_SPI_Bus_Id_t id);`
- 禁止 `extern` 全局总线实例

### Git Commit

格式: `@ type: 简述 — 补充说明`。type: `feat`/`fix`/`chore`/`refactor`/`docs`。中文叙述, 模块/API 名保留原文。

### 添加新模块

- **BSP**: `.h`→`BSP/Inc/`, `.c`→`BSP/Src/`, 源文件→`BSP/CMakeLists.txt`
- **Test**: `.c`→`Application/Test/`, 声明→`test.h`, 源文件→`Application/CMakeLists.txt`
- **HAL**: (1) `hal_conf.h` 添加 `#define HAL_xxx_MODULE_ENABLED` + (2) `cmake/stm32cubemx/CMakeLists.txt` 取消注释源文件

> 完整规范见 [Document/conventions.md](Document/conventions.md)

---

## Available Skills

| 技能              | 用途                                                  |
| ----------------- | ----------------------------------------------------- |
| `/build-stm32`  | CMake + Ninja 编译 (增量/首次/完全重建/生成HEX)       |
| `/flash-stm32`  | PyOCD 烧录固件 (`/flash-stm32 safe` = 完整预检模式) |
| `/reset-stm32`  | 硬件复位 + COM6 串口启动日志捕获                      |
| `/serial-debug` | 实时串口日志监控窗口 (Git Bash tail -f)               |

> 完整 API 参考见 [Document/skills-reference.md](Document/skills-reference.md)

---

## Current Firmware Behavior

`main()` 初始化所有 BSP 驱动，printf (USART1) 输出启动 Banner，调用一个 test 函数，当前处于测试阶段，每次测试只涉及少量外设。

---

## Reference Documents

| 文档                                               | 内容                                                                                                                     |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| [project-reference.md](Document/project-reference.md) | 架构图、Key Files 清单、Pin Map、Clock Tree、HAL 模块表、Memory Layout、Build Flags、目录规划、Firmware Size、Debug 配置 |
| [conventions.md](Document/conventions.md)             | 完整编码规范 (宏/API/分层/初始化/Git)                                                                                    |
| [skills-reference.md](Document/skills-reference.md)   | 技能详细 API 参考                                                                                                        |
