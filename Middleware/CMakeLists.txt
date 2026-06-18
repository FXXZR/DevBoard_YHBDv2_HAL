# ================================================================
# Middleware — 中间件层
#
# 结构:
#   ThirdParty/ — 第三方原始源码 (只读，升级时直接覆盖)
#   Port/       — 移植胶水 + 配置头文件 (我们编写)
#
# 设计:
#   - 第三方源码与其移植胶水编译进同一个库目标 (同目录内为一个目标)
#   - 只有独立的中间件 (如 FreeRTOS) 才另起新目标
#   - 随项目扩大可抽取 .cmake 模块
# ================================================================

# ---- FatFS (R0.15a) + SD Card 移植 ----
add_library(middleware STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/FatFs/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/FatFs/diskio.c
)
target_include_directories(middleware PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/FatFs    # ff.h, diskio.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Port/FatFs          # ffconf.h
)
target_link_libraries(middleware PUBLIC bsp)         # diskio.c → bsp_sdcard.h

# ---- FreeRTOS (后续添加) ----
# add_library(freertos STATIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/FreeRTOS-Kernel/tasks.c
#     ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/FreeRTOS-Kernel/queue.c
#     ...
#     ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c
#     ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/FreeRTOS-Kernel/portable/MemMang/heap_4.c
# )
# target_include_directories(freertos PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/FreeRTOS-Kernel/include
#     ${CMAKE_CURRENT_SOURCE_DIR}/Port/FreeRTOS          # FreeRTOSConfig.h
# )
