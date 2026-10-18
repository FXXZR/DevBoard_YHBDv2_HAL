---
name: build-stm32
description: CMake + Ninja build for STM32F103ZET6 firmware project
---

# STM32 Build

Build STM32F103ZET6 firmware with CMake + Ninja + ARM GNU Toolchain.

## Debug vs Release

两套**完全独立**的构建目录，互不影响：

| | Debug（默认） | Release |
| --- | --- | --- |
| 目录 / 产物 | `build/Debug/HAL_TEST_Board.elf` | `build/Release/HAL_TEST_Board.elf` |
| 优化 | `-O0 -g3` | `-Os -g0` |
| 日志 | 全量（DEBUG 级） | 只留 WARN 及以上 |
| panic | 停机等调试器 | `NVIC_SystemReset()` 自恢复 |
| printf `%f` | 链接（约 11KB） | 不链接 |
| FLASH | ~299 KB | ~153 KB |

**默认一切操作针对 Debug。** 用户说 "release" / "发布" / "正式版" 时，
把下面命令里的 `build/Debug` → `build/Release`、`Debug` → `Release` 即可，
其余完全一致。

⚠️ Release 目录**首次使用需要先配置**（它有独立的 CMakeCache）：

```bash
cmake -B build/Release -G "Ninja" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake 2>&1
```

未配置就 `cmake --build build/Release` 会报 `build.ninja` 找不到。
（VSCode 里用 CMake 插件切换配置也能达到同样效果。）

## Decision Tree

Follow this logic to choose the right build action:

```
┌─ build/Debug/ does NOT exist? ──→ First-time configure + build
├─ CMakeLists.txt or *.cmake changed since last configure? ──→ Reconfigure + build
├─ User said "clean" / "rebuild" / "重新生成"? ──→ Clean rebuild
├─ User said "hex" / "bin" / "生成hex"? ──→ Incremental build + generate HEX/BIN
├─ .clang-format / .clangd changed? ──→ Skip (not build-related)
└─ Default ──→ Incremental build only
```

**Default**: incremental build (fast, only recompiles changed files).
**Clean rebuild**: needed when project name, toolchain, or HAL config changes.

## Pre-flight Checklist

Before building, silently verify:
1. `arm-none-eabi-gcc --version` — toolchain available
2. `cmake --version` — CMake available
3. `ninja --version` — Ninja available

If any missing, report which one and stop.

## Actions

### 1. First-time configure + build

```bash
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake 2>&1 && \
cmake --build build/Debug 2>&1
```

### 2. Incremental build (default)

```bash
cmake --build build/Debug 2>&1
```

If this fails with a CMake configure error, fall back to reconfigure + build (action 3).

### 3. Reconfigure (CMakeLists changed) then build

```bash
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake 2>&1 && \
cmake --build build/Debug 2>&1
```

### 4. Clean rebuild

```bash
rm -rf build/Debug && \
cmake -B build/Debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake 2>&1 && \
cmake --build build/Debug 2>&1
```

### 5. Build + generate HEX & BIN

Do incremental build first (action 2), then:

```bash
arm-none-eabi-objcopy -O ihex build/Debug/HAL_TEST_Board.elf build/Debug/HAL_TEST_Board.hex && \
arm-none-eabi-objcopy -O binary build/Debug/HAL_TEST_Board.elf build/Debug/HAL_TEST_Board.bin && \
echo "HEX: $(du -h build/Debug/HAL_TEST_Board.hex | cut -f1)  BIN: $(du -h build/Debug/HAL_TEST_Board.bin | cut -f1)"
```

## After Build

Report to user:
- Build result (success/fail)
- Memory usage (RAM/FLASH from linker output)
- If success and user asked for flash, offer to run `/flash-stm32`

## Error Recovery

| Symptom | Action |
|---|---|
| `ninja: error: loading 'build.ninja'`（Debug） | Build dir corrupted → clean rebuild (action 4) |
| `ninja: error: loading 'build.ninja'`（Release） | **多半是没配置过** → 先跑上面的首次配置命令 |
| `No such file: CMakeCache.txt` | Not configured → first-time configure (action 1) |
| `arm-none-eabi-gcc: command not found` | Toolchain not in PATH → report and stop |
| Linker errors | Report the undefined symbols, suggest checking CMakeLists |
| `ninja: no work to do.` | Code unchanged, nothing to compile (success) |

## Project Info

- **MCU**: STM32F103ZET6 (Cortex-M3, 512KB Flash, 64KB RAM)
- **Build dirs**: `build/Debug`（默认）/ `build/Release`
- **Output**: `build/<BuildType>/HAL_TEST_Board.elf`
- **Toolchain file**: `cmake/gcc-arm-none-eabi.cmake`
- **Compile commands**: `build/Debug/compile_commands.json` (for clangd;
  Release 那份在 `build/Release/` 下 —— clangd 默认只读 Debug 的)

## Source Tree

```
Application/     ← main.c, business logic (NO direct HAL access)
BSP/             ← bsp_led — only layer that includes HAL
Start/           ← startup, linker, syscalls, ISR stubs, hal_conf
cmake/stm32cubemx/ ← HAL + CMSIS CMake glue
Drivers/         ← ST HAL (11 modules) + CMSIS headers
```
