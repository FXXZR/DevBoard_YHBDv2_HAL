cmake_minimum_required(VERSION 3.12.4)

# Middleware/lvgl-8.3.11/CMakeLists.txt
# LVGL v8.3.11 Graphics Library

# OBJECT 库 —— 与 BSP 统一风格
add_library(lvgl OBJECT)

# 递归收集 src/ 下所有 .c (GPU 后端等会被 lv_conf.h 的宏禁掉, 生成空 .o)
file(GLOB_RECURSE LVGL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
file(GLOB LVGL_PORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/port/*.c)
target_sources(lvgl PRIVATE ${LVGL_SOURCES} ${LVGL_PORT_SOURCES})

# 头文件路径: 让编译器能找到 lvgl.h / lv_conf.h / lv_port_disp.h / bsp_lcd.h
target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/port
    ${CMAKE_SOURCE_DIR}/BSP/Inc
)
# 编译宏: 告诉 LVGL 用简化 include 路径
target_compile_definitions(lvgl PUBLIC
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

