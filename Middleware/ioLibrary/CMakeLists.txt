# ================================================================
# ioLibrary_Driver (WIZnet) — W5500 硬件 TCP/IP 协议栈
#
# 来源: https://github.com/Wiznet/ioLibrary_Driver
# 许可: MIT (见 license.txt) —— 可自由使用/修改/再分发, 保留版权声明即可
#
# 结构保持上游原样, 便于将来整体覆盖更新:
#   Ethernet/        芯片驱动 (W5500/) + BSD Socket API (socket.c)
#                    + 移植层 (wizchip_conf.c/h, 回调注册在这里)
#   Internet/        应用协议 (DHCP / DNS)
#
# ⚠️ 本目录是【只读】的第三方代码 —— 不要改上游文件, 否则下次更新
#    会冲突且难追溯。需要定制的一律用编译定义或外部回调 (见下)。
#    详见 README.md。
# ================================================================

add_library(iolibrary OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/Ethernet/wizchip_conf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Ethernet/socket.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Ethernet/W5500/w5500.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Internet/DHCP/dhcp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Internet/DNS/dns.c
)

target_include_directories(iolibrary PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Ethernet
    ${CMAKE_CURRENT_SOURCE_DIR}/Ethernet/W5500
    ${CMAKE_CURRENT_SOURCE_DIR}/Internet/DHCP
    ${CMAKE_CURRENT_SOURCE_DIR}/Internet/DNS
)

# ---- 芯片型号与接口模式 (覆盖上游默认值) ----
#
# wizchip_conf.h 里 _WIZCHIP_ 与 _WIZCHIP_IO_MODE_ 都用 #ifndef 守卫,
# 所以在这里定义【不会】和上游冲突 —— 不需要为了选型去改上游文件。
#
# 上游默认是 _WIZCHIP_ = W6300, 不覆盖的话会编译错误或行为完全不对。
#
#   _WIZCHIP_IO_MODE_SPI_VDM_ = 变长数据模式 (控制字节 OM[1:0] = 00),
#   与野火例程以及本工程实测通过的帧格式一致。
target_compile_definitions(iolibrary PUBLIC
    _WIZCHIP_=W5500
    _WIZCHIP_IO_MODE_=_WIZCHIP_IO_MODE_SPI_VDM_
)

# 无外部依赖 —— 它通过注册进来的回调访问硬件, 自身不认识 SPI/GPIO。
# 回调实现在 Services/devices/w5500_port.c。
