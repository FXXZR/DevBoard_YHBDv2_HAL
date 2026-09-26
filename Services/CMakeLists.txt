# ================================================================
# Services — 平台服务层
#
# 定位：介于 Application 与 BSP 之间的一层，承载【硬件抽象之上、业务逻辑之下】
#       的通用能力。
#
#   log/      分级日志 —— 基于 bsp_usart 的 TX 环形缓冲, 非阻塞
#   bus/      总线服务 —— 互斥锁、事务边界 (spi_bus)
#   devices/  设备服务 —— 器件协议驱动 (w5500_drv) + 第三方库移植层
#             (w5500_port / diskio / lvgl_port)
#
# 与其他层的关系:
#   Application ──→ Services ──→ BSP      (向上提供设备/事务 API)
#                      └──────→ Middleware (ioLibrary、FreeRTOS、FatFs、LVGL)
#
# 移植层为什么在这里 (而不在 Middleware):
#   移植层是"我们对着本工程硬件写的代码", 它【必须】调 BSP。放进 Middleware
#   会让 Middleware 反向依赖 BSP, 破坏平级关系。放在本层则全是向下依赖。
#   Middleware 只留第三方源码与配置 (ffconf.h / lv_conf.h)。
#
# 关于 RTOS 依赖:
#   本层【直接】依赖 FreeRTOS, 不做 OSAL 包装 —— 换 RTOS 时领域和需求
#   都会大变, 抽象层保不住, 与其维护一层改名转发, 不如把各模块本身做得
#   通用。约定: RTOS 依赖只允许出现在 .c 里, 头文件不得出现 FreeRTOS 类型,
#   避免向上下两个方向泄漏。
#
# 编译为 OBJECT 库, 理由同 BSP (只有被引用的函数才链接进最终 elf)。
# ================================================================

add_library(services OBJECT)

# ---- 源文件 ----
target_sources(services PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/log/log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bus/spi_bus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/w5500_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/w5500_drv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/w5500_net.c
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/diskio.c    # FatFs 平台胶水 (SD 卡)
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/lvgl_port.c # LVGL 显示/触摸挂接
)

# ---- 对外头文件路径 ----
# 按功能分子目录, 头文件与源文件同目录 (与 Middleware 的约定一致)
target_include_directories(services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/bus
    ${CMAKE_CURRENT_SOURCE_DIR}/devices
)

# ---- 依赖 ----
# bsp      : 硬件访问 (raw transfer、板级资源)
# middleware: FreeRTOS 等
target_link_libraries(services PUBLIC bsp middleware)
