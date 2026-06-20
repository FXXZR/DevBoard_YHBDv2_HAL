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

格式 `@ type: 简述 — 补充说明`

- `@` 前缀模拟作者标记，与 VS Code Git 图形面板的提交者列形成视觉对齐
- 叙述用中文，模块/函数/API 名保留原文
- type 使用 conventional commits 分类: `feat`/`fix`/`chore`/`refactor`/`docs`
- 示例: `@ feat: BSP驱动扩充 — BEEP/KEY/ADC/RTC + Test测试模块`

## 模块添加规范

| 操作 | 步骤 |
|------|------|
| **New BSP module** | Add `.h` to `BSP/Inc/`, `.c` to `BSP/Src/`, source to `BSP/CMakeLists.txt` |
| **New test module** | Add `.c` to `Application/Test/`, declare in `test.h`, add source to `Application/CMakeLists.txt` |
| **New HAL module** | Two-step: (1) `hal_conf.h` macro + (2) `cmake/stm32cubemx/CMakeLists.txt` source uncomment |

## 分层规则 (Layering Rule)

Application code NEVER includes HAL peripheral headers directly.
- Includes `test.h` for test functions, BSP headers for hardware access
- `stm32f1xx_hal.h` is allowed for basics like `HAL_GetTick()`

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

## HAL 结构体初始化规范

BSP 层初始化 HAL 结构体时，必须使用 **designated initializers** (指定初始化器)，禁止位置初始化。

原因: HAL 结构体字段顺序不符合直觉 —— 例如 `RTC_DateTypeDef` 是 `{WeekDay, Month, Date, Year}` 而非 `{Year, Month, Date}`。

```c
// ❌ 禁止: 位置初始化
RTC_DateTypeDef date = {0, 6, 21, 2026};

// ✅ 正确: 指定初始化器
RTC_DateTypeDef date = {.WeekDay = 0, .Month = 6, .Date = 21, .Year = 2026};
```
