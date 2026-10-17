# ================================================================
# FatFs (R0.15a) — 文件系统中间件
#
# 结构:
#   Source/  — 第三方原始源码 (只读，升级时直接覆盖)
#   config/  — 项目定制配置 (ffconf.h)
#
#   移植层 diskio.c 不在这里 —— 它调 BSP, 属于 Services/devices/。
#   本目标因此不依赖任何东西, 两个 Middleware 目录保持平级。
#
#   Source/ + config/ 作为 PUBLIC include, 因为应用层需要
#   调用 FatFs API (ff.h), 而 ff.h 内部 include ffconf.h
# ================================================================

add_library(fatfs OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ff.c
)

target_include_directories(fatfs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source    # ff.h, diskio.h (FatFs API)
    ${CMAKE_CURRENT_SOURCE_DIR}/config    # ffconf.h (被 ff.h include)
)
