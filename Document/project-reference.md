# Project Reference — HAL_TEST_Board STM32F103ZET6

> 项目基础设施参考文档：硬件身份、架构、引脚、时钟、HAL 模块、内存布局、构建参数等。

## Identity

- **Target**: 野火霸道 V2 (YHBDv2) — STM32F103ZET6, Cortex-M3, 512KB Flash, 64KB RAM
- **Debug probe**: DAP-Link (CMSIS-DAP / SWD)
- **Toolchain**: `arm-none-eabi-gcc` 14.2 (ARM GNU Toolchain)
- **Build system**: CMake 3.22+ + Ninja
- **C standard**: C11 (gnu11 extensions)
- **Output**: `build/Debug/HAL_TEST_Board.elf`
- **Current firmware size**: 321068 B FLASH (61.24%), 55088 B RAM (84.06%) — Debug（W2 收尾）

## Architecture

四层结构，依赖只允许向下。规范条目见 [conventions.md](conventions.md#分层规则-layering-rule)。

```
Application/                业务逻辑、RTOS 任务、移植钩子
  ├─ Common/                rtos_port_hooks.c (FreeRTOS 移植钩子)
  ├─ Tasks/                 task_init / led / disp / net
  └─ Test/                  各外设验证 .c + 统一测试头 test.h
      ↓ depends on
Services/                   硬件抽象之上、业务逻辑之下
  ├─ log/                   分级日志 (log.c/h, 基于 bsp_usart TX 环形缓冲)
  ├─ bus/                   SPI 总线互斥锁 (spi_bus)
  └─ devices/               器件驱动 + 第三方库移植层
                            (w5500_drv / w5500_net / w5500_port / diskio / lvgl_port)
      ↓ depends on
{ Middleware/,  BSP/ }      ← 平级, 互不依赖
  Middleware/               第三方库源码与配置, 只读 (移植层不在这一层)
    ├─ FreeRTOS/            V11.3.0  (config/ + port/, port 为上游自带)
    ├─ FatFs/               R0.15a    (Source/ + config/)
    ├─ lvgl-8.3.11/         LVGL      (src/ + lv_conf.h)
    └─ ioLibrary/           WIZnet W5500 硬件 TCP/IP 协议栈
  BSP/                      板级硬件资源 + raw 传输; 唯一 include HAL 的层
      ↓ depends on
Drivers/STM32F1xx_HAL_Driver/    ST HAL (全量导入, 按需启用)
Drivers/CMSIS/                   ARM CMSIS + device headers

Start/                   startup, linker script, syscalls, ISR stubs
cmake/stm32cubemx/       HAL/CMSIS CMake glue (源文件按外设分组 + 注释)
```

CMake 目标与依赖：

| target | 类型 | 依赖 |
| ------ | ---- | ---- |
| `iolibrary` | OBJECT | 无（通过注册回调访问硬件） |
| `fatfs` | OBJECT | 无（`diskio.c` 已移至 `services`） |
| `lvgl` | OBJECT | 无（include 路径直指 `BSP/Inc`，供 `lv_conf.h` 取 SRAM 地址常量） |
| `bsp` | OBJECT | HAL |
| `services` | OBJECT | `bsp` `middleware`（`diskio` / `lvgl_port` 在此编译） |
| `app` | OBJECT | `middleware` `bsp` `services` |

## Key Files

| File                                     | Role                                                                   |
| ---------------------------------------- | ---------------------------------------------------------------------- |
| `Application/main.c`                   | `main()`: BSP 初始化 → LOG → 外部 SRAM → W5500 自检 → 启动调度器       |
| `Application/Test/test.h`              | 统一测试头, 聚合所有 BSP + 声明所有测试函数                            |
| `Application/Test/test_beep_key.c`     | BEEP+KEY 联动: KEY1 按下 → 蜂鸣 200ms                                 |
| `Application/Test/test_key.c`          | KEY 扫描: KEY1→LED_R, KEY2→LED_G, printf 状态                        |
| `Application/Test/test_adc.c`          | ADC 采样: PC1 电压每 500ms printf mV                                   |
| `Application/Test/test_rtc.c`          | RTC 交互: 定时打印 + 串口命令 T/D/S/U/I/H                              |
| `Application/Test/test_eeprom.c`       | EEPROM 读写验证: AT24C02 全片擦除→写入模式→读回校验                  |
| `Application/Test/test_spi_flash.c`    | SPI Flash 读写验证: W25Q64 ID→扇区擦→写→读→校验                    |
| `Application/Test/test_sdcard_fatfs.c` | SD Card + FatFS 读写验证: 挂载→写文件→读回校验→PASS/FAIL            |
| `BSP/Inc/bsp_systick.h`                | SysTick:`BSP_SYSTICK_DelayInit/DelayUS/DelayMS()`                    |
| `BSP/Src/bsp_systick.c`                | 时钟配置 (HSE→PLL→72MHz) + us/ms 阻塞延时                            |
| `BSP/Inc/bsp_led.h`                    | LED:`BSP_LED_Init/On/Off/Toggle(BSP_LED_Id_t id)`                    |
| `BSP/Inc/bsp_usart.h`                  | USART: 5 路串口, 中断收发, 环形缓冲, printf 重定向                     |
| `BSP/Src/bsp_usart.c`                  | USART 实现: HW 映射表 + HAL 回调 + ISR                                 |
| `BSP/Inc/bsp_beep.h`                   | BEEP:`BSP_BEEP_Init/On/Off/Toggle()` — PC0, active-high             |
| `BSP/Inc/bsp_key.h`                    | KEY:`BSP_KEY_Init/Scan()` — PA0+PC13, 20ms 消抖                     |
| `BSP/Inc/bsp_adc.h`                    | ADC:`BSP_ADC_Init/Read_mV()` — ADC1 CH11, DMA 循环                  |
| `BSP/Inc/bsp_rtc.h`                    | RTC:`DateTime/GetTimestamp/SetTimestamp` + ts↔dt 互转               |
| `BSP/Src/bsp_rtc.c`                    | RTC 实现: LSE/LSI 时钟源, Backup 寄存器日期持久化, 冷/热启动           |
| `BSP/Inc/bsp_i2c.h`                    | HW I2C:`BSP_I2C_Init/MemWrite/MemRead/IsDeviceReady` — I2C1 PB6/PB7 |
| `BSP/Src/bsp_i2c.c`                    | HW I2C 实现: HAL I2C 阻塞式主机, APB1=36MHz, 默认 100KHz               |
| `BSP/Inc/bsp_soft_i2c.h`               | SW I2C:`BSP_SoftI2C_Init/MemWrite/MemRead/IsDeviceReady`             |
| `BSP/Src/bsp_soft_i2c.c`               | SW I2C 实现: GPIO 位带 +`BSP_SYSTICK_DelayUS` 延时, 100K/400K/1M     |
| `BSP/Inc/bsp_eeprom.h`                 | EEPROM: AT24C02,`BSP_EEPROM_USE_SOFT_I2C` 宏选择 I2C 后端            |
| `BSP/Src/bsp_eeprom.c`                 | EEPROM 实现: 默认软 I2C, 自动页边界拆分, 写周期等待                    |
| `BSP/Inc/bsp_spi.h`                    | SPI:`BSP_SPI_Init/Transfer/TransferBuffer` — SPI1 PA5/PA6/PA7       |
| `BSP/Src/bsp_spi.c`                    | SPI 实现: HAL SPI 主机模式 18MHz, NSS 软件管理                         |
| `BSP/Inc/bsp_spi_flash.h`              | SPI Flash: W25Q64 指令集, JEDEC ID=0xEF4017, 8MB 容量                  |
| `BSP/Src/bsp_spi_flash.c`              | SPI Flash 实现: CS=PA4, 页写/扇区擦/块擦/全片擦, 轮询 BUSY             |
| `BSP/Inc/bsp_sdcard.h`                 | SD Card:`BSP_SDCARD_Init/Read/Write/GetInfo/IsReady` — SDIO DMA     |
| `BSP/Src/bsp_sdcard.c`                 | SD Card 实现: HW映射表+DMA+ISR+MSP全自包含, 阻塞式扇区读写             |
| `Middleware/FatFs/Source/`             | FatFS R0.15a 上游源码 (`ff.c/h`, `diskio.h`), 只读                     |
| `Services/devices/diskio.c`            | FatFs 平台胶水层 — `disk_read/write/ioctl` → BSP                       |
| `BSP/Inc/bsp_w5500_hw.h`               | W5500 板级: CS=PG9 / RST=PG15 / INT=PG8, 复位时序                      |
| `BSP/Src/bsp_w5500_hw.c`               | W5500 板级实现 + SPI2 初始化 (PB13/14/15, 模式 3)                      |
| `Services/log/log.h` `log.c`           | 分级日志 — 预处理期分级, DWT 补启动阶段时间戳                          |
| `Services/bus/spi_bus.h` `spi_bus.c`   | SPI 总线互斥锁 — 锁的边界 = CS 的边界                                  |
| `Services/devices/w5500_drv.c`         | W5500 上电 / 通路校验 / PHY 配置                                       |
| `Services/devices/w5500_net.c`         | W5500 网络层 — 网络参数 (静态 IP) + socket 超时封装                    |
| `Services/devices/w5500_port.c`        | ioLibrary 移植层 — 注册 CS/SPI/临界区回调 (含总线锁)                   |
| `Services/devices/lvgl_port.c` `h`     | LVGL 移植层 — 显示 flush (DMA) + 触摸读取回调挂接                      |
| `Middleware/ioLibrary/`                | WIZnet 官方 W5500 硬件 TCP/IP 协议栈, MIT, 只读                        |
| `Application/Tasks/task_net.c`         | 网络任务 — 单任务持有 socket 层; 当前 1Hz 轮询 PHYCFGR                 |
| `Start/startup_stm32f103xe.s`          | Vector table, Reset_Handler                                            |
| `Start/STM32F103XX_FLASH.ld`           | Linker script — memory regions + sections                             |
| `Start/stm32f1xx_hal_conf.h`           | HAL module enable/disable switches                                     |
| `Start/stm32f1xx_hal_msp.c`            | `HAL_MspInit()` — clock enables, SWJ config                         |
| `Start/stm32f1xx_it.c`                 | ISR handlers (SysTick →`HAL_IncTick()`)                             |
| `cmake/stm32cubemx/CMakeLists.txt`     | HAL 源文件列表, 按外设分组 + 注释                                      |
| `cmake/gcc-arm-none-eabi.cmake`        | Toolchain definition                                                   |

## Pin Map

| Pin       | Function                  | Label                     |
| --------- | ------------------------- | ------------------------- |
| PA4       | GPIO Output (active-high) | SPI Flash CS (W25Q64)     |
| PA5       | AF PP (SPI1_SCK)          | SPI Flash SCK             |
| PA6       | AF PP (SPI1_MISO)         | SPI Flash MISO            |
| PA7       | AF PP (SPI1_MOSI)         | SPI Flash MOSI            |
| PB5       | GPIO Output (active-low)  | Red LED                   |
| PB0       | GPIO Output (active-low)  | Green LED                 |
| PB1       | GPIO Output (active-low)  | Blue LED                  |
| PC0       | GPIO Output (active-high) | Buzzer                    |
| PA0       | GPIO Input (no pull)      | KEY1                      |
| PC13      | GPIO Input (no pull)      | KEY2                      |
| PC1       | ADC1_IN11                 | ADC input (potentiometer) |
| PB6       | AF4 OD (I2C1_SCL)         | I2C SCL (EEPROM AT24C02)  |
| PB7       | AF4 OD (I2C1_SDA)         | I2C SDA (EEPROM AT24C02)  |
| PB13      | AF PP (SPI2_SCK)          | W5500 SCK                 |
| PB14      | AF PP (SPI2_MISO)         | W5500 MISO                |
| PB15      | AF PP (SPI2_MOSI)         | W5500 MOSI                |
| PG9       | GPIO Output (active-low)  | W5500 CS                  |
| PG15      | GPIO Output (active-low)  | W5500 RST                 |
| PG8       | EXTI8 下降沿              | W5500 INT                 |
| —        | LSE 32.768KHz             | RTC 时钟源 (外部晶振)     |
| PA9/PA10  | USART1 TX/RX              | printf / serial debug     |
| PA2/PA3   | USART2 TX/RX              | (reserved)                |
| PB10/PB11 | USART3 TX/RX              | (reserved)                |
| PC10/PC11 | UART4 TX/RX               | (reserved)                |
| PC12/PD2  | UART5 TX/RX               | (reserved)                |
| PC8       | SDIO_D0                   | SD Card Data 0            |
| PC9       | SDIO_D1                   | SD Card Data 1            |
| PC10      | SDIO_D2                   | SD Card Data 2            |
| PC11      | SDIO_D3                   | SD Card Data 3            |
| PC12      | SDIO_CK                   | SD Card Clock             |
| PD2       | SDIO_CMD                  | SD Card Command           |

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

## Known Hardware Quirks (硬件坑, 改动前必读)

> 本节只记**硬件本身的性质**。已定位根因的历史故障不在此展开 —— 见
> 本地疑难问题记录（不入库；含"已排除"清单与排查方法教训，动手前值得先翻一眼）。

### 屏幕实际控制器是 ST7789V

标称 ILI9341，**实际为 ST7789V**（ReadID = `0x8552`）。驱动按 ID 自动选择初始化
序列（[bsp_lcd_ili9341.c](../BSP/Src/bsp_lcd_ili9341.c) 的 `BSP_LCD_ReadID`）。
`BSP_LCD_SetScanMode` 的 MADCTL 基值也随 ID 不同（ST7789V 无 BGR 位）。

### 屏幕复位时序必须是真实时长的延时

`bsp_lcd_ili9341.c` 的 `lcd_delay()` **不能用空转 NOP 循环实现** —— 其耗时会随
代码在 Flash 中的地址漂移而改变，导致面板复位时序不稳定。详见
本地疑难问题记录 001（不入库）。

### FSMC 时序参数下溢

F1 HAL LL 的 `FSMC_NORSRAM_Timing_Init` 计算 BTR 时做 `(CLKDivision-1)` / `(DataLatency-2)`, 传 0 会无符号下溢 (实测 BTR4=0x3FF00401, ACCMOD 错成 Mode D). 异步模式无功能影响, 但必须传合法值 (CLKDivision≥1, DataLatency≥2).

### 8080 并口读写时序不对称

LCD 的 8080 并口读周期远慢于写: 手册要求 RD 低电平 ≥355ns (T_RDLFM), 而写低电平只要 ≥15ns (T_WRL)、写周期 ≥66ns (T_WC). 原来读写共用同一套 BTR 时序, 读脉冲只有 69ns, 短了约 5 倍 —— 失败模式是"读回的值微妙地不对", 极难察觉. 现改为扩展模式 (Mode D): BTR 管写、BWTR 管读.

## Enabled HAL Modules

| 模块   | 宏                            | 源文件                               |
| ------ | ----------------------------- | ------------------------------------ |
| Core   | `HAL_MODULE_ENABLED`        | `stm32f1xx_hal.c`                  |
| RCC    | `HAL_RCC_MODULE_ENABLED`    | `hal_rcc.c` + `hal_rcc_ex.c`     |
| GPIO   | `HAL_GPIO_MODULE_ENABLED`   | `hal_gpio.c` + `hal_gpio_ex.c`   |
| DMA    | `HAL_DMA_MODULE_ENABLED`    | `hal_dma.c`                        |
| CORTEX | `HAL_CORTEX_MODULE_ENABLED` | `hal_cortex.c`                     |
| PWR    | `HAL_PWR_MODULE_ENABLED`    | `hal_pwr.c`                        |
| FLASH  | `HAL_FLASH_MODULE_ENABLED`  | `hal_flash.c` + `hal_flash_ex.c` |
| EXTI   | `HAL_EXTI_MODULE_ENABLED`   | `hal_exti.c`                       |
| UART   | `HAL_UART_MODULE_ENABLED`   | `hal_uart.c`                       |
| ADC    | `HAL_ADC_MODULE_ENABLED`    | `hal_adc.c` + `hal_adc_ex.c`     |
| RTC    | `HAL_RTC_MODULE_ENABLED`    | `hal_rtc.c` + `hal_rtc_ex.c`     |
| SPI    | `HAL_SPI_MODULE_ENABLED`    | `hal_spi.c`                        |
| I2C    | `HAL_I2C_MODULE_ENABLED`    | `hal_i2c.c`                        |
| SD     | `HAL_SD_MODULE_ENABLED`     | `hal_sd.c` + `ll_sdmmc.c`        |

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
| --------- | ------- |
| `Application/Tasks/` | RTOS 任务 (task_init / led / disp / net) |
| `Application/Common/` | FreeRTOS 移植钩子 (`rtos_port_hooks.c`) |
| `Application/Test/` | 外设验证模块 (`test.h` + 各外设 .c) |
| `Application/DataManager/` | 全局状态管理 (空 — 预留) |
| `Application/Communication/` | 协议栈 (空 — 预留) |
| `Application/Modules/` | 空 — 平台胶水层已并入 `Services/devices/` |
| `Services/log/` | 分级日志 |
| `Services/bus/` | 总线服务 — 锁 / 事务边界 |
| `Services/devices/` | 器件驱动 + 第三方库移植层 |
| `Middleware/` | 第三方库: FreeRTOS / FatFs / LVGL / ioLibrary (各自 CMakeLists) |
| `BSP/` | 板级硬件资源 + raw 传输 |
| `Start/` | startup, linker script, syscalls, ISR stubs |
| `Document/` | 本目录 — 规范与参考 |

## Firmware Size (Debug, `-O0 -g3`)

最近一次实测（链接器 region 输出）：

| Region | Used | Total | % |
| ------ | ---- | ----- | - |
| FLASH | 321068 B | 512 KB | 61.24% |
| RAM | 55088 B | 64 KB | 84.06% |

分阶段（FLASH）：

| 阶段 | FLASH |
| ---- | ----- |
| BSP + HAL + FreeRTOS + LVGL | 306012 B |
| + ioLibrary vendor（无代码引用，归档成员不被抽出） | 306012 B |
| + Services 层 + W5500 接入 | 315608 B |
| + FatFs/LVGL 移植层迁入 Services（代码未增减，段排布位移） | 315600 B |
| + 网络层（w5500_net：参数/socket/服务端/UDP） | 330568 B |
| + D4 中断通路 / D5 DHCP+DNS 脚手架 | 340456 B |
| + D6 清理测试脚手架（净删约 1000 行） | **321068 B** |

> 内部 RAM 已用 84%。**新增缓冲区前先看余量。**
> 当前占用大头（`nm --size-sort`）：两个 LVGL 绘图缓冲各 15360 B（共 30720 B，占已用
> 的一半）、FreeRTOS 堆 16384 B。要腾空间优先动绘图缓冲（240×32 → 240×16 省 15KB），
> 不要动 LVGL 内存池 —— 它已经在片外 SRAM 了。
> Release 为 `-Os -g0`，通常显著小于 Debug。

## 网络参数与 W5500 资源

### 参数（部署数据，在 `Application/Tasks/task_net.c`）

| 项 | 值 |
| --- | --- |
| MAC | `02:08:DC:01:02:03`（首字节 0x02 = 本地管理地址，不撞真实厂商 OUI） |
| 默认路径 | 静态 `192.168.1.100/24`，gw/dns `192.168.1.1`（板子直连 PC 的场景） |
| DHCP 实测 | 板子接路由器 LAN 口、PC 走 WiFi：租到 `192.168.5.128/24`，gw/dns `192.168.5.1`，租期 43200 s |

静态 IP 仍是默认路径；DHCP/DNS 只在 W2 中做过一次性验证（驱动它的测试脚手架未进产品）。

### socket 与缓冲

- 共 **8 个 socket**，默认每个 **2 KB TX + 2 KB RX**（合计用满 16 KB + 16 KB），
  分配表在 [w5500_drv.c](../Services/devices/w5500_drv.c)
- 接入 DHCP / DNS 时**各需一个专属 socket 号**（`DHCP_init(sn, ...)` / `DNS_init(sn, ...)`），
  应用只剩 6 个。DHCP 的 socket 在租约期内常开；DNS 的号即使只在调用期间打开也必须预留 ——
  库每次进来都会强制 `socket()` 重开同号 socket
- 这两个协议对缓冲要求很低：DHCP 报文固定 **548 B**，DNS 报文 **256 B** —— 各给 1 KB
  即可，余量可留给业务 socket（W5500 的分配单位是 1 KB，最小 1 KB）

### 中断通路

- INT = **PG8 → EXTI8 下降沿**；与触摸的 PF9 共用 `EXTI9_5` 向量，由 `bsp_exti.c` 统一分发
- ISR **只置任务通知位**，不碰 SPI；读 SIR/Sn_IR、搬数据都在任务里做
- `Sn_IMR` 在 `W5500_Int_Enable()` 中对全部 8 个 socket 统一设成 `W5500_SOCK_IR_OWNED`
  （CON/DISCON/RECV）。**不能留芯片默认的 0x1F** —— 其中的 SENDOK/TIMEOUT 是 ioLibrary
  的账本，本工程不认领也不清，漏进 SIR 会让 INT 线永远低、任务 2 ms 空转
- 软复位（`wizchip_init` / DHCP 的 `default_ip_conflict`）会把 `Sn_IMR` 打回 0x1F，
  之后必须重新调用 `W5500_Int_Enable()`

## 实测数据（W5500 通路）

| 项 | 实测值 | 条件 |
| --- | --- | --- |
| 2KB SPI 读 burst | **7.6 ms** | 空载；写 burst 2.7 ms。线速只要 0.91 ms |
| 2KB SPI 读 burst（被抢 CPU） | **16.4 ms** | 同优先级任务忙等时，正好 2 倍 |
| 收发 2KB 往返 | ~5 ms | TCP，Debug `-O0` |
| TCP 握手 | 2~4 ms | 链路稳定后；刚上电可能撞对端地址探测，见本地疑难问题记录 005 |
| ICMP 往返 | 1.19 ms | PC 侧抓包测得 |
| UDP 全速收包 | **~2000 包/秒** | 上限由 SPI 吞吐决定，非调度 |
| DHCP DORA 全程 | **686 ms** | DISCOVER→OFFER 30 ms（含 20 ms 轮询粒度） |
| DHCP ACK 后的 IP 冲突检测 | **614 ms** | ARP 探测 ×3，`RTR 200 ms × RCR 3` |
| DNS 解析（A 记录） | 6~56 ms | 取决于路由器缓存；最坏阻塞约 9 s |
| 网络任务栈余量 | 266 字 / 512 | DNS 解析在库里吃栈（`parse_name` 局部数组 192 B） |

> **ioLibrary 的 DHCP/DNS 有两个必须知道的接口特性**：
> `default_ip_assign()` 只写 SIPR/SUBR/GAR，**不写 DNS** —— W5500 分支的 DNS 存在
> host 侧静态数组里，要应用侧 `wizchip_setnetinfo()` 补写；`DNS_run()` 是忙等阻塞
> （标称上限约 9 s，L2 不通时更久），且不校验应答的 ID/QR/来源，源端口固定为 0。

> `W5500_SPI_TIMEOUT_MS` 取 50 ms 就是照第一行定的（最坏值 16.4 ms 的 3 倍余量）。
> 这些数字都是 `-O0` 的 Debug 构建 —— 瓶颈是 HAL 逐字节轮询循环的 CPU 开销，
> 不是线速。根因见本地疑难问题记录 003（不入库）。

## Debug

VS Code F5 debugging via **Cortex-Debug** + **PyOCD** (`.vscode/launch.json`):

| Config                     | Description                                        |
| -------------------------- | -------------------------------------------------- |
| **Flash & Debug**    | `F5` → builds → flashes → halts at `main()` |
| **Attach (running)** | Attach to running firmware without re-flash        |

Requires VS Code extension `marus25.cortex-debug`.

For automated debugging, prefer **serial printf logging** (`/serial-debug`) — simpler and more reliable than CLI GDB for AI-assisted workflows.
