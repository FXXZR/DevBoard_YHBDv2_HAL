# Project Reference — HAL_TEST_Board STM32F103ZET6

> 项目基础设施参考文档：硬件身份、架构、引脚、时钟、HAL 模块、内存布局、构建参数等。

## Identity

- **Target**: 野火霸道 V2 (YHBDv2) — STM32F103ZET6, Cortex-M3, 512KB Flash, 64KB RAM
- **Debug probe**: DAP-Link (CMSIS-DAP / SWD)
- **Toolchain**: `arm-none-eabi-gcc` 14.2 (ARM GNU Toolchain)
- **Build system**: CMake 3.22+ + Ninja
- **C standard**: C11 (gnu11 extensions)
- **Output**: `build/Debug/HAL_TEST_Board.elf`
- **Current firmware size**: ~56KB FLASH, ~6KB RAM (Debug -O0)

## Architecture

```
Application/           ← business logic + Test/ 测试模块 + Modules/ 胶水层
  ├─ Test/             ← 统一测试头 test.h + 各外设测试 .c
  └─ Modules/          ← 平台胶水层 (fatfs_diskio.c 等)
    ↓ depends on
ThirdParty/            ← 第三方中间件 (FatFS, FreeRTOS...)
    ↓ depends on
BSP/                   ← board abstraction; ONLY layer that includes HAL
    ↓ depends on
Drivers/STM32F1xx_HAL_Driver/  ← ST HAL (全量文件已导入, 按需启用)
Drivers/CMSIS/                  ← ARM CMSIS + device headers

Start/                ← startup, linker script, syscalls, ISR stubs
cmake/stm32cubemx/    ← HAL/CMSIS CMake glue (源文件按外设分组 + 注释)
```

**Layering rule**: Application code NEVER includes HAL peripheral headers directly. Includes `test.h` for test functions, BSP headers for hardware access. `stm32f1xx_hal.h` is allowed for basics like `HAL_GetTick()`.

## Key Files

| File | Role |
|---|---|
| `Application/main.c` | `main()`: 初始化 → Banner → 调用一个 test 函数 |
| `Application/Test/test.h` | 统一测试头, 聚合所有 BSP + 声明所有测试函数 |
| `Application/Test/test_beep_key.c` | BEEP+KEY 联动: KEY1 按下 → 蜂鸣 200ms |
| `Application/Test/test_key.c` | KEY 扫描: KEY1→LED_R, KEY2→LED_G, printf 状态 |
| `Application/Test/test_adc.c` | ADC 采样: PC1 电压每 500ms printf mV |
| `Application/Test/test_rtc.c` | RTC 交互: 定时打印 + 串口命令 T/D/S/U/I/H |
| `Application/Test/test_eeprom.c` | EEPROM 读写验证: AT24C02 全片擦除→写入模式→读回校验 |
| `Application/Test/test_spi_flash.c` | SPI Flash 读写验证: W25Q64 ID→扇区擦→写→读→校验 |
| `Application/Test/test_sdcard_fatfs.c` | SD Card + FatFS 读写验证: 挂载→写文件→读回校验→PASS/FAIL |
| `BSP/Inc/bsp_systick.h` | SysTick: `BSP_SYSTICK_DelayInit/DelayUS/DelayMS()` |
| `BSP/Src/bsp_systick.c` | 时钟配置 (HSE→PLL→72MHz) + us/ms 阻塞延时 |
| `BSP/Inc/bsp_led.h` | LED: `BSP_LED_Init/On/Off/Toggle(BSP_LED_Id_t id)` |
| `BSP/Inc/bsp_usart.h` | USART: 5 路串口, 中断收发, 环形缓冲, printf 重定向 |
| `BSP/Src/bsp_usart.c` | USART 实现: HW 映射表 + HAL 回调 + ISR |
| `BSP/Inc/bsp_beep.h` | BEEP: `BSP_BEEP_Init/On/Off/Toggle()` — PC0, active-high |
| `BSP/Inc/bsp_key.h` | KEY: `BSP_KEY_Init/Scan()` — PA0+PC13, 20ms 消抖 |
| `BSP/Inc/bsp_adc.h` | ADC: `BSP_ADC_Init/Read_mV()` — ADC1 CH11, DMA 循环 |
| `BSP/Inc/bsp_rtc.h` | RTC: `DateTime/GetTimestamp/SetTimestamp` + ts↔dt 互转 |
| `BSP/Src/bsp_rtc.c` | RTC 实现: LSE/LSI 时钟源, Backup 寄存器日期持久化, 冷/热启动 |
| `BSP/Inc/bsp_i2c.h` | HW I2C: `BSP_I2C_Init/MemWrite/MemRead/IsDeviceReady` — I2C1 PB6/PB7 |
| `BSP/Src/bsp_i2c.c` | HW I2C 实现: HAL I2C 阻塞式主机, APB1=36MHz, 默认 100KHz |
| `BSP/Inc/bsp_soft_i2c.h` | SW I2C: `BSP_SoftI2C_Init/MemWrite/MemRead/IsDeviceReady` |
| `BSP/Src/bsp_soft_i2c.c` | SW I2C 实现: GPIO 位带 + `BSP_SYSTICK_DelayUS` 延时, 100K/400K/1M |
| `BSP/Inc/bsp_eeprom.h` | EEPROM: AT24C02, `BSP_EEPROM_USE_SOFT_I2C` 宏选择 I2C 后端 |
| `BSP/Src/bsp_eeprom.c` | EEPROM 实现: 默认软 I2C, 自动页边界拆分, 写周期等待 |
| `BSP/Inc/bsp_spi.h` | SPI: `BSP_SPI_Init/Transfer/TransferBuffer` — SPI1 PA5/PA6/PA7 |
| `BSP/Src/bsp_spi.c` | SPI 实现: HAL SPI 主机模式 18MHz, NSS 软件管理 |
| `BSP/Inc/bsp_spi_flash.h` | SPI Flash: W25Q64 指令集, JEDEC ID=0xEF4017, 8MB 容量 |
| `BSP/Src/bsp_spi_flash.c` | SPI Flash 实现: CS=PA4, 页写/扇区擦/块擦/全片擦, 轮询 BUSY |
| `BSP/Inc/bsp_sdcard.h` | SD Card: `BSP_SDCARD_Init/Read/Write/GetInfo/IsReady` — SDIO DMA |
| `BSP/Src/bsp_sdcard.c` | SD Card 实现: HW映射表+DMA+ISR+MSP全自包含, 阻塞式扇区读写 |
| `ThirdParty/FatFs/` | FatFS R0.15a — `ff.c/h`, `ffconf.h`, `diskio.h` (核心库) |
| `Application/Modules/fatfs_diskio.c` | FatFS 平台胶水层 — `disk_read/write/ioctl` → BSP |
| `Start/startup_stm32f103xe.s` | Vector table, Reset_Handler |
| `Start/STM32F103XX_FLASH.ld` | Linker script — memory regions + sections |
| `Start/stm32f1xx_hal_conf.h` | HAL module enable/disable switches |
| `Start/stm32f1xx_hal_msp.c` | `HAL_MspInit()` — clock enables, SWJ config |
| `Start/stm32f1xx_it.c` | ISR handlers (SysTick → `HAL_IncTick()`) |
| `cmake/stm32cubemx/CMakeLists.txt` | HAL 源文件列表, 按外设分组 + 注释 |
| `cmake/gcc-arm-none-eabi.cmake` | Toolchain definition |

## Pin Map

| Pin | Function | Label |
|-----|----------|-------|
| PA4 | GPIO Output (active-high) | SPI Flash CS (W25Q64) |
| PA5 | AF PP (SPI1_SCK) | SPI Flash SCK |
| PA6 | AF PP (SPI1_MISO) | SPI Flash MISO |
| PA7 | AF PP (SPI1_MOSI) | SPI Flash MOSI |
| PB5 | GPIO Output (active-low) | Red LED |
| PB0 | GPIO Output (active-low) | Green LED |
| PB1 | GPIO Output (active-low) | Blue LED |
| PC0 | GPIO Output (active-high) | Buzzer |
| PA0 | GPIO Input (no pull) | KEY1 |
| PC13 | GPIO Input (no pull) | KEY2 |
| PC1 | ADC1_IN11 | ADC input (potentiometer) |
| PB6 | AF4 OD (I2C1_SCL) | I2C SCL (EEPROM AT24C02) |
| PB7 | AF4 OD (I2C1_SDA) | I2C SDA (EEPROM AT24C02) |
| — | LSE 32.768KHz | RTC 时钟源 (外部晶振) |
| PA9/PA10 | USART1 TX/RX | printf / serial debug |
| PA2/PA3 | USART2 TX/RX | (reserved) |
| PB10/PB11 | USART3 TX/RX | (reserved) |
| PC10/PC11 | UART4 TX/RX | (reserved) |
| PC12/PD2 | UART5 TX/RX | (reserved) |
| PC8 | SDIO_D0 | SD Card Data 0 |
| PC9 | SDIO_D1 | SD Card Data 1 |
| PC10 | SDIO_D2 | SD Card Data 2 |
| PC11 | SDIO_D3 | SD Card Data 3 |
| PC12 | SDIO_CK | SD Card Clock |
| PD2 | SDIO_CMD | SD Card Command |

All LED pins: push-pull, no pull-up/down, high speed. BEEP: push-pull, no pull-up/down, active-high.

## Clock Tree

```
HSE 8MHz → PLL (×9) → SYSCLK 72MHz
  ├─ AHB  (÷1) = 72MHz
  ├─ APB2 (÷1) = 72MHz (USART1)
  └─ APB1 (÷2) = 36MHz (USART2/3/4/5, RTC APB)

RTC 时钟源:
  LSE 32.768KHz → RTC prescaler (RTC_AUTO_1_SECOND) → 1Hz
```

Flash latency = 2WS. SysTick = 1ms via `HAL_IncTick()`.

## Enabled HAL Modules

| 模块 | 宏 | 源文件 |
|------|-----|-------|
| Core | `HAL_MODULE_ENABLED` | `stm32f1xx_hal.c` |
| RCC | `HAL_RCC_MODULE_ENABLED` | `hal_rcc.c` + `hal_rcc_ex.c` |
| GPIO | `HAL_GPIO_MODULE_ENABLED` | `hal_gpio.c` + `hal_gpio_ex.c` |
| DMA | `HAL_DMA_MODULE_ENABLED` | `hal_dma.c` |
| CORTEX | `HAL_CORTEX_MODULE_ENABLED` | `hal_cortex.c` |
| PWR | `HAL_PWR_MODULE_ENABLED` | `hal_pwr.c` |
| FLASH | `HAL_FLASH_MODULE_ENABLED` | `hal_flash.c` + `hal_flash_ex.c` |
| EXTI | `HAL_EXTI_MODULE_ENABLED` | `hal_exti.c` |
| UART | `HAL_UART_MODULE_ENABLED` | `hal_uart.c` |
| ADC | `HAL_ADC_MODULE_ENABLED` | `hal_adc.c` + `hal_adc_ex.c` |
| RTC | `HAL_RTC_MODULE_ENABLED` | `hal_rtc.c` + `hal_rtc_ex.c` |
| SPI | `HAL_SPI_MODULE_ENABLED` | `hal_spi.c` |
| I2C | `HAL_I2C_MODULE_ENABLED` | `hal_i2c.c` |
| SD | `HAL_SD_MODULE_ENABLED` | `hal_sd.c` + `ll_sdmmc.c` |

**启用新模块的两步操作**:
1. `Start/stm32f1xx_hal_conf.h` → 取消注释/添加 `#define HAL_xxx_MODULE_ENABLED`
2. `cmake/stm32cubemx/CMakeLists.txt` → 取消注释对应源文件行（含 `_ex` 配套）

所有 HAL 源文件已全量导入 `Drivers/STM32F1xx_HAL_Driver/`，CMake 中按外设分组注释。未启用的模块源文件不会被编译。

**关于 `_ex` 文件**: 包含扩展功能函数。由于项目使用 `-ffunction-sections -fdata-sections` + `--gc-sections`，即使 `_ex` 源文件被编译，其中未被调用的函数也会被链接器丢弃，不占用 FLASH。因此 `_ex` 文件可以放心加入编译列表。

## Memory Layout

```
FLASH: 0x08000000 (512KB) — code, rodata, .data LMA
RAM:   0x20000000 (64KB)  — .data, .bss, heap (512B), stack (1KB)
_estack = 0x20010000 (RAM top)
```

Linker discards unused libc/libm/libgcc sections (`--gc-sections`). Uses `--specs=nano.specs`.

## Build Flags

- CPU: `-mcpu=cortex-m3`
- Debug: `-O0 -g3`; Release: `-Os -g0`
- Defines: `USE_HAL_DRIVER`, `STM32F103xE`, `DEBUG` (Debug only)
- Compile commands exported to `build/Debug/compile_commands.json` for clangd

## Project Directory Intent

| Directory | Purpose |
|---|---|
| `Application/Test/` | 外设测试模块 (test.h + 各外设测试 .c) |
| `Application/Config/` | App configuration (empty — future) |
| `Application/Common/` | Shared types/macros (empty — future) |
| `Application/Modules/` | Hardware driver modules + 平台胶水层 (fatfs_diskio 等) |
| `Application/DataManager/` | Global state management (empty — future) |
| `Application/Communication/` | Protocol stacks (empty — future) |
| `Application/Tasks/` | RTOS task organization (empty — future) |
| `ThirdParty/` | Middleware: FatFS, FreeRTOS, ... (各自有 CMakeLists) |
| `Services/` | Platform services (empty — future) |

## Firmware Size (Debug -O0)

| Category | ~Size | Notes |
|----------|-------|-------|
| HAL UART | 3.5KB | IRQHandler, Abort, Transmit, config helpers |
| HAL RCC | 2.5KB | OscConfig, ClockConfig, PeriphCLKConfig |
| HAL RTC | 2.3KB | Init, Set/Get Time/Date, BKUP, DateUpdate, WeekDayNum |
| newlib nano | 2.0KB | _printf_i, _vfprintf_r, _printf_common, _malloc_r |
| Test code | 1.7KB | cmd_execute, cmd_feed, parse functions |
| HAL GPIO | 1.2KB | Init, DeInit |
| BSP modules | ~2KB | usart, rtc, led, beep, key, adc, systick |
| HAL DMA | 0.5KB | Abort_IT |
| HAL ADC | 0.3KB | Init (only used functions) |
| Startup + vectors | ~3KB | startup, system_stm32f1xx, syscalls |
| Other (const data, etc.) | ~5KB | rodata, interrupt vectors, etc. |
| FatFS core | ~12KB | ff.c (FF_USE_LFN=0, FF_USE_MKFS=1) |
| HAL SD | ~3.5KB | Init, InitCard, Read/Write Blocks, DMA, IRQ |
| BSP SD Card | ~2KB | CMD16+Polling+DMA+MSP+ISR, SDSC: HW块→512B转换+CMD13轮询 |
| Test SD+FatFS | ~1KB | test_sdcard_fatfs.c (auto f_mkfs FAT32) |
| diskio glue | ~0.5KB | FatFS ↔ BSP bridge |
| **Total** | **~56KB** | |

Debug -O0 is typically 2-3× larger than Release -Os.

## Debug

VS Code F5 debugging via **Cortex-Debug** + **PyOCD** (`.vscode/launch.json`):

| Config | Description |
|--------|-------------|
| **Flash & Debug** | `F5` → builds → flashes → halts at `main()` |
| **Attach (running)** | Attach to running firmware without re-flash |

Requires VS Code extension `marus25.cortex-debug`.

For automated debugging, prefer **serial printf logging** (`/serial-debug`) — simpler and more reliable than CLI GDB for AI-assisted workflows.
