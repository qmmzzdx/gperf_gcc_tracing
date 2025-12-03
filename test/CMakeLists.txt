# 创建测试可执行文件
add_executable(test test.cpp)

# 设置编译选项
target_compile_options(test PRIVATE
    "-fplugin=${CMAKE_BINARY_DIR}/gperf.so"
    "-fplugin-arg-gperf-trace=${CMAKE_CURRENT_BINARY_DIR}/trace.json"
    "-std=c++20"
    "-g"  # 添加调试信息
)

# 可选：编译后显示信息
add_custom_command(TARGET test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Trace file: ${CMAKE_CURRENT_BINARY_DIR}/trace.json"
)
