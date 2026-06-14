# ================================================================
# Start — 启动 + 系统初始化 + HAL 适配层
#
# 包含:
#   - startup_xxx.s     MCU 启动汇编（向量表、复位处理）
#   - linker script     链接脚本（在工具链 cmake 中指定）
#   - system_xxx.c      CMSIS 系统初始化（SystemInit）
#   - syscalls/sysmem   标准库桩函数（_write, _sbrk 等）
#   - stm32f1xx_it.c    HAL 中断服务例程
#   - stm32f1xx_hal_msp.c  HAL 底层硬件初始化（MSP）
#
# 依赖: stm32cubemx (HAL 宏定义 + 头文件路径)
# ================================================================

add_library(start OBJECT)

target_sources(start PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/startup_stm32f103xe.s
    ${CMAKE_CURRENT_SOURCE_DIR}/system_stm32f1xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32f1xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32f1xx_hal_msp.c
)

# 对外暴露本目录头文件（stm32f1xx_it.h 供 HAL 使用）
target_include_directories(start PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 依赖 HAL 头文件和宏定义
target_link_libraries(start PUBLIC stm32cubemx)
