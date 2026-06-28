# ================================================================
# FreeRTOS (V11.3.0) — 实时操作系统中间件
#
# 结构:
#   include/  — 第三方头文件 (只读, 升级时直接覆盖)
#   source/   — 内核源码 (只读)
#   port/     — Cortex-M3 移植 + 堆管理
#   config/   — 项目定制配置 (FreeRTOSConfig.h)
# ================================================================

add_library(freertos OBJECT
    # ---- 内核核心 ----
    ${CMAKE_CURRENT_SOURCE_DIR}/source/croutine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/event_groups.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/stream_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/tasks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/timers.c

    # ---- Cortex-M3 移植 (GCC) ----
    ${CMAKE_CURRENT_SOURCE_DIR}/port/GCC/ARM_CM3/port.c

    # ---- 堆管理 (heap_4: 支持 free + 相邻块合并) ----
    ${CMAKE_CURRENT_SOURCE_DIR}/port/MemMang/heap_4.c
)

target_include_directories(freertos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include              # FreeRTOS.h, task.h, queue.h …
    ${CMAKE_CURRENT_SOURCE_DIR}/config               # FreeRTOSConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/port/GCC/ARM_CM3     # portmacro.h
)

# 无外部依赖 — port.c 直接操作 NVIC 寄存器, 不依赖 HAL/CMSIS 头文件路径
