# ================================================================
# Application — 应用层
#
# 目录约定:
#   main.c              程序入口
#   Common/              公共类型、宏、数据结构定义
#   Modules/             独立硬件模块（每个模块一个子目录）
#   DataManager/         全局数据管理
#   Communication/       通信协议栈
#   Tasks/               RTOS 任务组织
#
# 依赖: middleware (ff.h/ffconf.h 等) + bsp (板级支持包)
#       应用层不直接使用 HAL API —— 通过 BSP 访问硬件
#       middleware 封装了所有第三方库, app 不需要逐个链接
# ================================================================

add_library(app OBJECT)

# ---- 入口文件 + 测试模块 ----
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_beep_key.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_key.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_adc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_eeprom.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_spi_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_sdcard_fatfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_rs485.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_esp8266.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Test/test_dma_echo.c

    # 后续新增应用模块在这里添加，例如:
    # ${CMAKE_CURRENT_SOURCE_DIR}/Common/app_data_model.c
    # ${CMAKE_CURRENT_SOURCE_DIR}/Modules/SensorA/sensorA_drv.c
    # ${CMAKE_CURRENT_SOURCE_DIR}/Tasks/task_sensors.c
)

# ---- 头文件路径 ----
target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}            # 根: main.c 所在
    ${CMAKE_CURRENT_SOURCE_DIR}/Common     # 公共头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/Test       # 测试模块头文件
    # 后续:
    # ${CMAKE_CURRENT_SOURCE_DIR}/Modules
    # ${CMAKE_CURRENT_SOURCE_DIR}/DataManager
)

# ---- 依赖 ----
# app 通过 middleware 获得 ff.h 等头文件; 通过 bsp 获得硬件抽象
target_link_libraries(app PUBLIC middleware bsp)
