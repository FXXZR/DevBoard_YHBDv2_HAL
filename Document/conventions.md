# Code Conventions — HAL_TEST_Board 项目编码规范

> 代码规范、提交规范、API 设计约定。AI 在与本项目的每次对话中必须遵守。

---

## Language & Formatting

- **C standard**: C11 with GNU extensions
- **Formatting**: `.clang-format` — LLVM-based, 4-space indent, 120 columns
- **Naming**: `BSP_` prefix for board layer, `HAL_` for ST HAL
- **Error handling**: `Error_Handler()` — disables IRQs, infinite loop
- **ISRs**: All weak-aliased to `Default_Handler`; override by defining the handler

## Git Commit Convention

格式 `type(scope): 简述`。type 使用 conventional commits 分类：`feat`/`fix`/`chore`/`refactor`/`docs`。
叙述用中文，模块/函数/API 名保留原文。

**message 必须精简。** 正文控制在 3~10 行，只写两件事：

1. 为了什么、干了什么
2. 用了什么架构 / 关键取舍（一句话级别）

**不要写**：逐条的问题排查过程、讨论经过、大段"为什么"。这些属于代码注释和 `Document/`，不属于提交信息。

```
refactor(services): 新增 Services 服务层, W5500 按新架构接入

Services 位于 BSP 之上、Application 之下, 补上"硬件抽象之上、业务逻辑之下"这层:
它可向下调 BSP, 也可在 .c 里用 FreeRTOS。

    Application/ → Services/ → {Middleware, BSP} → HAL

    Services/bus/        SPI 总线互斥锁 —— 锁的边界 = CS 的边界
    Services/devices/    W5500 上电/PHY 配置 + ioLibrary 移植回调

FLASH 306012 → 315608 B。实测 VERSIONR=0x04, PHY 链路 100M 全双工。
```

**第三方库 vendor 单独成提交**，不与自己写的代码混在一起 —— 便于将来对照上游做覆盖式升级：
`git diff <vendor提交> <新版本> -- Middleware/<lib>` 出来的应只有上游改动。

## 模块添加规范

| 操作 | 步骤 |
|------|------|
| **New BSP module** | Add `.h` to `BSP/Inc/`, `.c` to `BSP/Src/`, source to `BSP/CMakeLists.txt` |
| **New test module** | Add `.c` to `Application/Test/`, declare in `test.h`, add source to `Application/CMakeLists.txt` |
| **New HAL module** | Two-step: (1) `hal_conf.h` macro + (2) `cmake/stm32cubemx/CMakeLists.txt` source uncomment |

## 分层规则 (Layering Rule)

依赖只允许向下，不允许成环。

| 层 | 放什么 | 不许放什么 |
|------|--------|-----------|
| `Application/` | 业务逻辑、RTOS 任务、移植钩子 | 直接调 HAL |
| `Services/` | `log/` 分级日志；`bus/` 总线服务（锁/事务边界）；`devices/` 器件协议驱动 | 业务逻辑 |
| `Middleware/` | 第三方库（FreeRTOS / FatFs / LVGL / ioLibrary）—— **只读** | 项目自己的代码 |
| `BSP/` | 板级硬件资源、raw 传输 | RTOS API、第三方库 |

依赖图：

```
Application/  →  Services/  →  { Middleware/, BSP/ }  →  Drivers/HAL
```

`Middleware/` 与 `BSP/` 是平级，互不依赖。

### 硬性要求

- **Application 不得直接 `#include` HAL 外设头文件**。测试函数走 `test.h`，硬件访问走 BSP 头文件。`stm32f1xx_hal.h` 仅允许用于 `HAL_GetTick()` 这类基础接口
- **BSP 不得依赖 RTOS**。需要锁的场景由 `Services/bus` 提供；BSP 只负责"怎么发一个字节"
- **Services 可以直接用 FreeRTOS，但只允许出现在 `.c` 里** —— 头文件不得出现 FreeRTOS 类型，避免向上下两个方向泄漏
- **不做 OSAL 包装**。换 RTOS 时领域与需求都会变，抽象层保不住；把模块本身做通用即可
- **第三方库的移植层（回调实现）放 `Services/devices/`**，不放 Middleware —— 否则 Middleware 要反向依赖 BSP。参见 ioLibrary 的 `reg_wizchip_*_cbfunc`（[w5500_port.c](../Services/devices/w5500_port.c)）
- **配置留 Middleware，我们写的适配代码搬 `Services/`**。判据是"这行内容的作者是谁"：

  | 内容 | 作者 | 归属 |
  |------|------|------|
  | `ffconf.h`、`_WIZCHIP_=` 编译定义 | 第三方库自己的开关 | `Middleware/` |
  | `diskio.c`、`w5500_port.c` | 我们对着本工程硬件写的 | `Services/devices/` |

> **现状**：ioLibrary 已合规。FatFs 的 `Middleware/FatFs/port/diskio.c` 与
> LVGL 的 `Middleware/lvgl-8.3.11/port/lvgl_port.c` 尚未迁移，待整理。

## 宏使用规范 (Macro Usage Rules)

严格遵守 C11 现代实践，减少预处理器滥用：

| 场景 | 允许/禁止 | 替代方案 |
|------|-----------|----------|
| 条件编译 (`#if`/`#ifdef`) / 功能开关 | ✅ 允许 | — |
| 头文件保护 (`#ifndef` guard) | ✅ 允许 | — |
| 错误码 / 状态码 | ❌ 禁止 `#define` | `typedef enum` |
| 可选项 / 配置常量 (暴露给应用层) | ❌ 禁止 `#define` | `typedef enum` 或头文件中匿名 `enum` |
| 模块内部常量 (不对外暴露) | ❌ 禁止 `#define` | `.c` 文件中 `static const` |
| 寄存器位操作 / 硬件地址 | ✅ 允许 (HAL/Start 层) | — |
| 函数别名 / API 适配宏 | ⚠️ 最小化 | 优先用 `static inline` 函数 |

**对比示例:**
```c
// ❌ 禁止
#define BSP_I2C_OK      0U
#define BSP_I2C_ERROR   1U
#define BSP_EEPROM_PAGE_SIZE  8U

// ✅ 正确
typedef enum { BSP_I2C_OK = 0, BSP_I2C_ERROR, BSP_I2C_TIMEOUT } BSP_I2C_Status_t;
// 页大小等常量如仅在 .c 使用 → static const uint8_t kPageSize = 8;
// 如头文件需要暴露 → enum { BSP_EEPROM_PAGE_SIZE = 8U };
```

## API 设计规范 (API Design Rules)

对于同类型多数量的外设，遵循 `bsp_usart` 模式，禁止 `void*` 句柄和 `extern` 全局总线实例：

| 规范 | 说明 |
|------|------|
| **枚举 ID 代替指针句柄** | `typedef enum { BSP_SPI_BUS_1, BSP_SPI_BUS_COUNT } BSP_SPI_Bus_Id_t;` |
| **`static const` 硬件映射表** | `.c` 文件内部 `static const HW_Map_t hw_map[] = { [BUS_1] = {...} };` |
| **`static` 上下文数组** | `.c` 文件内部 `static Ctx_t ctx[BSP_BUS_COUNT];`，外部不可见 |
| **API 传枚举 ID** | `void BSP_SPI_Init(BSP_SPI_Bus_Id_t id);` 而非 `void BSP_SPI_Init(BSP_SPI_Bus_t *bus);` |
| **禁止 `extern` 全局句柄** | 不暴露 `extern BSP_SPI_Bus_t g_spi_bus_1;` |

## 日志与串口规范

### 两条输出通道

| | 位置 | 依赖 | 用途 |
|------|------|------|------|
| panic 通道 | `BSP/bsp_assert` | **零** | 断言失败时的最后一句话 |
| 分级日志 | `Services/log` | bsp_usart + FreeRTOS + vsnprintf | 日常诊断 |

**BSP 层禁止使用 `LOG_x`。** 断言路径要能在 DMA 损坏、中断关闭、堆被打烂时依然输出，
因此不使用缓冲、不使用锁，直接轮询寄存器；代价是没有分级与格式化。两条通道不合并。

### 用法

每个 `.c` 顶部 `#define LOG_TAG "XXX"` 再 `#include "log.h"`，之后直接 `LOG_I("x=%d", x)`。
输出格式 `[   13180] W DISP: message`（时间戳 / 级别 / 标签）。
启动阶段时间戳来自 DWT 周期计数器（`HAL_GetTick()` 在调度器启动前恒为 0）。

### printf 的定位

`printf` 保留，但**只用于临时排查**，提交前应改回 `LOG_x` 或删除。LOG 有分级、毫秒时间戳、
模块标签、丢弃计数，printf 都没有；且 printf 在 ISR 里会长时间占用中断上下文。

`printf` 与 LOG 共用同一路串口和同一个 TX 环形缓冲 —— `__io_putchar` 在
[Services/log/log.c](../Services/log/log.c)（不在 BSP），端口由 `LOG_Init()` 决定，
因此 printf 也是非阻塞的。

### 级别过滤

过滤在**预处理期**完成（已在根 `CMakeLists.txt` 的 Release 分支接好）。
Release 加 `-DLOG_LEVEL=LOG_LEVEL_WARN`，低级别语句整条编掉，连格式串都不进 Flash。

⚠️ 必须写 `LOG_LEVEL_WARN`（宏），**不是** `LOG_LVL_WARN`（enum 常量）。
`#if` 在预处理期求值，enum 常量那时不存在，会被替换成 0 —— 写成 enum 名会让**全部日志消失**
（有 `#warning` 兜底）。

### 浮点

Debug 构建链接 `-Wl,-u,_printf_float`（约 11KB Flash）以支持 `%f`。`-Wl,-u,_scanf_float` 已移除。

该 flag 会拉入 `_vfprintf_r → _write_r → _write` 链路。想彻底禁用 printf 省那 11KB，
必须把该 flag、`_write`（[syscalls.c](../Start/syscalls.c)）、`__io_putchar`
（[log.c](../Services/log/log.c)）**三者一起删**；只删一个会链接失败。

### 串口归属

每路串口只有一个使用者，应用层不得绕过 BSP 模块直接调 `bsp_usart`：

| 串口 | 使用者 | TX 环形缓冲 |
| --- | --- | --- |
| USART1 | LOG 模块（日志口） | 512 B（[main.c](../Application/main.c) 提供） |
| USART2 | `bsp_rs485` | 256 B（模块内提供） |
| USART3 | `bsp_esp8266` | 1 KB（模块内提供） |
| USART4/5 | 空闲 | — |

- **TX 环形缓冲由使用者提供**，`BSP_USART_InitEx()` 的 `tx_buf` 必填，大小须为 2 的幂
- **`BSP_USART_Write()` 返回实际入队字节数**，部分入队是正常情况。不需要知道缓冲区多大 —— 用 `TxSpace()` 查询、余下部分续传即可
- **`tx_cplt_callback` 的语义是「环形彻底排空」**，不是「一段发完」（DMA 分段续传时中间段完成不触发）。RS485 的 DE 方向控制依赖这一点

### 外设验证测试

`Application/Test/*.c` 照常编译，但 `task_init.c` 里 test 任务项是注释掉的，因此
`--gc-sections` 会把它们连同引用链一起丢弃，不占 Flash。保留在构建内的唯一代价是编译时间。

重新启用前需先处理：`test_esp8266` / `test_rs485` / `test_dma_echo` 把 USART1 当原始透传口用
（改波特率、重新初始化），与"USART1 = 日志口"冲突，须改造成 LOG 输出并挪到空闲的 USART4/5。

## HAL 结构体初始化规范

BSP 层初始化 HAL 结构体时，必须使用 **designated initializers** (指定初始化器)，禁止位置初始化。

原因: HAL 结构体字段顺序不符合直觉 —— 例如 `RTC_DateTypeDef` 是 `{WeekDay, Month, Date, Year}` 而非 `{Year, Month, Date}`。

```c
// ❌ 禁止: 位置初始化
RTC_DateTypeDef date = {0, 6, 21, 2026};

// ✅ 正确: 指定初始化器
RTC_DateTypeDef date = {.WeekDay = 0, .Month = 6, .Date = 21, .Year = 2026};
```
