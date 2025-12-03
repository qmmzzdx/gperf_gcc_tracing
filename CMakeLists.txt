# ==================== 项目基础配置 ====================
cmake_minimum_required(VERSION 3.15)
project(gperf
    VERSION 1.0.0
    DESCRIPTION "GCC Performance Tracing Plugin"
    LANGUAGES CXX
)

# ==================== 项目选项配置 ====================
option(GPERF_BUILD_TEST "Build the compiler plugin test" ON)
option(GPERF_ENABLE_WERROR "Treat warnings as errors" OFF)
option(GPERF_BUILD_EXAMPLES "Build usage examples" OFF)

# ==================== 编译器检测和配置 ====================
# 检查编译器
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "This plugin is designed for GCC. Using ${CMAKE_CXX_COMPILER_ID} may not work properly.")
endif()

# 检查GCC版本（需要支持插件和C++20）
execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
    OUTPUT_VARIABLE GCC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Using GCC version: ${GCC_VERSION}")

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 不使用GNU扩展

# ==================== GCC插件头文件检测 ====================
# 查找GCC插件头文件目录
if(NOT GPERF_GCC_PLUGIN_DIR)
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=plugin
        OUTPUT_VARIABLE GPERF_GCC_PLUGIN_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# 验证插件头文件是否存在
if(NOT EXISTS ${GPERF_GCC_PLUGIN_DIR}/include/gcc-plugin.h)
    message(FATAL_ERROR 
        "gcc-plugin.h not found under ${GPERF_GCC_PLUGIN_DIR}/include\n"
        "You may need to install gcc-plugin-dev package"
    )
endif()

message(STATUS "GCC plugin headers found in: ${GPERF_GCC_PLUGIN_DIR}")

# ==================== 插件库构建 ====================
# 源文件列表
set(GPERF_SOURCES
    src/plugin.cpp
    src/tracking.cpp
    src/perf_output.cpp
)

# 创建共享库（GCC插件）
add_library(gperf SHARED ${GPERF_SOURCES})

# 设置目标属性
set_target_properties(gperf PROPERTIES
    OUTPUT_NAME "gperf"      # 输出名为 gperf.so（而不是 libgperf.so）
    PREFIX ""                # 无前缀
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# 包含目录配置
target_include_directories(gperf 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${GPERF_GCC_PLUGIN_DIR}/include
)

# 编译选项配置
target_compile_options(gperf PRIVATE
    -fno-rtti                # 禁用RTTI（GCC插件要求）
    -Wall                    # 所有警告
    -Wextra                  # 额外警告
    -Wpedantic               # 严格ISO C++警告
    -g                       # 调试信息
)

# 可选：将警告视为错误
if(GPERF_ENABLE_WERROR)
    target_compile_options(gperf PRIVATE -Werror)
endif()

# 链接选项
target_link_options(gperf PRIVATE
    -shared                  # 明确指定生成共享库
    -fPIC                    # 位置无关代码
)

# ==================== 安装配置 ====================
# 安装插件库到GCC插件目录
install(TARGETS gperf
    DESTINATION ${GPERF_GCC_PLUGIN_DIR}
)

# 安装头文件
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION include/gperf
    FILES_MATCHING PATTERN "*.h"
)

# 创建导出配置（便于其他项目使用）
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/gperfConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# ==================== 测试构建 ====================
if(GPERF_BUILD_TEST)
    add_subdirectory(test)
    # 确保测试在插件之后构建
    if(TARGET test)
        add_dependencies(test gperf)
    endif()
endif()

# ==================== 示例构建 ====================
if(GPERF_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ==================== 输出信息 ====================
message(STATUS "=========================================")
message(STATUS "gperf - GCC Performance Tracing Plugin")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "GCC plugin dir: ${GPERF_GCC_PLUGIN_DIR}")
message(STATUS "Build tests: ${GPERF_BUILD_TEST}")
message(STATUS "=========================================")
