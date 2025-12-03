// GCC性能追踪插件的JSON输出模块
// 负责将收集到的编译事件转换为Chrome Tracing格式的JSON文件

#include "perf_output.h"     // 包含JSON输出接口声明，提供函数实现
#include <gcc-plugin.h>      // 插件初始化、回调注册、GCC内部API
#include <plugin-version.h>  // GCC版本信息，用于条件编译处理API差异
#include <sys/types.h>       // 系统类型定义（如pid_t、size_t等）
#include <unistd.h>          // Unix标准函数（getpid、close、write等）

namespace GccTrace
{
    // 最小事件长度常量：1毫秒（1000000纳秒）
    // 用于过滤过短的编译事件，避免生成过于庞大的追踪文件
    constexpr int MINIMUM_EVENT_LENGTH_NS = 1000000;  // 1ms

    namespace // 匿名命名空间，限制符号只在当前文件可见
    {
        // JSON输出系统的全局状态变量
        json::object* output_json;            // JSON根对象指针
        json::array* output_events_list;      // 事件数组指针（快速访问）
        static std::FILE* trace_file;         // 输出文件句柄（static限制作用域）

        // 将EventCategory枚举转换为对应的字符串表示
        // 用于JSON输出中的"cat"字段
        const char* category_string(EventCategory cat)
        {
            // 静态字符串数组，避免每次调用都重新构造
            static const char* strings[10] = {
                "TU",                  // Translation Unit（整个编译单元）
                "PREPROCESS",          // 预处理阶段
                "FUNCTION",            // 函数解析
                "STRUCT",              // 结构体/类定义
                "NAMESPACE",           // 命名空间
                "GIMPLE_PASS",         // GIMPLE中间表示优化pass
                "RTL_PASS",            // RTL（寄存器传输级）优化pass
                "SIMPLE_IPA_PASS",     // 简单过程间分析pass
                "IPA_PASS",            // 完整过程间分析pass
                "UNKNOWN"              // 未知类型
            };
            return strings[(int)cat];  // 通过枚举值索引获取字符串
        }

        // 创建单个JSON事件对象
        // 参数说明：
        // - event: 原始追踪事件数据
        // - pid: 进程ID（编译进程）
        // - tid: 线程ID（单线程编译固定为0）
        // - ts: 时间戳（纳秒）
        // - phase: 事件阶段（"B"开始或"E"结束）
        // - this_uid: 事件唯一标识符，用于配对开始和结束事件
        json::object* new_event(const TraceEvent& event, int pid, int tid, TimeStamp ts,
            const char* phase, int this_uid)
        {
            // 创建JSON对象
            json::object* json_event = new json::object;

            // 设置事件基本属性
            json_event->set("name", new json::string(event.name));                      // 事件名称
            json_event->set("ph", new json::string(phase));                             // 阶段："B"或"E"
            json_event->set("cat", new json::string(category_string(event.category)));  // 事件类别

            // 时间戳转换：纳秒 → 微秒（Chrome Tracing标准格式）
            // 乘以0.001L将纳秒转换为微秒，使用long double保证精度
            json_event->set("ts",
                new json::float_number(static_cast<double>(ts) * 0.001L));

            // 进程和线程标识
            json_event->set("pid", new json::integer_number(pid));  // 进程ID
            json_event->set("tid", new json::integer_number(tid));  // 线程ID（固定为0）

            // 创建参数对象
            json::object* args = new json::object();
            args->set("UID", new json::integer_number(this_uid));  // 唯一标识符，用于事件配对

            // 如果事件有额外参数，将它们添加到args对象
            if (event.args)
            {
                // C++17结构化绑定遍历键值对
                for (auto& [key, value] : *event.args)
                {
                    args->set(key.data(), new json::string(value.data()));
                }
            }

            json_event->set("args", args);  // 将参数对象附加到事件
            return json_event;              // 返回构建好的JSON事件对象
        }

    }  // 匿名命名空间结束

    // 初始化输出文件系统
    // 参数：file - 已打开的文件句柄
    void init_output_file(FILE* file)
    {
        trace_file = file;  // 保存文件句柄

        // 创建JSON根对象
        output_json = new json::object();

        // 设置Chrome Tracing格式的元数据
        output_json->set("displayTimeUnit", new json::string("ns"));  // 显示时间单位为纳秒

        // beginningOfTime: 时间原点（编译开始的绝对时间）
        // 转换为微秒精度的时间戳
        output_json->set("beginningOfTime",
            new json::integer_number(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    COMPILATION_START.time_since_epoch())  // 获取从纪元开始的时间间隔
                .count()));  // 转换为微秒计数

        // 创建事件数组容器
        output_json->set("traceEvents", new json::array());

        // 获取事件数组的指针，便于后续快速添加事件
        output_events_list = (json::array*)output_json->get("traceEvents");
    }

    // 添加单个追踪事件到输出队列
    // 参数：event - 要添加的追踪事件
    void add_event(const TraceEvent& event)
    {
        // 静态变量，只初始化一次
        static int pid = getpid();  // 获取编译进程ID
        static int tid = 0;         // 线程ID（单线程编译固定为0）
        static int UID = 0;         // 事件唯一标识符计数器

        // 事件长度过滤：跳过短于1ms的事件
        if ((event.ts.end - event.ts.start) < MINIMUM_EVENT_LENGTH_NS)
        {
            return;  // 事件太短，直接返回
        }

        // 分配当前事件的唯一标识符
        int this_uid = UID++;

        // 为每个事件生成一对JSON记录：开始("B")和结束("E")
        output_events_list->append(
            new_event(event, pid, tid, event.ts.start, "B", this_uid));  // 开始事件
        output_events_list->append(
            new_event(event, pid, tid, event.ts.end, "E", this_uid));    // 结束事件
    }

    // 写入所有追踪事件并完成输出
    // 这是输出模块的主入口函数，在编译结束时调用
    void write_all_events()
    {
        // 1. 添加整个编译单元（TU）的总时间事件
        add_event(TraceEvent{"TU", EventCategory::TU, {0, ns_from_start()}, std::nullopt});

        // 2. 按顺序写入所有类型的追踪事件
        write_preprocessing_events();  // 预处理事件
        write_opt_pass_events();       // 优化pass事件
        write_all_functions();         // 函数解析事件
        write_all_scopes();            // 作用域事件

        // 3. 序列化JSON对象到文件（处理GCC版本兼容性）
#if GCCPLUGIN_VERSION_MAJOR >= 14
    // GCC 14及以上版本：支持格式化参数
        output_json->dump(trace_file, /*formatted=*/false);  // 不格式化，减小文件大小
#else
    // GCC 13及以下版本：简化API
        output_json->dump(trace_file);
#endif

        // 4. 关闭输出文件
        fclose(trace_file);

        // 5. 清理内存资源
        output_events_list = nullptr;  // 清空指针（事件数组会被delete output_json一起释放）
        delete output_json;            // 释放JSON根对象（递归释放所有子对象）
    }

}  // namespace GccTrace
