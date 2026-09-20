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

# ---- BSP 头文件路径 ----
# FreeRTOSConfig.h 里的 configASSERT 收编到了 BSP 的 panic 通道
# (BSP/Inc/bsp_assert.h), 所以 freertos 需要能看见 BSP 的头文件.
#
# 用 PRIVATE: 只供 freertos 自己编译用, 不往 middleware / app 传播 ——
# 否则每个中间件都会"看得见" BSP, 破坏分层.
#
# 注: 这是本工程里 Middleware → BSP 的唯一一条【编译期】依赖边.
#     链接期的依赖 (bsp_assert.o) 由最终可执行文件统一链接, 此处无需声明.
#     port.c 本身仍然只操作 NVIC 寄存器, 不依赖 HAL/CMSIS.
target_include_directories(freertos PRIVATE ${CMAKE_SOURCE_DIR}/BSP/Inc)
