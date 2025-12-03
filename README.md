# GCC 编译过程追踪插件 (GPERF)

## 📖 项目概述

**GPERF** 是一个专业的 GCC 插件，用于深度追踪和可视化 C/C++ 编译过程。通过插入探针到 GCC 编译器的关键位置，本插件能够捕获从预处理到优化完成的完整编译流水线，并生成 **Chrome Tracing** 格式的性能数据，用于在 Perfetto UI 中进行可视化分析。

## 🎯 核心价值

- **编译器开发者**: 深入理解 GCC 内部优化流水线和代码生成过程
- **C++开发者**: 量化头文件包含、模板实例化、宏展开等编译开销
- **性能工程师**: 识别编译瓶颈，优化大型项目的构建时间
- **教育研究**: 学习现代编译器内部工作机制和优化策略

## 🏗️ 架构设计

### 模块分层架构

```
GPERF 插件架构
├── 应用层 (Application Layer)
│   ├── Chrome Tracing UI
│   ├── Perfetto UI
│   └── 自定义分析工具
│
├── 输出层 (Output Layer)
│   ├── JSON序列化 (perf_output.cpp)
│   ├── 文件系统管理
│   ├── 时间戳转换 (ns → μs)
│   └── 事件过滤 (>1ms)
│
├── 插件接口层 (Plugin API Layer)
│   ├── 函数追踪 (tracking.cpp)
│   ├── 预处理追踪 (tracking.cpp)
│   ├── 优化Pass追踪 (tracking.cpp)
│   └── 作用域追踪 (tracking.cpp)
│
├── GCC插件框架层 (GCC Plugin Framework)
│   ├── 回调注册 (plugin.cpp)
│   ├── AST遍历 (plugin.cpp)
│   ├── 树节点操作 (plugin.cpp)
│   └── 预处理状态机 (plugin.cpp)
│
└── GCC编译器核心 (GCC Compiler Core)
    ├── 前端解析 (词法/语法分析)
    ├── 中间优化 (GIMPLE/RTL)
    ├── 后端生成 (汇编/机器码)
    └── 链接器 (ld)
```

### 核心模块说明

核心模块说明可查看[gcc_trace_description.md](./gcc_trace_description.md)

## 🔧 安装与使用

### 环境要求

- **GCC 11.0+** (支持插件 API)
- **CMake 3.15+**
- **Linux/Unix 环境** (支持 GCC 插件开发)
- **C++20 标准库**

### 快速开始

```bash
# 1. 设置开发环境
chmod +x setup-dev-env.sh
./setup-dev-env.sh

# 2. 编译与测试
chmod +x build-test.sh
./build-test.sh
```

## 📊 追踪事件类型

插件追踪 8 类编译事件，每类在 Chrome Tracing 中有不同颜色：

| 事件类别 | 示例 | 对应 GCC 内部阶段 | 可视化颜色 |
|---------|------|------------------|-----------|
| **TU** | 整个编译单元 | 翻译单元处理 | 灰色 |
| **PREPROCESS** | `#include <iostream>` | 预处理/宏展开 | 蓝色 |
| **FUNCTION** | `std::vector::push_back()` | 函数解析/实例化 | 绿色 |
| **STRUCT** | `class MyTemplate<T>` | 类/结构体定义 | 黄色 |
| **NAMESPACE** | `namespace std` | 命名空间处理 | 橙色 |
| **GIMPLE_PASS** | `*build_cgraph_edges` | GIMPLE 优化 | 紫色 |
| **RTL_PASS** | `ira`, `reload` | 寄存器分配优化 | 红色 |
| **SIMPLE_IPA_PASS** | `simdclone` | 过程间分析 | 青色 |

## 🎨 可视化分析

### trace.json 示例

Chrome Tracing JSON 格式：

```json
{
  "displayTimeUnit": "ns",
  "traceEvents": [
    {
      "cat": "PREPROCESS",
      "name": "iostream",
      "pid": 6727,
      "ts": 5602.81,
      "tid": 0,
      "args": {"UID": 29},
      "ph": "B"
    },
    {
      "cat": "PREPROCESS", 
      "name": "iostream",
      "pid": 6727,
      "ts": 33757,
      "tid": 0,
      "args": {"UID": 29},
      "ph": "E"
    }
  ],
  "beginningOfTime": 1764746506379873
}
```

### 使用 Perfetto UI

```bash
# 在线工具：https://ui.perfetto.dev
# 1. 上传 trace.json
# 2. 更强大的筛选和统计功能
```

### 追踪文件示例解读

```json
{
  "cat": "PREPROCESS",
  "name": "/usr/include/c++/11/iostream",
  "ts": 5602.81,
  "dur": 28154.19,  // 处理 iostream 耗时 28.15µs
  "ph": "X"
}
```

**分析要点**:
1. **预处理耗时**: `iostream` 通常是最耗时的头文件（~28µs）
2. **模板膨胀**: 查看 `vector`、`string` 等模板类的实例化时间
3. **优化效果**: GIMPLE/RTL Pass 执行时间反映优化开销

## 🧪 测试套件设计

项目包含完整的测试用例 (`test/test.cpp`)，验证插件的全面追踪能力：

```cpp
// 测试覆盖的 C++ 特性：
1. ✅ 基础包含：<iostream>, <vector>, <string> 等
2. ✅ 宏系统：嵌套宏、变参宏、条件编译
3. ✅ 命名空间：嵌套、匿名、using 声明
4. ✅ 类层次：虚函数、继承、模板类
5. ✅ 模板特性：可变参数、概念约束 (C++20)
6. ✅ 编译期计算：constexpr 函数、编译期字符串
7. ✅ Lambda：泛型 Lambda、捕获列表、立即调用
8. ⚡ 内联汇编：x86_64 平台特定（可选）
```

## 🔍 关键技术细节

### 1. 时间系统设计

```cpp
// 高精度时间基准
using clock_t = std::chrono::high_resolution_clock;
time_point_t COMPILATION_START;  // 编译开始的绝对时间点

// 获取相对时间戳（纳秒）
inline TimeStamp ns_from_start() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock_t::now() - COMPILATION_START
    ).count();
}
```

**时间对齐策略**:
- 函数事件: `+3ns` 避免与预处理事件重叠
- 作用域事件: `-1ns` 开始，`+1ns` 结束，确保包含关系
- Pass 事件: `+1ns` 避免连续 Pass 时间戳相同

### 2. 文件名规范化

```cpp
// 将 /usr/include/c++/11/iostream 转换为 iostream
const char* normalized_file_name(const char* file_name) {
    if (无冲突) {
        return 相对路径;  // e.g., "iostream"
    } else {
        return 原始路径;   // e.g., "/usr/include/c++/11/iostream"
    }
}
```

**冲突处理**:
- 检测多个目录中的同名文件
- 有冲突时保留完整路径避免歧义
- 记录冲突集合用于后续分析

### 3. 循环包含检测

```cpp
// 检测到循环包含时使用特殊标记
if (文件已在栈中且未结束) {
    file_name = "CIRCULAR_POISON_VALUE";  // 毒丸值
    // 不追踪内层循环，避免无限递归
}
```

## 🚀 性能优化技巧

### 编译时优化建议

1. **减少头文件依赖**
   ```cpp
   // ❌ 避免
   #include <iostream>  // 28µs 开销
   
   // ✅ 推荐（如果可能）
   #include <cstdio>    // 更轻量
   ```

2. **前向声明替代包含**
   ```cpp
   // 在头文件中
   class MyClass;  // 前向声明
   // 代替 #include "MyClass.h"
   ```

3. **显式模板实例化**
   ```cpp
   // 减少编译单元内的模板实例化
   extern template class std::vector<int>;
   ```

### 插件使用建议

1. **生产环境采样**
   ```bash
   # 只追踪关键文件
   g++ -fplugin=... -fplugin-arg-gperf-trace=critical.json critical.cpp
   ```

2. **对比分析**
   ```bash
   # 生成优化前后的对比
   g++ -O0 -fplugin=... -o trace_O0.json
   g++ -O3 -fplugin=... -o trace_O3.json
   ```

## 🐛 故障排除

### 常见问题

1. **插件加载失败**
   ```bash
   # 检查 GCC 版本兼容性
   gcc --version
   # 确保 gcc-plugin-dev 包已安装
   ```

2. **无输出文件**
   ```bash
   # 检查文件权限
   ls -la trace.json
   # 启用详细日志
   export GCC_DEBUG_PLUGIN=1
   ```

3. **Chrome Tracing 无法解析**
   ```bash
   # 验证 JSON 格式
   python -m json.tool trace.json > /dev/null && echo "Valid JSON"
   ```

### 调试模式

```bash
# 启用 GCC 调试输出
gcc -fplugin=./gperf.so -v source.cpp

# 使用 GDB 调试插件
gdb --args gcc -fplugin=./gperf.so source.cpp
```

## 📈 性能数据示例

基于测试文件的实际追踪数据：

| 阶段 | 平均耗时 | 说明 |
|------|---------|------|
| **整体编译** | 0.54ms | 小型测试文件的完整编译 |
| **预处理** | 0.12ms | 占编译时间的 22% |
| **iostream** | 28.2µs | 最重的头文件 |
| **函数解析** | 2-5µs/个 | 标准库函数实例化 |
| **优化 Pass** | 1-10µs/个 | GCC 内部优化开销 |

**提示**: 本插件专为 GCC 编译器设计，强烈依赖 GCC 内部 API。建议在生产环境中使用前进行全面测试。

**性能提示**: 追踪会增加约 5-15% 的编译开销，建议在需要分析时启用。



