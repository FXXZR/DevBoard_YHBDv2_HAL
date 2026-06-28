# ================================================================
# Middleware — 中间件层
#
# 每个中间件自包含:
#   <Middleware>/
#     ├── CMakeLists.txt   # 独立库目标
#     ├── Source/ 或 Include/ + Source/  # 第三方源码 (只读)
#     ├── port/            # 移植胶水
#     └── config/          # 项目定制配置
#
# 新增中间件: 新建目录 → 编写 CMakeLists.txt → 在此 add_subdirectory
# ================================================================

# ---- FatFs (R0.15a) + SD Card 移植 ----
add_subdirectory(FatFs)

# ---- FreeRTOS (V11.3.0) ----
add_subdirectory(FreeRTOS)

# ---- middleware 聚合目标 (STATIC) ----
# 汇集 fatfs/freertos OBJECT 库的 .o 文件, 打包为 libmiddleware.a
# STATIC 确保 OBJECT 的 .o 被正确传播到最终可执行文件
# (INTERFACE 库无法可靠传递 OBJECT 库的目标文件)
add_library(middleware STATIC)
target_link_libraries(middleware PUBLIC fatfs freertos)
