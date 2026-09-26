# CLAUDE.md — HAL_TEST_Board STM32F103ZET6

> 野火霸道 V2 (YHBDv2) — STM32F103ZET6, Cortex-M3, 512KB Flash, 64KB RAM.
> 工具链: `arm-none-eabi-gcc` 14.2 | CMake 3.22+ + Ninja | C11 (gnu11) | DAP-Link SWD.

## Quick Build

```bash
cmake --build build/Debug                          # 增量编译 (默认)
cmake --build build/Release                        # Release (需先配置, 见下)

cmake -B build/<Debug|Release> -G "Ninja" -DCMAKE_BUILD_TYPE=<Debug|Release> \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake   # 首次配置
```

> Debug / Release 是**两套独立**的 build 目录，差异见 [CMakeLists.txt](CMakeLists.txt) 里的构建类型分支。
> VSCode 快捷键：`Ctrl+Shift+B` 选 BUILD / BUILD (Release)；烧录用 `PYOCD Flash` / `PYOCD Flash (Release)`。

---

## Conventions (必须遵守)

### 分层规则

依赖只允许向下，不允许成环。完整规范（CMake 目标依赖、移植层归属判据）见
[conventions.md](Document/conventions.md#分层规则-layering-rule)。

| 层 | 放什么 | 不许放什么 |
| --- | --- | --- |
| `Application/` | 业务逻辑、RTOS 任务、移植钩子 | 直接调 HAL |
| `Services/` | `log/` 分级日志；`bus/` 总线服务（锁/事务边界）；`devices/` 器件协议驱动 | 业务逻辑 |
| `Middleware/` | 第三方库（FreeRTOS / FatFs / LVGL / ioLibrary）—— **只读** | 项目自己的代码 |
| `BSP/` | 板级硬件资源、raw 传输 | RTOS API、第三方库 |

- **BSP 不得依赖 RTOS**；需要锁的场景由 `Services/bus` 提供
- **Services 可用 FreeRTOS，但只允许出现在 `.c` 里**（头文件不得含 RTOS 类型）。**不做 OSAL 包装**
- **第三方库的移植层放 `Services/devices/`**，不放 Middleware

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

### 日志与串口

- 正式日志走 `Services/log/log.h` 的 `LOG_E/W/I/D` 宏；`printf` 仅用于临时排查，提交前应改回 `LOG_x`
- **两条输出通道，不要试图合并**：`BSP/bsp_assert`（零依赖 panic 输出）与 `Services/log`（分级日志）。
  **BSP 层用不了 `LOG_x`，这是设计而非缺陷**
- **串口归属** —— 每路串口只有一个使用者，应用层不得绕过 BSP 模块直接调 `bsp_usart`：
  USART1=日志 / USART2=`bsp_rs485` / USART3=`bsp_esp8266` / USART4-5 空闲
- **级别过滤**在预处理期完成。⚠️ Release 用 `LOG_LEVEL_WARN`（宏），**不是** `LOG_LVL_WARN`（enum 常量）
  —— 后者在预处理期会被替换成 0，导致**全部日志消失**

完整规范（printf 定位、`%f` 链接耦合、TX 环形缓冲语义、外设测试模块状态、输出格式）见
[conventions.md](Document/conventions.md#日志与串口规范)。

### Git Commit

格式 `type(scope): 简述`。type: `feat`/`fix`/`chore`/`refactor`/`docs`。中文叙述，模块/API 名保留原文。

**message 必须精简（3~10 行正文）**：只写为了什么、干了什么、用了什么架构。不写排查过程与长篇"为什么"。
第三方库 vendor 单独成提交。详见 [conventions.md](Document/conventions.md#git-commit-convention)。

### 添加新模块

- **BSP**: `.h`→`BSP/Inc/`, `.c`→`BSP/Src/`, 源文件→`BSP/CMakeLists.txt`
- **Services**: 放 `Services/<log|bus|devices>/`（头文件与源文件同目录），源文件→`Services/CMakeLists.txt`
- **Test**: `.c`→`Application/Test/`, 声明→`test.h`, 源文件→`Application/CMakeLists.txt`
- **HAL**: (1) `hal_conf.h` 添加 `#define HAL_xxx_MODULE_ENABLED` + (2) `cmake/stm32cubemx/CMakeLists.txt` 取消注释源文件

> 完整规范见 [Document/conventions.md](Document/conventions.md)

---

## Available Skills

| 技能              | 用途                                                  |
| ----------------- | ----------------------------------------------------- |
| `/build-stm32`  | CMake + Ninja 编译 (Debug 默认 / Release / 增量 / 首次 / 完全重建 / 生成HEX) |
| `/flash-stm32`  | PyOCD 烧录固件 (`release` = 烧 Release 版, `safe` = 完整预检) |
| `/reset-stm32`  | 硬件复位 + 串口启动日志捕获 (端口自动识别 CH340, 不写死 COM 号) |
| `/serial-debug` | 实时串口日志监控窗口 (Git Bash tail -f)               |

> 完整 API 参考见 [Document/skills-reference.md](Document/skills-reference.md)

---

## Current Firmware Behavior

`main()` 初始化 BSP 驱动 → `LOG_Init()` 启动日志 → 初始化外部 SRAM → W5500 通路自检 + 静态网络参数 → 启动 FreeRTOS 调度器。

当前跑三个任务：`vLedTask`（心跳）、`vDispTask`（LVGL 显示 + 触摸校准）、`vNetTask`（网络任务，当前 1Hz 轮询 PHYCFGR，W2 起接管 socket）。

---

## Reference Documents

| 文档                                               | 内容                                                                                                                     |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| [project-reference.md](Document/project-reference.md) | 架构图、Key Files 清单、Pin Map、Clock Tree、HAL 模块表、Memory Layout、Build Flags、目录规划、Firmware Size、Debug 配置 |
| [conventions.md](Document/conventions.md)             | 完整编码规范 (分层/宏/API/日志与串口/Git 提交/初始化)                                                                                    |
| [skills-reference.md](Document/skills-reference.md)   | 技能详细 API 参考                                                                                                        |
| [issue-log.md](Document/issue-log.md)                 | 疑难问题记录 —— 已定位根因的问题: 现象/根因/修复/排查方法教训 (含"已排除"清单, 避免重走)                                    |
