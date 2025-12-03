// GCC性能追踪的数据管理模块
// 负责编译事件的数据收集、处理和存储

#include <gcc-plugin.h>          // GCC插件框架核心头文件（提供插件API）

#include <stack>                 // 标准库：栈容器（用于预处理文件包含栈管理）
#include <string>                // 标准库：字符串（存储文件名、作用域名等）
#include <vector>                // 标准库：向量容器（存储事件列表，支持快速遍历）

#include "c-family/c-pragma.h"   // GCC预处理指令支持（#pragma处理）

#include "cpplib.h"              // GCC C++预处理库（cpp_reader等预处理状态机）
#include "tracking.h"            // 项目内部头文件：本模块的接口声明
#include <tree-pass.h>           // GCC优化pass定义（opt_pass结构体和类型枚举）

namespace GccTrace
{
    // 全局编译开始时间点定义（在comm.h中声明）
    time_point_t COMPILATION_START;

    // 匿名命名空间：内部实现细节，对外不可见
    namespace
    {
        // ==================== 预处理追踪数据结构 ====================
        // 记录每个文件的预处理开始和结束时间
        map_t<std::string, int64_t> preprocess_start;  // 文件 -> 开始时间（纳秒）
        map_t<std::string, int64_t> preprocess_end;    // 文件 -> 结束时间（纳秒）

        // 预处理文件栈：跟踪嵌套的文件包含关系
        // 栈顶是当前正在处理的文件
        std::stack<std::string> preprocessing_stack;

        // 循环包含毒丸值：用于标记循环包含的特殊情况
        const char* CIRCULAR_POISON_VALUE = "CIRCULAR_POISON_VALUE";

        // 上一个函数解析完成的时间戳
        // 用于确保连续函数事件的时间戳不重叠
        TimeStamp last_function_parsed_ts = 0;

        // ==================== 优化pass追踪数据结构 ====================
        // 优化pass事件结构：存储pass指针和时间跨度
        struct OptPassEvent
        {
            const opt_pass* pass;  // GCC优化pass对象
            TimeSpan ts;           // pass执行的时间跨度
        };

        OptPassEvent last_pass;                  // 当前正在执行的pass
        std::vector<OptPassEvent> pass_events;   // 所有pass的历史记录

        // ==================== 文件名规范化系统 ====================
        // 将绝对路径转换为相对包含路径，便于分析和可视化

        // 文件 -> 包含目录的映射（用于计算相对路径）
        map_t<std::string, std::string> file_to_include_directory;

        // 原始文件路径 -> 规范化文件名的映射
        map_t<std::string, std::string> normalized_files_map;

        // 已注册的规范化文件名集合（用于冲突检测）
        set_t<std::string> normalized_files;

        // 冲突文件名集合：多个不同目录有相同相对路径的文件
        set_t<std::string> conflicted_files;

        // 注册文件的包含位置信息
        // 参数：
        //   file_name - 文件绝对路径
        //   dir_name  - 包含该文件的目录绝对路径
        void register_include_location(const char* file_name, const char* dir_name)
        {
            // 如果这个文件还未注册
            if (!file_to_include_directory.contains(file_name))
            {
                std::string file_std = file_name;
                file_to_include_directory[file_name] = dir_name;
                auto& folder_std = file_to_include_directory[file_std];

                // 检查文件路径是否以目录路径开头
                if (file_std.starts_with(folder_std))  // C++20 starts_with方法
                {
                    // 计算相对路径：去除目录前缀和路径分隔符
                    // +1 用于跳过路径分隔符（/ 或 \）
                    auto normalized_file = file_std.substr(folder_std.size() + 1);

                    // 存储映射关系
                    normalized_files_map[file_std] = normalized_file;

                    // 检查文件名冲突
                    if (normalized_files.contains(normalized_file))
                    {
                        // 发现冲突：相同相对路径已存在
                        conflicted_files.insert(normalized_file);
                    }
                    else
                    {
                        // 无冲突，注册成功
                        normalized_files.insert(normalized_file);
                    }
                }
                else
                {
                    // 路径格式异常：文件不在声称的目录中
                    fprintf(stderr, "GPERF warning: Can't normalize paths %s and %s\n", file_name, dir_name);
                }
            }
        }

        // 获取文件的规范化名称
        // 如果没有冲突，返回相对路径；否则返回原始路径
        const char* normalized_file_name(const char* file_name)
        {
            if (normalized_files_map.contains(file_name) &&
                !conflicted_files.contains(normalized_files_map[file_name]))
            {
                // 无冲突：返回相对路径
                return normalized_files_map[file_name].data();
            }
            else
            {
                // 有冲突或未注册：返回原始路径
                return file_name;
            }
        }

        // 将GCC的opt_pass_type转换为项目内部的EventCategory
        EventCategory pass_type(opt_pass_type type)
        {
            switch (type)
            {
                case opt_pass_type::GIMPLE_PASS:
                    return GIMPLE_PASS;       // GIMPLE中间表示优化
                case opt_pass_type::RTL_PASS:
                    return RTL_PASS;          // RTL（寄存器传输级）优化
                case opt_pass_type::SIMPLE_IPA_PASS:
                    return SIMPLE_IPA_PASS;   // 简单过程间分析
                case opt_pass_type::IPA_PASS:
                    return IPA_PASS;          // 完整过程间分析
            }
            return UNKNOWN;  // 未知类型（默认情况）
        }

        // ==================== 函数和作用域事件存储 ====================

        // 作用域事件结构：命名空间、类/结构体的追踪
        struct ScopeEvent
        {
            std::string name;       // 作用域名称
            EventCategory type;     // 作用域类型（STRUCT 或 NAMESPACE）
            TimeSpan ts;            // 时间跨度
        };
        std::vector<ScopeEvent> scope_events;  // 所有作用域事件

        // 函数事件结构：函数解析的追踪
        struct FunctionEvent
        {
            std::string name;      // 函数签名（包含命名空间和类名）
            const char* file_name; // 定义所在的源文件
            TimeSpan ts;           // 解析时间跨度
        };
        std::vector<FunctionEvent> function_events;  // 所有函数事件

    } // 匿名命名空间结束

    // ==================== 公共接口实现 ====================

    // 强制结束预处理阶段
    // 清理所有未关闭的文件包含，确保数据一致性
    void finish_preprocessing_stage()
    {
        // 循环处理所有仍在栈中的文件
        while (!preprocessing_stack.empty())
        {
            end_preprocess_file();                      // 结束当前文件的预处理
            last_function_parsed_ts = ns_from_start();  // 更新时间戳基准
        }
    }

    // 开始预处理一个文件（进入#include）
    // 参数：
    //   file_name - 文件名
    //   pfile     - GCC预处理状态机（用于获取包含路径信息）
    void start_preprocess_file(const char* file_name, cpp_reader* pfile)
    {
        auto now = ns_from_start();  // 获取当前时间

        // 过滤特殊文件名：空指针或命令行参数
        if (!file_name || !strcmp(file_name, "<command-line>"))
        {
            return;
        }

        // 检查循环包含（文件已在栈中但未结束）
        if (preprocess_start.contains(file_name) &&
            !preprocess_end.contains(file_name))
        {
            // 发现循环包含！这是一个边界情况
            // 我们不追踪内层的包含，而是使用毒丸值标记
            file_name = CIRCULAR_POISON_VALUE;  // 替换为毒丸值
            pfile = nullptr;                    // 清空pfile，避免后续处理
        }

        // 记录文件的开始时间（如果是第一次处理）
        if (!preprocess_start.contains(file_name))
        {
            preprocess_start[file_name] = now;
        }

        // 将文件压入栈中（表示开始处理）
        preprocessing_stack.push(file_name);

        // 如果pfile有效，获取包含路径信息用于文件名规范化
        if (pfile)
        {
            // 获取GCC预处理器的内部数据结构
            auto cpp_buffer = cpp_get_buffer(pfile);
            auto cpp_file = cpp_get_file(cpp_buffer);
            auto dir = cpp_get_dir(cpp_file);

            // 获取真实路径（解析符号链接）
            auto real_dir_name = realpath(dir->name, nullptr);
            auto real_file_name = realpath(file_name, nullptr);

            // 如果两个路径都成功获取，注册包含关系
            if (real_dir_name && real_file_name)
            {
                register_include_location(real_file_name, real_dir_name);
            }
            else
            {
                // 路径解析失败，输出错误信息
                if (strcmp(dir->name, ""))
                {
                    fprintf(stderr, "GPERF error! Couldn't call realpath(\"%s\")\n", dir->name);
                }
            }
            // 清理realpath分配的内存
            if (real_dir_name)
            {
                free(real_dir_name);
            }
            if (real_file_name)
            {
                free(real_file_name);
            }
        }
    }

    // 结束预处理一个文件（离开#include）
    void end_preprocess_file()
    {
        auto now = ns_from_start();  // 获取当前时间

        // 记录栈顶文件的结束时间
        if (!preprocess_end.contains(preprocessing_stack.top()))
        {
            preprocess_end[preprocessing_stack.top()] = now;
        }

        // 弹出栈顶文件（表示处理完成）
        preprocessing_stack.pop();

        // 更新函数解析时间戳基准（+3纳秒避免重叠）
        last_function_parsed_ts = now + 3;
    }

    // 写入所有预处理事件到输出系统
    void write_preprocessing_events()
    {
        // 确保预处理阶段完全结束（安全措施）
        finish_preprocessing_stage();

        // 遍历所有预处理文件
        for (const auto& [file, start] : preprocess_start)
        {
            // 跳过循环包含的毒丸记录
            if (file == CIRCULAR_POISON_VALUE)
            {
                continue;
            }

            // 获取文件的结束时间
            int64_t end = preprocess_end.at(file);

            // 创建并添加预处理事件
            add_event(TraceEvent{
                normalized_file_name(file.data()),  // 使用规范化文件名
                EventCategory::PREPROCESS,          // 事件类别：预处理
                {start, end},                       // 时间跨度
                std::nullopt                        // 无额外参数
                });
        }
    }

    // 开始追踪一个优化pass的执行
    void start_opt_pass(const opt_pass* pass)
    {
        auto now = ns_from_start();  // 获取当前时间

        // 结束上一个pass的追踪（如果有的话）
        last_pass.ts.end = now;
        if (last_pass.pass)
        {
            // 将上一个pass保存到历史记录
            pass_events.emplace_back(last_pass);
        }

        // 开始新pass的追踪
        last_pass.pass = pass;           // 设置pass指针
        last_pass.ts.start = now + 1;    // 开始时间（+1纳秒避免重叠）
    }

    // 写入所有优化pass事件到输出系统
    void write_opt_pass_events()
    {
        // 遍历所有记录的pass事件
        for (const auto& event : pass_events)
        {
            // 准备pass的额外参数
            map_t<std::string, std::string> args;
            args["static_pass_number"] = std::to_string(event.pass->static_pass_number);

            // 创建并添加pass事件
            add_event(TraceEvent{
                event.pass->name,                // pass名称
                pass_type(event.pass->type),     // pass类型转换
                event.ts,                        // 时间跨度
                std::move(args)                  // 额外参数（移动语义）
                });
        }
    }

    // 处理函数解析完成事件
    // 参数：
    //   info - 从GCC回调传递来的函数信息
    void end_parse_function(FinishedFunction info)
    {
        // 静态变量：记录上一个函数是否有作用域
        static bool did_last_function_have_scope = false;

        TimeStamp now = ns_from_start();  // 获取当前时间

        // 由于Chrome Tracing的UI bug，我们不能让不同事件在同一时间开始和结束
        // 因此调整事件的时间戳，避免完全重叠

        // 计算函数解析的时间跨度（+3纳秒避免与上一个事件重叠）
        TimeSpan ts{last_function_parsed_ts + 3, now};
        last_function_parsed_ts = now;  // 更新基准时间

        // 存储函数事件
        function_events.emplace_back(info.name, info.file_name, ts);

        // 处理作用域事件（如果函数有作用域）
        if (info.scope_name)
        {
            // 检查是否可以扩展上一个作用域事件
            if (!scope_events.empty() && did_last_function_have_scope &&
                scope_events.back().name == info.scope_name)
            {
                // 扩展现有作用域的时间范围（+1纳秒避免重叠）
                scope_events.back().ts.end = ts.end + 1;
            }
            else
            {
                // 创建新的作用域事件（微调时间避免重叠）
                scope_events.emplace_back(
                    info.scope_name,                    // 作用域名称
                    info.scope_type,                    // 作用域类型
                    TimeSpan{ts.start - 1, ts.end + 1}  // 时间跨度
                );
            }
            did_last_function_have_scope = true;
        }
        else
        {
            did_last_function_have_scope = false;
        }
    }

    // 写入所有作用域事件到输出系统
    void write_all_scopes()
    {
        // 遍历所有作用域事件
        for (const auto& [name, type, ts] : scope_events)
        {
            // 创建并添加作用域事件
            add_event(TraceEvent{
                name.data(),    // 作用域名称
                type,           // 作用域类型
                ts,             // 时间跨度
                std::nullopt    // 无额外参数
                });
        }
    }

    // 写入所有函数事件到输出系统
    void write_all_functions()
    {
        // 遍历所有函数事件
        for (const auto& [name, file_name, ts] : function_events)
        {
            // 准备函数的额外参数
            map_t<std::string, std::string> args;
            args["file"] = normalized_file_name(file_name);  // 规范化文件名

            // 创建并添加函数事件
            add_event(TraceEvent{
                name.data(),                    // 函数签名
                EventCategory::FUNCTION,        // 事件类别：函数
                ts,                             // 时间跨度
                std::move(args)                 // 额外参数（包含文件名）
                });
        }
    }
} // namespace GccTrace
