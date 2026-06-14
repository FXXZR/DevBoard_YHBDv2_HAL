# CLAUDE.md — HAL_TEST_Board STM32F103ZET6 Firmware Project

> AI-readable project context. Keep this file dense and technical.

## Identity

- **Target**: 野火霸道 V2 (YHBDv2) — STM32F103ZET6, Cortex-M3, 512KB Flash, 64KB RAM
- **Debug probe**: DAP-Link (CMSIS-DAP / SWD)
- **Toolchain**: `arm-none-eabi-gcc` 14.2 (ARM GNU Toolchain)
- **Build system**: CMake 3.22+ + Ninja
- **C standard**: C11 (gnu11 extensions)
- **Output**: `build/Debug/HAL_TEST_Board.elf`

## Quick Build

```bash
# Incremental
cmake --build build/Debug

# First time or after CMakeLists changes
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build build/Debug

# Generate HEX/BIN
arm-none-eabi-objcopy -O ihex build/Debug/HAL_TEST_Board.elf build/Debug/HAL_TEST_Board.hex
arm-none-eabi-objcopy -O binary build/Debug/HAL_TEST_Board.elf build/Debug/HAL_TEST_Board.bin

# Flash (PyOCD)
pyocd flash --target stm32f103ze --connect under-reset \
  -O reset_type=hw build/Debug/HAL_TEST_Board.elf
```

Claude Code shortcuts: `/build-stm32` (build), `/flash-stm32` (flash).

## Architecture

```
Application/          ← business logic, main.c, no direct HAL access
    ↓ depends on
BSP/                  ← board abstraction (bsp_led); ONLY layer that includes HAL
    ↓ depends on
Drivers/STM32F1xx_HAL_Driver/  ← ST HAL (11 modules compiled)
Drivers/CMSIS/                  ← ARM CMSIS + device headers

Start/                ← startup, linker script, syscalls, ISR stubs
cmake/stm32cubemx/    ← HAL/CMSIS CMake glue (INTERFACE + OBJECT libs)
```

**Layering rule**: Application code NEVER includes HAL headers directly. Always go through BSP or create a new BSP module.

## Key Files

| File | Role |
|---|---|
| `Application/main.c` | `main()`, `SystemClock_Config()`, LED blink loop |
| `BSP/Inc/bsp_led.h` | LED API: `BSP_LED_Init/On/Off/Toggle(BSP_LED_Id_t id)` |
| `BSP/Src/bsp_led.c` | LED GPIO impl, pin map table |
| `Start/startup_stm32f103xe.s` | Vector table, Reset_Handler |
| `Start/STM32F103XX_FLASH.ld` | Linker script — memory regions + sections |
| `Start/stm32f1xx_hal_conf.h` | HAL module enable/disable switches |
| `Start/stm32f1xx_hal_msp.c` | `HAL_MspInit()` — clock enables, SWJ config |
| `Start/stm32f1xx_it.c` | ISR handlers (SysTick → `HAL_IncTick()`) |
| `cmake/gcc-arm-none-eabi.cmake` | Toolchain definition |

## Pin Map

| Pin | Function | Label |
|-----|----------|-------|
| PB0 | GPIO Output (active-low) | Red LED |
| PB1 | GPIO Output (active-low) | Green LED |
| PB5 | GPIO Output (active-low) | Blue LED |

All LED pins: push-pull, no pull-up/down, high speed. GPIOA clock also enabled in `HAL_MspInit()`.

## Clock Tree

```
HSE 8MHz → PLL (×9) → SYSCLK 72MHz
  ├─ AHB  (÷1) = 72MHz
  ├─ APB2 (÷1) = 72MHz
  └─ APB1 (÷2) = 36MHz
```

Flash latency = 2WS. SysTick = 1ms via `HAL_IncTick()`.

## Enabled HAL Modules

`HAL_CORTEX`, `HAL_DMA`, `HAL_FLASH`+`EX`, `HAL_EXTI`, `HAL_GPIO`+`EX`, `HAL_PWR`, `HAL_RCC`+`EX`.

All other modules (ADC, CAN, I2C, SPI, TIM, UART, USB, etc.) are disabled in `stm32f1xx_hal_conf.h`.

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
| `Application/Config/` | App configuration (empty — future) |
| `Application/Common/` | Shared types/macros (empty — future) |
| `Application/Modules/` | Hardware driver modules (empty — future) |
| `Application/DataManager/` | Global state management (empty — future) |
| `Application/Communication/` | Protocol stacks (empty — future) |
| `Application/Tasks/` | RTOS task organization (empty — future) |
| `Services/` | Platform services (empty — future) |
| `ThirdParty/` | Middleware: FreeRTOS, FatFs, etc. (empty — future) |

## Conventions

- **Language**: C11 with GNU extensions
- **Formatting**: `.clang-format` — LLVM-based, 4-space indent, 120 columns
- **Naming**: `BSP_` prefix for board layer, `HAL_` for ST HAL
- **Error handling**: `Error_Handler()` — disables IRQs, infinite loop
- **ISRs**: All weak-aliased to `Default_Handler`; override by defining the handler
- **New files**: Each new module gets an OBJECT library in its own `CMakeLists.txt`, linked into `HAL_TEST_Board.elf`
- **HAL config**: Always edit `Start/stm32f1xx_hal_conf.h` to enable/disable HAL modules

## Current Firmware Behavior

Red (PB0) and Blue (PB5) LEDs alternate blink via busy-wait delay loop in `main()`. Green (PB1) LED code present but commented out.
