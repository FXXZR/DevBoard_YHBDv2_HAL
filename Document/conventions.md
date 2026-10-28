# Code Conventions — HAL_TEST_Board 项目编码规范

> 代码规范、提交规范、API 设计约定。AI 在与本项目的每次对话中必须遵守。

---

## Language & Formatting

- **C standard**: C11 with GNU extensions
- **Formatting**: `.clang-format` — LLVM-based, 4-space indent, 120 columns
- **Naming**: `BSP_` prefix for board layer, `HAL_` for ST HAL。变量与常量 `snake_case`，
  类型 `CamelCase` + `_t`，宏与枚举常量 `UPPER_SNAKE`。
  **不用 `k` 前缀 + 驼峰**（那是 Google C++ 风格，与仓库现有的 `xxx_hw_map`、`s_ctx` 不一致）
- **`s_` 只表示「文件作用域可变状态」** —— 不是 static，更不是 status：
  `s_ctx` / `s_net` / `s_port` 是状态；`static` 辅助函数不加这个前缀（见下条），
  写成 `s_write_reg()` 会和状态变量撞语义
- **内部辅助函数带模块前缀**：`static` 函数统一 `<模块>_` 前缀 —— `eeprom_page_write()`、
  `ap3216c_write_reg()`、`soft_i2c_start()`、`sdcard_read_sector()`；不要留 `write_reg()` 这种
  脱离文件就看不出归属的光杆名字。例外：FreeRTOS 任务入口（`vXxxTask`）与库规定名的回调
  （`disk_*`、`HAL_*Callback`）
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

## 常量与宏使用规范 (Constant and Macro Usage Rules)

严格遵守 C11 现代实践，减少预处理器滥用。选择常量形式时按“语义”和“可见范围”判断：

| 场景 | 允许/禁止 | 替代方案 |
|------|-----------|----------|
| 条件编译 (`#if`/`#ifdef`) / 功能开关 | ✅ 允许 | — |
| 头文件保护 (`#ifndef` guard) | ✅ 允许 | — |
| 错误码 / 状态码 / 枚举 ID | ❌ 禁止 `#define` | `typedef enum` |
| 一组互斥的状态、模式或选项 | ❌ 禁止 `#define` | `typedef enum`；公共 API 放 `.h`，模块私有值放 `.c` |
| 同类事件的位掩码（位集合） | ❌ 禁止 `#define` | 匿名 `enum`；跨模块公开放 `.h`，仅模块内部使用放 `.c` |
| 数组尺寸等必须是编译期整数 | ✅ 允许 | — |
| 单值配置常量 / 周期 / 尺寸 / 超时 | ⚠️ 最小化 | 数据类变量优先在`.c` 内使用 `static const <类型>`，字符串常量允许使用define；<br />如果某些数据类常量或者变量默认值、字符串常量确需公开时给第三方修改，数据等式右侧赋值数值（或字符串宏定义）可以使用宏定义开放到`.h`文件中<br />但这类放在h文件的宏定义过多的话，需要抽离成单独xxx_config.h文件管理，详情见下方 |
| 寄存器位操作 / 硬件地址 | ✅ 允许 (HAL/Start 层) | 保留来源注释，优先给寄存器与位域赋予明确语义 |
| 函数别名 / API 适配宏 | ⚠️ 最小化 | 优先用 `static inline` 函数 |

### 单值常量

不能为了消除 `#define` 就把所有内部常量塞进枚举。枚举只表达有限集合；一个周期、超时、数量、缓冲区尺寸、重试次数是一个配置值，应保留明确数据类型。

模块内部且不对外暴露的配置值放 `.c`，并用 `snake_case` 命名的 `static const`：

```c
static const uint32_t task_net_tick_ms     = 1000U;
static const uint32_t task_net_retry_ms    = 1000U;
static const uint16_t task_net_sock_rounds = 8U;
static const uint32_t task_net_int_retry_ms = 2U;
```

“数据类变量”指有数值语义的东西（周期、超时、尺寸、阈值、重试次数）。“字符串常量”不属于这一类，
`#define` 一直可用：

```c
#define LOG_TAG "W5500"                             /* 必须用宏: 日志宏要在编译期拼接 */
static const char default_host[] = "192.168.1.1";   /* 只在 .c 内使用: 数组更好调试 */
```

`.h` 中不定义只被一个 `.c` 使用的数据类配置（要开放给第三方改的项例外，见「使用者可调的配置常量」）。
公开时按“使用者什么时候改”分两类：

- **编译期改数值 / 字符串**：`.h` 里放宏，只承担**等式右侧的数值或字符串**；实现侧仍用
  `static const <类型>` 接住它（见下一节的 A 模式）
- **运行期改**：那是参数不是常量，走 setter，由模块状态结构体保存（见「模块状态管理」）

如果确实要跨文件共享一个有类型对象时，优先考虑提供该变量的访问api，尽量避免用`extern const` 作为对外开放配置的手段。

**存量代码按迁移口径逐步收敛**：[task_net.c](../Application/Tasks/task_net.c) 曾把周期/超时写成
单用途匿名 `enum`（`enum { TASK_NET_TICK_MS = 1000 };`），属历史写法，已随 P0 整改改成
`static const`。其余模块遇到时顺手收敛，不单独做一次全仓替换。

配置值与枚举示例：

```c
/* ❌ 禁止：只为消除宏而创建无意义枚举项 */
enum { EEPROM_PAGE_SIZE = 8 };

/* ✅ 正确：内部单值配置携带明确类型 */
static const uint8_t eeprom_page_size = 8U;

/* ✅ 正确：对外开放的数值 —— 宏只做等式右侧的数值来源, 类型留在实现侧 */
/* bsp_adc.h:  #define BSP_ADC_VREF_MV  3300U */
static const uint32_t adc_vref_mv = BSP_ADC_VREF_MV;

/* ✅ 正确：公开枚举表达“状态集合” */
typedef enum {
    BSP_I2C_OK = 0,
    BSP_I2C_ERROR,
    BSP_I2C_TIMEOUT,
} BSP_I2C_Status_t;
```

### 使用者可调的配置常量

`.c` 里的 `static const` 是默认落点，但它同时封住了“使用者想改就改”这条路。对外开放的配置按**使用者
改的时机**分两条路径；宏只出现在路径 A，且只承担等式右侧的数值或字符串。

#### A. 编译期可改 —— 宏给值，类型留在实现侧

```c
/* bsp_adc.h —— 对外开放: 改这里, 或 -DBSP_ADC_VREF_MV=3300 */
#define BSP_ADC_VREF_MV  3300U

/* bsp_adc.c —— 实现侧仍是有名字、有类型的常量 */
static const uint32_t adc_vref_mv = BSP_ADC_VREF_MV;
```

- 宏本身**不带类型**：`3300` 是 `int`，直接进运算会发生符号提升与隐式截断。让宏只提供数值、
  由 `static const <类型>` 接住，类型检查与调试信息就落在对象上
- 字符串配置同理：`.h` 用 `#define` 给默认值，实现侧用 `static const char[]` 或指针接住
- 这条路径的前提是“改完要重新编译”，与 `static const` 的行为一致；需要热改的走路径 B

#### B. 运行期可改 —— 提供 setter，不做常量

运行期会变的配置是**参数**不是常量：提供 `Xxx_SetConfig()`，由模块状态结构体保存（见「模块状态管理」）。
不要为了“以后也许要热改”先把值做成宏，也不要同时保留“宏改 + setter 改”两条改法 —— 后者会让
“现在生效的是哪一份”变成需要推理的问题。

#### 开放到模块头文件时的数量控制

| 情况 | 做法 |
|------|------|
| 只有一两个、属于模块自身的常规参数 | 直接放模块公开头文件，`#define` + 完整模块前缀 |
| 数量开始增长（超过一屏 / 超过 3 组） | 抽成独立 `xxx_config.h`，**只被 `.c` 包含** |
| 必须参与 `#if` 的编译开关 | 无论数量多少都必须用宏 |

现状示例（符合“放模块头文件”这条）：[bsp_adc.h](../BSP/Inc/bsp_adc.h) 的 `BSP_ADC_VREF_MV` /
`BSP_ADC_RES_MAX` —— 两个项、跟 ADC 量程换算直接相关，留在模块头文件里最省事。它们在
[bsp_adc.c](../BSP/Src/bsp_adc.c) 里直接参与运算，两个宏都带 `U` 后缀，所以没有符号提升问题；
若以后这类参与运算的配置项变多，就按 A 模式在 `.c` 里接一层 `static const <类型>`。

放模块公开头文件的要求：

- **命名必须带完整模块前缀**（`BSP_ADC_`、`LOG_`）。宏没有作用域，只有前缀能挡住重名
- 一个头文件里的这类配置**不要超过一屏**；超过一屏说明该拆了
- 宏的值要自带类型后缀（`3300U`、`8U`），不要写成裸数字；不要用宏拼装复杂表达式掩盖类型
- 公开头文件中的宏会被**所有**包含者看到，因此不得用宏定义与配置无关的实现细节

抽离 `xxx_config.h` 的规则：

```
BSP/Inc/bsp_ili9341.h           公开 API: 类型 + 函数声明（不含可调配置）
BSP/Inc/bsp_ili9341_config.h    可调配置: 引脚、方向、字体尺寸、超时
BSP/Src/bsp_ili9341.c           #include 两者
```

- **`xxx_config.h` 只由 `.c` 包含**，公开 API 头文件不 `#include` 它。于是包含 `bsp_xxx.h` 的模块
  不会被动继承这批宏，命名空间保持干净
- `xxx_config.h` 自身只依赖 `<stdint.h>` 等标准头，不 `#include` 项目内其他头文件 —— 否则会引入依赖环
- 同一配置只允许一处定义。不要既在 `xxx_config.h` 定义、又在 `xxx_config.h` 之外导出一份同义常量
- 配置头里的宏同样只承担等式右侧的数值/字符串；`.c` 用 `static const <类型>` 接住后再参与运算
- 层归属跟随模块：BSP 模块的配置头放 `BSP/Inc/`，`Services/devices/` 的配置头放同目录
- 例外：若配置值必须出现在**调用方**的编译期上下文（例如调用方按它声明静态数组），它就只能留在公开
  头文件里 —— 这时优先把该长度封装成模块内部的数组，避免把尺寸常量交给调用方

判据一句话：**“使用者会改、改完不用碰 API” → 配置头；否则留 `.c` 的 `static const`。**

### 内部配置常量的分组 (`static const` 结构体)

常量数量一多，平铺的 `xxx_yyy` 会退化成第二套全局命名空间。`static const` 的粒度应该是**功能**，
不是“每个值一条”。

- 一组服务于同一功能的常量（寄存器地址表、寄存器配置值、时序参数）装进**同一个 `static const`
  结构体实例**，按功能拆成 2~4 组，而不是 20 个平铺常量
- 实例名 `snake_case`；字段名不再重复模块前缀，靠实例名区分（`mpu_regs.who_am_i`）
- **只在 `.c` 内使用、且不作为参数传递时，直接用匿名结构体**，连类型名都不用起：
  `static const struct { ... } close_timeout = { ... };`。要传参或要复用类型时才 `typedef`
- 对外开放的项仍走宏：字段初值可以取自 `.h` 的配置宏（`{.tick_ms = XXX_TICK_MS}`），结构体本身
  依旧是 `static const`
- 字段用最窄够用的类型（`uint8_t` / `uint16_t` / `uint32_t`）
- 全 `const` 初始化 ⇒ 落在 Flash 的 `.rodata`，**不占 RAM**；`cfg.field` 这类访问在优化构建下会
  常量折叠，不产生额外访存
- 不要为了分组而分组：只有一两个常量就直接 `static const`；互不相关的常量不要塞进同一个“杂物筐”
- 查表/数组本来就是一个常量（`spi_prescaler_to_hal[]`、`hw_map[]`），不需要再包一层结构体

以 [bsp_mpu6050.c](../BSP/Src/bsp_mpu6050.c) 为例，19 个平铺常量按功能收敛成 3 个实例
（类型只在 `.c` 内用，所以是匿名 struct）：

```c
static const uint32_t mpu6050_i2c_speed = 400000U;

static const struct {
    uint8_t pwr_mgmt1;
    uint8_t smplrt_div;
    uint8_t config;
    uint8_t gyro_config;
    uint8_t accel_config;
    uint8_t accel_xout_h;
    uint8_t who_am_i;
} mpu6050_regs = {
    .pwr_mgmt1    = 0x6B,
    .smplrt_div   = 0x19,
    .config       = 0x1A,
    .gyro_config  = 0x1B,
    .accel_config = 0x1C,
    .accel_xout_h = 0x3B,
    .who_am_i     = 0x75,
};

static const struct {
    uint8_t  wake_up;
    uint8_t  smplrt_div;
    uint8_t  dlpf_cfg;
    uint8_t  accel_fs;
    uint8_t  gyro_fs;
    uint8_t  who_am_i_expected;
    uint32_t pwr_on_delay_ms;
    uint32_t stabilize_delay_ms;
    float    temp_scale;
    float    temp_offset;
} mpu6050_cfg = {
    .wake_up            = 0x00,
    .smplrt_div         = 0x07,
    .dlpf_cfg           = 0x06,
    .accel_fs           = 0x08,
    .gyro_fs            = 0x18,
    .who_am_i_expected  = 0x68,
    .pwr_on_delay_ms    = 100U,
    .stabilize_delay_ms = 200U,
    .temp_scale         = 340.0f,
    .temp_offset        = 36.53f,
};

/* burst read 长度要当栈数组尺寸 → 编译期整数, 用宏 */
#define MPU6050_BURST_LEN   14U
```

判断是否“该分组”的两个问题：

1. 这些值是否会在同一个函数里成组出现？是 → 一个结构体
2. 加新常量时，你会不会纠结该叫什么前缀？会 → 它多半属于已有的一组

**注意不要“为了分组而分组”**：单个常量、或彼此无关的常量（如上面的 I2C 速率）就各自
`static const`；结构体成员里若有一个值需要当**数组尺寸**（编译期整数），它必须单独提成宏，
不能用 `struct.field` 当数组长度 —— 那会变成 VLA。

### `typedef enum` 与匿名 `enum`

- `typedef enum` 用于需要在 API 签名中表达类型的状态码、模式、外设 ID，以及调用方会保存或比较的离散值。
- 匿名 `enum` 适合一组语义同属一类、主要在预处理/编译期使用的整数集合，例如公开 API 的各类状态或模块私有的事件位掩码。
- 不要为了给单个配置项“命名成枚举常量”而新建枚举。
- 使用枚举的场景都要确保要定义的值都是唯一的，避免出现同类型不同名但值重复的枚举值；

### 事件位与位掩码

事件位属于“同一种通知机制中的若干来源”这一集合，因此匿名 `enum` 是合理形式；不要只为了一个事件位创建枚举，也不要把它们改成彼此无关的单独宏或 `static const`。

| 位置 | 做法 | 示例 |
|------|------|------|
| `.h` | 多模块共用、属于公共协议的事件位或状态位 | socket 中断状态、芯片提供的多 bit 状态寄存器 |
| `.c` | 模块私有的任务唤醒位、内部状态位 | `TASK_NET_EVT_SOCKET` |

事件位应使用无符号常量：位数超过 `unsigned int` 范围时必须显式使用宽度后缀或宽整型类型，例如 `UINT32_C(1) << n`。公共位掩码必须说明“生产者”和“消费者”，避免位字段失去来源语义。

```c
/* .c：模块私有、同属任务通知来源集合 */
enum {
    /* 生产者: net_int_isr */
    TASK_NET_EVT_SOCKET = 1u << 0,
};
```

当事件位是芯片或协议实际定义的状态字段时，应放在对应模块的公开头文件中，并保留芯片寄存器位的来源与含义；不要在任务 `.c` 中重新发明一套同名协议常量。

### `.h` 与 `.c` 的职责边界

公共头文件只放公共 API、公共类型、公开协议常量以及确实需要跨模块共享的位掩码。头文件中不得为了方便实现而暴露模块私有配置。

- 同一文件内的相关常量优先合并成一个小结构体或枚举集合，不要在头尾散落十几个单用途 `enum`。
- 私有常量的可见性只到所在 `.c`；不要以 `extern` 暴露来规避定义位置。
- 头文件中的 `typedef enum` 名称应表达集合语义（状态、模式、ID），不是单独的配置项容器。
- `static const` 对象在 Debug 中通常保留类型与调试信息；使用 `const` 还能防止实现代码意外修改配置值。

## 模块状态管理 (Module State)

`.c` 文件作用域的**可变**状态（跨调用保留）必须集中管理，防止模块长大后变成一堆散落的 `s_xxx`。
这条只管状态；只读配置走上节的 `static const` / `static const` 结构体，两者不要混放。

### 状态结构体

- 状态实例名一律 **`s_<模块>`**：`s_usart_ctx`、`s_adc_ctx`、`s_lcd_ctx`、`s_log`、`s_net`。
  **加 `s_` 前缀时不要顺手把模块前缀删掉** —— `s_ctx` 这种名字出了本文件就没有归属信息，
  读别的文件要回头翻定义才知道是谁的状态；`s_` 只表达"可变、跨调用"，不表达"属于哪个模块"
- **`s_` 前缀专用于状态对象**。`static` 辅助函数用 `<模块>_` 前缀（`w5500_phy_config()`、
  `sock_wait_closed()`），只读常量不加前缀（`task_net_tick_ms`）—— 于是看到 `s_xxx`
  就能断定它是可写的跨调用状态
- **门槛：1~2 个状态变量可以各自独立 `static`；出现第 3 个、且它们描述同一件事（同一个任务、
  同一个外设实例的状态机、同一份运行期账本）时，收进结构体**。不要等长到十个才动手 ——
  但两个就合并属于过度操作，反而让单变量模块多一层 `.field` 噪音
- 结构体只装跨调用状态。函数内的临时变量、只在一次调用内有效的中间值不进
- 只在本文件使用时用匿名 `struct { ... } s_<模块>;`；要作为参数传递、或要在数组/多个实例间复用类型时，
  才写 `typedef struct { ... } XxxCtx_t;`
- 默认值一律用 designated initializer（与文末「HAL 结构体初始化规范」同一条理由），匿名的也能写
- 多实例外设用下标数组：`static Xxx_Ctx_t s_<模块>_ctx[BSP_XXX_COUNT];`（如 `s_usart_ctx[]`），
  与 API 的枚举 ID 一一对应
- 结构体类型不导出到头文件。要观测内部状态就提供只读快照 API，不要 `extern` 一个全局实例
- 运行期禁止对状态结构体整体 `memset` —— 会把锁、句柄、计数器一起清掉；只在初始化阶段清零一次

以 [task_net.c](../Application/Tasks/task_net.c) 为例（类型不导出，所以用匿名 struct）：

```c
static struct {
    TaskHandle_t task;        /* ISR 靠它置事件位; 任务入口登记后只读 */
    uint8_t      phy_last;    /* 上次读到的 PHYCFGR */
    uint8_t      phy_first;   /* 首次读数尚未打印 */
} s_net = {
    .phy_first = 1,
};
```

调用处从 `s_phy_last` 变成 `s_net.phy_last`；新增 “上次链路状态时间戳” 之类字段时不再扩大文件级命名空间。

### 被 ISR 访问的字段

结构体不改变并发语义，只改变命名空间：

- 在字段旁注明访问方（任务 / ISR），例如 `/* ISR 写, 任务读 */`
- 同步靠 FreeRTOS 原语或临界区，**不要靠 `volatile` 做同步**。`volatile` 只用于内存映射寄存器与
  “必须每次重新读取”的标志位，它不提供原子性，也不建立内存序
- 真正需要原子增减的计数器（如日志丢弃计数）保持 `volatile`，并接受“只用于观察趋势、非精确统计”的
  语义，不要把它当同步手段

### 迁移口径

现有模块里的散落 `s_xxx`（如 [log.c](../Services/log/log.c) 的 7 个文件作用域变量）**不要求一次性重构**。
口径是：新写模块一律按本节执行；改动到某个模块、且需要新增到第 3 个同源状态变量时，顺手把该模块的
状态收进结构体。

## API 设计规范 (API Design Rules)

对于同类型多数量的外设，遵循 `bsp_usart` 模式，禁止 `void*` 句柄和 `extern` 全局总线实例：

| 规范 | 说明 |
|------|------|
| **枚举 ID 代替指针句柄** | `typedef enum { BSP_SPI_BUS_1, BSP_SPI_BUS_COUNT } BSP_SPI_Bus_Id_t;` |
| **`static const` 硬件映射表** | `.c` 文件内部 `static const HW_Map_t hw_map[] = { [BUS_1] = {...} };` |
| **`static` 上下文数组** | `.c` 文件内部 `static Xxx_Ctx_t s_<模块>_ctx[BSP_BUS_COUNT];`，外部不可见 |
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
- **满时语义是「丢掉装不下的部分」**。115200 下环形每毫秒只排掉约 11.5 B，而中文注释让一行就有 60~120 B —— **几毫秒内连着打几行就会溢出**，现象是日志被拦腰截断、后面整行消失（不是串口或上位机丢字节）。实测 D5 一次 8 行约 575 B 就把 512 B 的环打满，丢了两行，其中一行正是刚拿到的网络参数。成组输出时在行间留排空间隙（`vTaskDelay(20)` ≈ 排掉 230 B）；要根治得改 `log.c` 的满时语义或加大缓冲，但那会动到「非阻塞、ISR 可调」这条契约

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
