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

Application 代码 **禁止** 直接 `#include` HAL 外设头文件。通过 `test.h` 获取测试函数，通过 BSP 头文件访问硬件。

**四层结构** —— 依赖只允许向下，不允许成环：

| 层 | 放什么 | 不许放什么 |
| --- | --- | --- |
| `Application/` | 业务逻辑、RTOS 任务、移植钩子 | 直接调 HAL |
| `Services/` | `log/` 分级日志；`bus/` 总线服务（锁/事务边界）；`devices/` 器件协议驱动 | 业务逻辑 |
| `Middleware/` | 第三方库（FreeRTOS / FatFs / LVGL / ioLibrary）—— **只读** | 项目自己的代码 |
| `BSP/` | 板级硬件资源、raw 传输 | RTOS API、第三方库 |

三条硬性约定：

- **BSP 不得依赖 RTOS**（逆向依赖）。需要锁的场景（如 SPI 总线互斥）由 `Services/bus` 提供，BSP 只管"怎么发一个字节"
- **Services 可以直接用 FreeRTOS，但只允许出现在 `.c` 里** —— 头文件不得出现 FreeRTOS 类型，避免向上下两个方向泄漏。**不做 OSAL 包装**：换 RTOS 时领域和需求都会大变，抽象层保不住，与其维护一层改名转发，不如把模块本身做通用
- **第三方库的移植层（回调实现）放 `Services/devices/`**，不放 Middleware —— 否则 Middleware 要反向依赖 BSP。ioLibrary 的 `reg_wizchip_*_cbfunc` 就是这么接的（见 [w5500_port.c](Services/devices/w5500_port.c)）

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

### 日志与串口 (2026-09 起)

正式日志走 `Services/log/log.h` 的 `LOG_E/W/I/D` 宏；`printf` **保留**，但只用于临时排查。

> 放 Services 而非 Application：器件驱动也要打日志。理由与写法见 [conventions.md](Document/conventions.md)。

- **用法**：每个 `.c` 顶部 `#define LOG_TAG "XXX"` 再 `#include "log.h"`，之后直接 `LOG_I("x=%d", x)`
- **printf 的定位**：可以随手写，但提交前应改回 `LOG_x` 或删除。理由不是"禁止"，而是 LOG 有分级、毫秒时间戳、模块标签、丢弃计数，printf 一样都没有；且 printf 在 ISR 里会长时间占用中断上下文
- `printf` 与 LOG **共用同一路串口和同一个 TX 环形缓冲** —— `__io_putchar` 在 `log.c`（不在 BSP），端口由 `LOG_Init()` 决定，所以 printf 也是非阻塞的
- **支持 `%f`**（Debug 构建链接 `-Wl,-u,_printf_float`，约 11KB Flash）。`-Wl,-u,_scanf_float` 已移除，工程不用 scanf 浮点
- **耦合**：`-u _printf_float` 会拉入 `_vfprintf_r → _write_r → _write` 链路。想彻底禁用 printf 省那 11KB，必须把该 flag、`_write`（[syscalls.c](Start/syscalls.c)）、`__io_putchar`（[log.c](Services/log/log.c)）**三者一起删**；只删一个会链接失败
- 级别过滤在**预处理期**完成（已接进 [CMakeLists.txt](CMakeLists.txt) 的 Release 分支）。Release 加 `-DLOG_LEVEL=LOG_LEVEL_WARN`，低级别语句整条编掉，连格式串都不进 Flash
  - ⚠️ 是 `LOG_LEVEL_WARN`（宏），**不是** `LOG_LVL_WARN`（enum 常量）。`#if` 在预处理期求值，enum 常量那时不存在，会被替换成 0 —— 写成 enum 名会让**全部日志消失**（有 `#warning` 兜底）
- 前缀格式：`[   13180] W DISP: message`（时间戳 / 级别 / 标签）。启动阶段时间戳来自 DWT 周期计数器（`HAL_GetTick()` 在调度器启动前恒为 0）

**两条输出通道，不要试图合并**：`BSP/bsp_assert`（零依赖 panic 输出）与 `Services/log`（分级日志）。
**BSP 层用不了 `LOG_x`，这是设计而非缺陷。**

**串口归属** —— 每路串口只有一个使用者，应用层不得绕过 BSP 模块直接调 `bsp_usart`：

| 串口 | 使用者 | TX 环形缓冲 |
| --- | --- | --- |
| USART1 | LOG 模块（日志口） | 512 B（[main.c](Application/main.c) 提供） |
| USART2 | `bsp_rs485` | 256 B（模块内提供） |
| USART3 | `bsp_esp8266` | 1 KB（模块内提供） |
| USART4/5 | 空闲 | — |

- **TX 环形缓冲由使用者提供**，`BSP_USART_InitEx()` 的 `tx_buf` 必填，大小须为 2 的幂
- **返回值语义**：`BSP_USART_Write()` 返回*实际入队字节数*，部分入队是正常情况。不需要知道缓冲区多大 —— 用 `TxSpace()` 查询、余下部分续传即可
- **`tx_cplt_callback` 的语义是「环形彻底排空」**，不是「一段发完」（DMA 分段续传时中间段完成不触发）。RS485 的 DE 方向控制依赖这一点

**外设验证测试已移出构建**（`Application/Test/*.c`，源文件保留在磁盘）。主要原因是 `test_esp8266` / `test_rs485` / `test_dma_echo` 把 USART1 当原始透传口用（改波特率、重新初始化），与"USART1 = 日志口"直接冲突；它们也全都依赖 printf。需要时改造成 LOG 输出、并挪到空闲的 USART4/5，再取消 [CMakeLists.txt](Application/CMakeLists.txt) 中的注释。

注：这些测试本来就已被 `--gc-sections` 丢弃（`task_init.c` 里 test 任务早已屏蔽），所以移出构建对 Flash 占用没有影响。

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
| `/build-stm32`  | CMake + Ninja 编译 (Debug 默认 / Release / 增量 / 首次 / 完全重建 / 生成HEX) |
| `/flash-stm32`  | PyOCD 烧录固件 (`release` = 烧 Release 版, `safe` = 完整预检) |
| `/reset-stm32`  | 硬件复位 + 串口启动日志捕获 (端口自动识别 CH340, 不写死 COM 号) |
| `/serial-debug` | 实时串口日志监控窗口 (Git Bash tail -f)               |

> 完整 API 参考见 [Document/skills-reference.md](Document/skills-reference.md)

---

## Current Firmware Behavior

`main()` 初始化 BSP 驱动 → `LOG_Init()` 启动日志 → 初始化外部 SRAM → 启动 FreeRTOS 调度器。

当前跑三个任务：`vLedTask`（心跳）、`vDispTask`（LVGL 显示 + 触摸校准）、`vW5500Task`（W5500 链路监控，1Hz 轮询 PHYCFGR）。外设验证测试已移出构建，见下方"日志与串口"。

---

## Reference Documents

| 文档                                               | 内容                                                                                                                     |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| [project-reference.md](Document/project-reference.md) | 架构图、Key Files 清单、Pin Map、Clock Tree、HAL 模块表、Memory Layout、Build Flags、目录规划、Firmware Size、Debug 配置 |
| [conventions.md](Document/conventions.md)             | 完整编码规范 (宏/API/分层/初始化/Git)                                                                                    |
| [skills-reference.md](Document/skills-reference.md)   | 技能详细 API 参考                                                                                                        |
| [issue-log.md](Document/issue-log.md)                 | 疑难问题记录 —— 已定位根因的问题: 现象/根因/修复/排查方法教训 (含"已排除"清单, 避免重走)                                    |
