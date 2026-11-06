# -----------------------------------------------------------------------------
# src/ 子目录构建入口
#
# 每个 add_subdirectory 一个模块边界；分组目录（runtime/、components/、
# base/、inference/、frame/）不产生 target，因此不需要各自的 CMakeLists。
# -----------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(app)
