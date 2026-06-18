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
# 链接: HAL/Start → Middleware → BSP → App
#   (middleware 封装了所有中间件, 可执行文件不直接链接内部的 fatfs/mw_port)
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    STM32_Drivers
    start
    middleware
    bsp
    app

    # Add user defined libraries
)
