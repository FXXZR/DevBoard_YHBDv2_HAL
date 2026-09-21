# ================================================================
# Services — 平台服务层
#
# 定位：介于 Application 与 BSP 之间的一层，承载【硬件抽象之上、业务逻辑之下】
#       的通用能力。
#
#   bus/      总线服务 —— 互斥锁、事务边界 (spi_bus)
#   devices/  设备服务 —— 具体器件的协议驱动 (w5500_drv, spi_flash_drv)
#
# 与其他层的关系:
#   Application ──→ Services ──→ BSP      (向上提供设备/事务 API)
#                      └──────→ Middleware (ioLibrary、FreeRTOS)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/bus/spi_bus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/w5500_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/w5500_drv.c
)

# ---- 对外头文件路径 ----
# 按功能分子目录, 头文件与源文件同目录 (与 Middleware 的约定一致)
target_include_directories(services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/bus
    ${CMAKE_CURRENT_SOURCE_DIR}/devices
)

# ---- 依赖 ----
# bsp      : 硬件访问 (raw transfer、板级资源)
# middleware: FreeRTOS 等
target_link_libraries(services PUBLIC bsp middleware)
