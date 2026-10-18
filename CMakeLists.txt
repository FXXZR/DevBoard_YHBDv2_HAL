cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME HAL_TEST_Board)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# ================================================================
# 构建类型相关宏
#
# 必须用 add_compile_definitions (目录级) 而非 target_compile_definitions:
# 可执行目标自己没有源文件 —— 所有 .c 都在 OBJECT 库里 (bsp / app /
# middleware), 而 PRIVATE 的编译定义不会反向传播给被链接的库.
# 放在下面 add_subdirectory 之前, 才能覆盖此后创建的所有目标.
# ================================================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # panic 时停机等调试器看现场 (见 BSP/Src/bsp_assert.c)
    add_compile_definitions(BSP_ASSERT_HALT)
else()
    # 低于 WARN 的日志整条编掉 —— 连格式字符串都不进 Flash
    # (注意用 LOG_LEVEL_WARN, 不是 enum 名 LOG_LVL_WARN —— 原因见 log.h)
    add_compile_definitions(LOG_LEVEL=LOG_LEVEL_WARN)
    # 不定义 BSP_ASSERT_HALT → panic 走 NVIC_SystemReset() 自恢复
endif()

# 缩短 __FILE__ 与调试信息里的绝对路径 (断言和日志都会用到 __FILE__)
#   ${CMAKE_SOURCE_DIR}/BSP/Src/bsp_spi.c   →   BSP/Src/bsp_spi.c
# 每个断言点省约 50 字节; GCC 8+ / AC6 (armclang) 支持, AC5 不支持.
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=)

# ================================================================
# 芯片厂商驱动层 (HAL + CMSIS)
# ================================================================
add_subdirectory(cmake/stm32cubemx)

# ================================================================
# Start — 启动文件 + 系统初始化 + HAL 适配层
#   startup_xxx.s / linker script / system_xxx.c / syscalls / MSP / IT
# ================================================================
add_subdirectory(Start)

# ================================================================
# BSP (Board Support Package) — 板级支持包
#   唯一直接依赖 HAL 的模块；应用层不直接使用 HAL API
# ================================================================
add_subdirectory(BSP)

# ================================================================
# Middleware — 中间件 (ThirdParty 只读 + Port 移植胶水)
# ================================================================
add_subdirectory(Middleware)

# ================================================================
# Services — 平台服务层
#   总线服务 (锁/事务) + 设备服务 (器件的协议驱动)
#   位于 BSP 之上、Application 之下; 依赖 BSP 与 Middleware,
#   所以必须排在两者之后。
# ================================================================
add_subdirectory(Services)

# ================================================================
# Application — 应用层（业务逻辑）
# ================================================================
add_subdirectory(Application)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    # (BSP 源文件已通过 BSP/CMakeLists.txt 添加)
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    # (BSP 头文件路径已通过 BSP/CMakeLists.txt 对外暴露)
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
# 链接: HAL/Start → middleware (聚合 fatfs/freertos...) → BSP → App
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    STM32_Drivers
    start
    middleware
    bsp
    services
    app

    # Add user defined libraries
)

# Debug: 启用 printf/scanf 浮点格式化 (newlib-nano 默认关闭)
# Release: 不启用 — 正式固件通常关闭 printf 或只用整数格式
# -Wl,-u,_printf_float, 让 newlib-nano 支持 %f, 实测约 11KB FLASH.
# -Wl,-u,_scanf_float, 实测约 7KB，工程里没有任何地方用到，暂时屏蔽

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        -Wl,-u,_printf_float
        # -Wl,-u,_scanf_float
    )
endif()
