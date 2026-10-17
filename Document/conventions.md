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
  | `ffconf.h`、`lv_conf.h`、`_WIZCHIP_=` 编译定义 | 第三方库自己的开关 | `Middleware/` |
  | `diskio.c`、`w5500_port.c`、`lvgl_port.c` | 我们对着本工程硬件写的 | `Services/devices/` |

> **现状**：ioLibrary / FatFs / LVGL 三者的移植层均已迁到 `Services/devices/`，
> `Middleware/` 下只剩上游源码与配置。（FreeRTOS 的 `Middleware/FreeRTOS/port/`
> 是上游自带的可移植层，与这里说的移植胶水不是一回事，保持原位。）

- **已知例外：LVGL 目标带一条 `BSP/Inc` 头文件路径**（[lvgl-8.3.11/CMakeLists.txt](../Middleware/lvgl-8.3.11/CMakeLists.txt)）。
  它是**承重的，不要删**：`lv_conf.h` 里 `LV_MEM_POOL_INCLUDE "bsp_sram.h"` +
  `LV_MEM_POOL_ALLOC → BSP_SRAM_LVGL_BASE`，LVGL 的 `lv_mem.c` 真的会 include 它
  （内存池放外部 SRAM，地址常量由 BSP 定义）。

  这条豁免的边界要认清：**只是 include 路径，不是 `target_link_libraries(lvgl bsp)`** ——
  CMake 目标图上 Middleware 与 BSP 仍然平级。"配置留 Middleware" 这条约定必然
  导致配置里引用跨层常量，这属于可接受的副作用，但**不要再扩大**：新增的适配代码
  一律进 `Services/devices/`。

### 总线锁的粒度

**锁必须覆盖「不变量」，而不是覆盖「线」。** 这是给外设上锁时最容易搞错的一点。

`Services/bus` 提供的锁只保证**线级**语义：两帧不会在 MOSI 上交错。这对
**不跨帧保存状态**的器件就够了（W5500：每个寄存器事务语义自包含，socket 状态
由芯片内部按 socket 号维护），所以 W5500 的锁可以加在 CS 回调里
（[w5500_port.c](../Services/devices/w5500_port.c) 的 `port_cs_select`）——
ioLibrary 本来就用 select…deselect 包住每个事务，加在那里**免费获得整帧粒度**。

**但这条不能推广。** 器件一旦有跨帧不变量，CS 边界就不再是事务边界：

| 器件 | 跨帧状态 | CS 边界 = 事务边界？ | 锁加在哪 |
|------|---------|-------------------|---------|
| W5500 | 无（寄存器自包含） | ✅ | CS 回调 |
| W25Q64 | **有（WEL 位）** | ❌ | **驱动的操作级** |

W25Q64 一次"写一页" = `write_enable`(CS 周期①) → `page_program`(CS 周期②)
→ `wait_busy`(CS 周期③×N)，**三个以上 CS 周期**。若把锁加在 CS 边界上，
任务 A 置完 WEL 就放锁，任务 B 插进来做一次擦除（擦除完成会清 WEL），
任务 A 的 Page Program 就会被芯片**静默丢弃** —— 状态寄存器读回一切正常。
[bsp_spi_flash.c](../BSP/Src/bsp_spi_flash.c) 当前**没有**用 `SpiBus`，接入时必须
按此规则加在操作级。

> ⚠️ **真正的禁令是「同一个驱动内不得嵌套」，不是「一条总线只能有一种粒度」。**
> 两种粒度共用同一把**非递归**互斥量，谁持锁谁独占总线，彼此之间没有嵌套：
>
> ```
> A (CS 回调加锁):   [take L] 帧 [release L]
> B (操作级加锁):    [take L] 帧①(裸) 帧②(裸) 帧③(裸) [release L]
> ```
>
> B 路径里的帧是**裸的**（不再经过 CS 级锁），所以 A 与 B 可以共存于一条总线 ——
> 说"一条总线上的设备只能二选一"就说过头了。要禁止的是**嵌套**：同一个驱动里
> 操作级路径又走到 CS 级锁，才是同一个互斥量的重复获取。

嵌套的后果也要写准：FreeRTOS 非递归互斥量上自己取自己**不是立即死锁**，而是
阻塞到超时后返回 -1（在本工程里触发 [bsp_assert](../BSP/Src/bsp_assert.c) 通道），
只有传 `SPI_BUS_WAIT_FOREVER` 才是真死锁。所以给操作级路径选超时值时要意识到：
它决定症状是「当场断言一句」还是「系统看着还活着但功能全无」。

**W25Q64 的锁由谁加**：BSP 不能加 —— `BSP/` 不许向上依赖 `Services/bus`。
所以 flash 接入总线锁的前提是先按 W5500 的形状拆一次（**留到 W4，W2 不动**）：

```
BSP/Src/bsp_w25q64_hw.c      CS / GPIO / SPI1 初始化        (板级, 无锁无 RTOS)
Services/devices/w25q64.c    指令协议 + 操作级锁             (依赖 bsp_w25q64_hw / bsp_spi / spi_bus)
```

操作级加锁一律用「包壳」模式，让提前 return 漏解锁在结构上不可能：

```c
static Status_t page_write_nolock(...);          /* 真正的逻辑, 一行锁都没有 */
Status_t W25Q64_Write(...)
{
    if (SpiBus_Lock(BSP_SPI_BUS_1, W25Q64_LOCK_TIMEOUT_MS) != 0) return W25Q64_ERR_BUSY;
    Status_t st = page_write_nolock(...);        /* WEL → PP → wait_busy 全在这段里 */
    SpiBus_Unlock(BSP_SPI_BUS_1);
    return st;
}
```

持锁时长的量级（**决定 timeout 参数，要写进驱动注释**）：页写 ~1ms / 4KB 扇区擦除
~50ms / 64KB 块擦除 ~150ms / 整片擦除 ~20s（典型值，最大值查手册）。

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
