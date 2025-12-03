#pragma once                // 头文件保护，防止重复包含

#include <chrono>           // 高精度时间库
#include <optional>         // 可选类型（C++17）
#include <string>           // 字符串
#include <unordered_map>    // 哈希表
#include <unordered_set>    // 哈希集合

namespace GccTrace // 项目核心命名空间
{
    // 类型别名模板，提供统一的容器类型定义
    // 便于后续更换容器实现（如从unordered_map换成map）
    template <class Key, class Value>
    using map_t = std::unordered_map<Key, Value>;

    template <class Value>
    using set_t = std::unordered_set<Value>;

    // 时间系统类型定义
    using clock_t = std::chrono::high_resolution_clock;     // 高精度时钟
    using time_point_t = std::chrono::time_point<clock_t>;  // 时间点类型
    extern time_point_t COMPILATION_START;                  // 编译开始时间（全局变量，在tracking.cpp中定义）
    using TimeStamp = int64_t;                              // 时间戳类型（纳秒）

    // ============== 核心数据结构定义 ==============

    // 时间跨度结构，表示事件的开始和结束时间
    // 时间单位：纳秒，相对于COMPILATION_START的偏移量
    struct TimeSpan
    {
        int64_t start;  // 开始时间戳（纳秒）
        int64_t end;    // 结束时间戳（纳秒）
    };

    // 获取当前时间相对于编译开始的纳秒偏移量
    // inline函数：在头文件中定义，避免链接错误
    inline TimeStamp ns_from_start()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock_t::now() - COMPILATION_START  // 当前时间减去编译开始时间
        ).count();  // 转换为纳秒计数
    }

    // 事件类别枚举，决定在Chrome Tracing中的颜色和分组
    enum EventCategory
    {
        TU,                 // Translation Unit：整个编译单元
        PREPROCESS,         // 预处理阶段（文件包含、宏展开）
        FUNCTION,           // 函数解析
        STRUCT,             // 结构体/类定义（包括union）
        NAMESPACE,          // 命名空间
        GIMPLE_PASS,        // GIMPLE中间表示优化pass
        RTL_PASS,           // RTL（寄存器传输级）优化pass
        SIMPLE_IPA_PASS,    // 简单过程间分析pass
        IPA_PASS,           // 完整过程间分析pass
        UNKNOWN             // 未知类型（默认/错误处理）
    };

    // 追踪事件基本单元，对应Chrome Tracing中的一个事件
    struct TraceEvent
    {
        const char* name;                                    // 事件名称（函数名、文件名、pass名等）
        EventCategory category;                              // 事件类别
        TimeSpan ts;                                         // 时间跨度
        std::optional<map_t<std::string, std::string>> args; // 可选参数键值对
        // 使用optional：某些事件可能没有额外参数
        // 使用const char*而非string：避免拷贝开销，事件名称通常是字符串字面量
    };

    // 已解析完成的函数信息结构
    // 从GCC回调函数传递到追踪系统的数据结构
    struct FinishedFunction
    {
        void* decl;               // GCC的tree节点指针（类型擦除为void*）
        const char* name;         // 函数签名（如"my_namespace::MyClass::func(int)"）
        const char* file_name;    // 定义所在的源文件
        const char* scope_name;   // 所属作用域名称（命名空间或类名）
        EventCategory scope_type; // 作用域类型（NAMESPACE或STRUCT）
    };
}  // namespace GccTrace
