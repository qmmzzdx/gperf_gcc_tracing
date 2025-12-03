#!/bin/bash
# build-test.sh - 构建并测试GCC插件
set -e  # 遇到错误退出

echo "========================================"
echo "GCC插件构建与测试"
echo "========================================"

# 清理旧构建
echo "1. 清理旧构建文件..."
rm -rf build

# 创建构建目录
echo "2. 创建构建目录..."
mkdir -p build && cd build

# 运行CMake配置
echo "3. 运行CMake配置..."
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 编译项目
echo "4. 编译插件和测试程序..."
make -j$(nproc)

# 检查生成的文件
echo "5. 检查生成的文件..."
echo "已生成:"
ls -la gperf.so 2>/dev/null && echo "  ✓ gperf.so (插件)"
ls -la test/test 2>/dev/null && echo "  ✓ test/test (测试程序)"

# 运行测试
echo "6. 运行测试程序..."
if [ -f "test/test" ]; then
    cd test
    echo "开始执行测试程序..."
    ./test
    echo "测试程序执行完成！"
    
    # 检查追踪文件
    if [ -f "trace.json" ]; then
        echo "追踪文件生成在: $(pwd)/trace.json"
        echo "文件大小: $(du -h trace.json | cut -f1)"
    else
        echo "警告: 未找到trace.json文件"
    fi
else
    echo "错误: 测试程序未生成"
    exit 1
fi

echo "========================================"
echo "构建测试完成！"
echo "========================================"
