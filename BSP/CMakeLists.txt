# ================================================================
# BSP (Board Support Package) 板级支持包
#
# 设计原则：
#   BSP 是整个项目中唯一依赖 STM32 HAL 的模块。
#   应用层 (main.c 等) 只调用 BSP 接口，不直接引用 HAL 头文件。
#
# 使用方式：
#   在根 CMakeLists.txt 中 add_subdirectory(BSP) 即可。
#   其他模块通过 target_link_libraries(xxx bsp) 使用 BSP。
# ================================================================

# 编译为 OBJECT 库（每个 .c → .o 独立编译，最后一起链接）
# 好处：只有被引用的函数才会链接进最终 elf，不会引入死代码
add_library(bsp OBJECT)

# BSP 源文件列表
target_sources(bsp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_systick.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_usart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_beep.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_key.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_adc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_soft_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_eeprom.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_mpu6050.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_ap3216c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_spi_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_sdcard.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_rs485.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_esp8266.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_sram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_fonts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_lcd_ili9341.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/bsp_xpt2046.c
)

# BSP 对外暴露的头文件路径（应用层需要 #include "bsp_xxx.h"）
target_include_directories(bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)

# BSP 依赖 stm32cubemx 的 HAL 头文件和宏定义
# 注意：这里用 PUBLIC，意味着链接 bsp 的目标也能自动获得 HAL 头文件路径
# 但应用层不应该直接使用 HAL API —— 这是约定，靠代码审查保证
target_link_libraries(bsp PUBLIC stm32cubemx)
