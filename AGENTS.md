# AGENTS.md

## Project

野火霸道 V2（STM32F103ZET6, Cortex-M3, 512KB Flash / 64KB RAM）的裸机 + FreeRTOS 工程。
工具链: arm-none-eabi-gcc 14.2 · CMake + Ninja · C11。
第三方库: FreeRTOS / FatFs / LVGL / WIZnet ioLibrary。

当前固件: `main()` 初始化 BSP → 启动日志 → 外部 SRAM → FreeRTOS; 三个任务
(`vLedTask` 心跳 / `vDispTask` LVGL + 触摸 / `vNetTask` W5500 全生命周期)。
socket 封装 (`W5500_Sock_*`) 已就绪; DHCP/DNS 未进产品路径。

**最容易搞错的架构事实 —— 分层只许向下, 不许成环:**
`Application/ → Services/ → { Middleware/, BSP/ } → Drivers/`
- `Middleware/` 是第三方库, **只读**; 移植层放 `Services/devices/`
- `BSP/` 不依赖 RTOS, 也不许用 `LOG_x`; 需要锁由 `Services/bus` 提供
- `Services/` 的头文件不得出现 RTOS 类型

## Commands

```bash
cmake --build build/Debug                      # 增量编译 (Debug 默认)
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake   # 首次配置
pyocd flash --target stm32f103ze --connect under-reset \
  -O reset_type=hw build/Debug/HAL_TEST_Board.elf         # 烧录
python tools/capture_boot.py --wait 6           # 复位 + 抓启动日志
python tools/serial_cli.py --read 5             # 读串口
```

Release 是独立 build 目录 (`build/Release`)；日志只留 WARN, **正常启动串口无输出**,
panic 走复位而不是停机。同样的流程也封装在 `.agents/skills/` (`SKILL.md`)。

Claude Code 只从 `.claude/skills/` 加载技能；首次 clone 后运行
`python tools/sync_skills.py` 生成本地链接（Windows junction / Unix symlink），
`.claude/skills/` 不入库。

## Conventions

- 错误码/状态码用 `typedef enum`; 数据类常量用 `static const`（成组时收进 `static const` 结构体）;
  对外开放的配置在 `.h` 用宏只给数值/字符串, 实现侧用 `static const <类型>` 接住;
  宏多了抽 `xxx_config.h` (只被 `.c` 包含)
- 命名: 变量/常量 `snake_case`, 类型 `CamelCase`+`_t`, 宏与枚举常量 `UPPER_SNAKE`;
  **不用 `k` 前缀 + 驼峰**
- `s_` 只表示**文件作用域可变状态**; 必须写成 `s_<模块>` (`s_usart_ctx`/`s_log`), 别把模块前缀删掉
- `static` 辅助函数一律带**模块前缀** (`eeprom_page_write()`/`ap3216c_write_reg()`), 不留光杆函数名
- `.c` 内的可变状态到**第 3 个同源变量**就收进文件作用域结构体 (`s_ctx`), 不散落一长串 `s_xxx`;
  1~2 个可以各自 `static`
- 日志用 `LOG_E/W/I/D`; `printf` 只用于临时排查
- ⚠️ Release 必须写 `LOG_LEVEL_WARN`（宏）, **不是** `LOG_LVL_WARN`（enum）—— 后者会静默关掉全部日志
- ⚠️ 日志 TX 环只有 512 B, 成组打印会静默丢行; 行间留 `vTaskDelay(20)` 或减少行数
- 串口每路只有一个使用者: USART1=日志 / USART2=rs485 / USART3=esp8266 / USART4-5 空闲
- 提交信息 `type(scope): 简述`, 正文 3~10 行
- 新模块的源文件清单加进对应层的 CMakeLists

完整规范见 `Document/conventions.md`。

## Boundaries

- 不改 `Middleware/` 下的上游代码; 定制用编译定义或外部回调
- 不改厂商 BSP 驱动的语义; 保留其来源标注
- `Document/temp/` 不入库（计划、日志、抓包）
- BSP 不得反向依赖 Services / Application

## Done means

- `cmake --build build/Debug` 通过, 且没有新增 warning
- 分层、外设或日志行为有变化时, 同步更新 `Document/` 下对应文档
- 硬件相关的结论给实测数据, 不写猜测
