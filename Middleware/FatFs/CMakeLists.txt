# ================================================================
# FatFs (R0.15a) — 文件系统中间件
#
# 结构:
#   Source/  — 第三方原始源码 (只读，升级时直接覆盖)
#   port/    — 移植胶水 (diskio.c — SD卡 BSP 桥接)
#   config/  — 项目定制配置 (ffconf.h)
#
#   Source/ + config/ 作为 PUBLIC include, 因为应用层需要
#   调用 FatFs API (ff.h), 而 ff.h 内部 include ffconf.h
# ================================================================

add_library(fatfs OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/diskio.c
)

target_include_directories(fatfs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source    # ff.h, diskio.h (FatFs API)
    ${CMAKE_CURRENT_SOURCE_DIR}/config    # ffconf.h (被 ff.h include)
)

target_link_libraries(fatfs PUBLIC bsp)   # diskio.c → bsp_sdcard.h
