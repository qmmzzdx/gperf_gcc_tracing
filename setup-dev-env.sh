#!/bin/bash
# setup-dev-env.sh - 在 Ubuntu 22.04 中安装 GCC 插件开发环境

set -e  # 遇到错误立即退出

echo "======================================="
echo "开始安装 GCC 插件开发环境"
echo "======================================="

# 1. 系统更新
echo "步骤 1/3: 更新系统包列表和已安装的包..."
sudo apt-get update
sudo apt-get upgrade -y

# 2. 安装开发工具
echo "步骤 2/3: 安装开发工具..."
sudo apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    gcc-11 \
    g++-11 \
    gcc-11-plugin-dev \
    vim \
    net-tools \
    git \
    wget \
    curl \
    ca-certificates

# 3. 清理安装缓存
echo "步骤 3/3: 清理安装缓存..."
sudo apt-get autoremove -y
sudo apt-get clean
sudo rm -rf /var/lib/apt/lists/*

# 4. 设置 GCC 11 为默认（可选）
echo "设置 GCC 11 为默认编译器..."
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100

echo "======================================="
echo "安装完成！"
echo "======================================="

# 验证安装
echo "验证安装结果："
echo "1. GCC 版本:"
gcc --version | head -n 1
echo ""
echo "2. GCC 插件头文件位置:"
find /usr -name "gcc-plugin.h" 2>/dev/null | head -3
echo ""
echo "3. GCC 插件目录:"
gcc -print-file-name=plugin
echo ""
echo "环境安装完成"
