cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)

# ================================================================
# STM32CubeMX 生成符号 (宏定义)
# ================================================================
set(MX_Defines_Syms
	USE_HAL_DRIVER
	STM32F103xE
    $<$<CONFIG:Debug>:DEBUG>
)

# ================================================================
# HAL/Drivers 头文件路径
# ================================================================
set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Start
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32F1xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
)

# ================================================================
# STM32 HAL/LL 驱动源文件列表
#
# 启用规则 (两步):
#   1. Start/stm32f1xx_hal_conf.h → 取消注释/添加 HAL_xxx_MODULE_ENABLED
#   2. 下方 → 取消注释对应源文件（含 _ex 配套文件）
#
# 分组: 核心模块 | 通信 | 定时器 | 模拟 | 存储 | USB | 其他
# ================================================================

# ---- 核心 (始终启用) ----
set(STM32_Drivers_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c

    # ---- ADC (启用 HAL_ADC_MODULE_ENABLED 后取消注释) ----
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc_ex.c

    # ---- UART (启用 HAL_UART_MODULE_ENABLED 后取消注释) ----
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c

    # ---- RTC (启用 HAL_RTC_MODULE_ENABLED 后取消注释) ----
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rtc_ex.c

    # ---- SPI (启用 HAL_SPI_MODULE_ENABLED 后取消注释) ----
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c

    # ---- I2C (启用 HAL_I2C_MODULE_ENABLED 后取消注释) ----
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_i2c.c
)

# 注意: 以下 HAL 模块源文件按需启用。
# 每个模块基文件与其 _ex 扩展文件配对，需同时取消注释。

# ---- USART (启用 HAL_USART_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_usart.c

# ---- I2S (启用 HAL_I2S_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_i2s.c

# ---- CAN (启用 HAL_CAN_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_can.c

# ---- CEC (启用 HAL_CEC_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cec.c

# ---- ETH (启用 HAL_ETH_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_eth.c

# ---- IRDA (启用 HAL_IRDA_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_irda.c

# ---- SMARTCARD (启用 HAL_SMARTCARD_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_smartcard.c

# ================================================================
# 定时器
# ================================================================

# ---- TIM (启用 HAL_TIM_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c

# RTC 已在上方 set(STM32_Drivers_Src ...) 中启用

# ---- IWDG (启用 HAL_IWDG_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_iwdg.c

# ---- WWDG (启用 HAL_WWDG_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_wwdg.c

# ================================================================
# 模拟
# ================================================================

# ---- DAC (启用 HAL_DAC_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dac.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dac_ex.c

# ================================================================
# 存储
# ================================================================

# ---- CRC (启用 HAL_CRC_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_crc.c

# ---- SD (启用 HAL_SD_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_sd.c

# ---- MMC (启用 HAL_MMC_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_mmc.c

# ---- SRAM (启用 HAL_SRAM_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_sram.c

# ---- NOR (启用 HAL_NOR_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_nor.c

# ---- NAND (启用 HAL_NAND_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_nand.c

# ---- PCCARD (启用 HAL_PCCARD_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pccard.c

# ================================================================
# USB
# ================================================================

# ---- PCD (启用 HAL_PCD_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pcd.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pcd_ex.c

# ---- HCD (启用 HAL_HCD_MODULE_ENABLED 后取消注释) ----
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_hcd.c

# ================================================================
# LL 驱动 (独立于 HAL, 需按需启用对应 LL_xxx 模块)
# ================================================================
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_adc.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_crc.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_dac.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_dma.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_exti.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_fsmc.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_gpio.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_i2c.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_pwr.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_rcc.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_rtc.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_sdmmc.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_spi.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_tim.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_usart.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_usb.c
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_utils.c

# ================================================================
# Legacy CAN (启用 HAL_CAN_LEGACY_MODULE_ENABLED 后取消注释)
# ================================================================
# ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/Legacy/stm32f1xx_hal_can.c


# Drivers Midllewares



# Link directories setup
set(MX_LINK_DIRS

)
# Project static libraries
set(MX_LINK_LIBS
    STM32_Drivers
    ${TOOLCHAIN_LINK_LIBRARIES}

)
# Interface library for includes and symbols
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# Create STM32_Drivers static library
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)


# (应用层源码已移至Start/ CMakeLists.txt)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_DIRS})

# Add libraries to the project
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
