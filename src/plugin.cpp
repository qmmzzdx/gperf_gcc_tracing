// GCC性能追踪插件的主入口模块
// 负责插件初始化、回调函数注册和GCC事件处理

#include "plugin.h"             // 包含插件接口声明，提供回调函数的实现
#include <options.h>            // GCC编译选项处理和解析
#include <tree-check.h>         // GCC树节点验证和调试工具
#include <tree-pass.h>          // GCC优化pass定义和管理
#include <tree.h>               // GCC抽象语法树（AST）核心数据结构定义
#include <cp/cp-tree.h>         // C++特定的树节点类型和操作函数
#include "c-family/c-pragma.h"  // 预处理指令（#pragma）处理
#include "cpplib.h"             // C++预处理库核心实现

// GCC插件必须的GPL兼容性声明
// 值为1表示插件与GPL许可证兼容
int plugin_is_GPL_compatible = 1;

namespace GccTrace
{
    // ==================== GCC回调函数实现 ====================

    // 回调函数：当GCC完成一个函数的解析时调用
    // 参数：
    //   gcc_data - GCC传入的数据，指向函数的tree节点
    //   user_data - 用户数据（未使用）
    void cb_finish_parse_function(void* gcc_data, void* user_data)
    {
        // 将void*转换为GCC的tree类型（函数声明节点）
        tree decl = (tree)gcc_data;

        // 获取函数的源代码位置信息（文件、行号、列号）
        auto expanded_location = expand_location(decl->decl_minimal.locus);

        // 获取函数的名称（带命名空间和参数类型的完整签名）
        auto decl_name = decl_as_string(decl, 0);

        // 获取函数的父作用域（包含此函数的命名空间或类）
        auto parent_decl = DECL_CONTEXT(decl);

        // 初始化作用域信息
        const char* scope_name = nullptr;
        GccTrace::EventCategory scope_type = GccTrace::EventCategory::UNKNOWN;

        // 如果存在父作用域且不是全局作用域（翻译单元）
        if (parent_decl)
        {
            if (TREE_CODE(parent_decl) != TRANSLATION_UNIT_DECL)
            {
                // 获取作用域名称
                scope_name = decl_as_string(parent_decl, 0);

                // 根据GCC树节点类型判断作用域类型
                switch (TREE_CODE(parent_decl))
                {
                    case NAMESPACE_DECL:  // C++命名空间
                        scope_type = GccTrace::EventCategory::NAMESPACE;
                        break;
                    case RECORD_TYPE:     // 结构体或类
                        scope_type = GccTrace::EventCategory::STRUCT;
                        break;
                    case UNION_TYPE:      // 联合体（也归类为STRUCT）
                        scope_type = GccTrace::EventCategory::STRUCT;
                        break;
                    default:
                        // 未知的作用域类型，输出警告信息
                        fprintf(stderr, "Unkown tree code %d\n", TREE_CODE(parent_decl));
                        break;
                }
            }
        }

        // 将收集到的函数信息传递给追踪系统处理
        end_parse_function(FinishedFunction{
            gcc_data,                // GCC树节点指针（保持原始类型）
            decl_name,               // 函数签名
            expanded_location.file,  // 定义所在的源文件
            scope_name,              // 所属作用域名称（可为空）
            scope_type               // 作用域类型
            });
    }

    // 回调函数：当GCC完成整个编译过程时调用
    // 负责触发所有事件的最终写入
    void cb_plugin_finish(void* gcc_data, void* user_data)
    {
        write_all_events();
    }

    // 保存原始的文件变更回调函数指针（用于函数链式调用）
    void (*old_file_change_cb)(cpp_reader*, const line_map_ordinary*);

    // 回调函数：当GCC处理文件包含（#include）时调用
    // 使用Hook技术插入预处理文件追踪逻辑
    void cb_file_change(cpp_reader* pfile, const line_map_ordinary* new_map)
    {
        // 检查是否有新的行号映射（表示文件切换）
        if (new_map)
        {
            // 获取文件名
            const char* file_name = ORDINARY_MAP_FILE_NAME(new_map);
            if (file_name)
            {
                // 根据文件切换原因进行处理
                switch (new_map->reason)
                {
                    case LC_ENTER:  // 进入新文件（开始处理#include）
                        start_preprocess_file(file_name, pfile);
                        break;
                    case LC_LEAVE:  // 离开当前文件（结束处理#include）
                        end_preprocess_file();
                        break;
                    default:
                        // 忽略其他原因的文件变更
                        break;
                }
            }
        }

        // 调用原始的file_change回调，保持GCC原有功能
        (*old_file_change_cb)(pfile, new_map);
    }

    // 回调函数：当GCC开始编译一个翻译单元时调用
    void cb_start_compilation(void* gcc_data, void* user_data)
    {
        // 开始追踪主输入文件的预处理
        // main_input_filename是GCC全局变量，指向主源文件
        start_preprocess_file(main_input_filename, nullptr);

        // 获取GCC的C++预处理回调函数表
        cpp_callbacks* cpp_cbs = cpp_get_callbacks(parse_in);

        // 保存原始的文件变更回调函数
        old_file_change_cb = cpp_cbs->file_change;

        // 用我们的回调函数替换原始的file_change回调
        cpp_cbs->file_change = &cb_file_change;
    }

    // 回调函数：当GCC执行一个优化pass时调用
    void cb_pass_execution(void* gcc_data, void* user_data)
    {
        // 将gcc_data转换为优化pass指针
        auto pass = (opt_pass*)gcc_data;

        // 开始追踪这个优化pass的执行
        start_opt_pass(pass);
    }

    // 回调函数：当GCC完成一个声明的处理时调用
    // 主要用于标记预处理阶段的结束
    void cb_finish_decl(void* gcc_data, void* user_data)
    {
        finish_preprocessing_stage();
    }

}  // namespace GccTrace结束

// ==================== 插件全局函数 ====================

// 插件名称常量
static const char* PLUGIN_NAME = "gperf";

// 设置输出文件系统
// 参数：
//   argc - 插件参数个数
//   argv - 插件参数数组
// 返回值：成功返回true，失败返回false
bool setup_output(int argc, plugin_argument* argv)
{
    // 插件参数名称定义
    const char* flag_name = "trace";       // 直接指定输出文件
    const char* dir_flag_name = "trace-dir"; // 指定输出目录

    // TODO: 可以考虑将默认文件名与源文件名关联
    // TODO: 验证我们一次只编译一个翻译单元（目前不支持并行编译追踪）

    FILE* trace_file = nullptr;  // 输出文件句柄

    // 根据参数数量和处理方式分为三种情况：

    // 情况1：没有指定参数，使用默认临时文件
    if (argc == 0)
    {
        // 创建临时文件模板
        char file_template[] = "/tmp/trace_XXXXXX.json";

        // 使用mkstemps创建唯一的临时文件（XXXXXX会被随机字符替换）
        // 参数5表示".json"后缀长度
        int fd = mkstemps(file_template, 5);
        if (fd == -1)
        {
            perror("GPERF mkstemps error: ");
            return false;
        }

        // 将文件描述符转换为FILE*指针
        trace_file = fdopen(fd, "w");
    }
    // 情况2：指定了输出文件路径
    else if (argc == 1 && !strcmp(argv[0].key, flag_name))
    {
        // 直接打开指定的文件
        trace_file = fopen(argv[0].value, "w");
        if (!trace_file)
        {
            fprintf(stderr, "GPERF Error! Couldn't open %s for writing\n", argv[0].value);
        }
    }
    // 情况3：指定了输出目录
    else if (argc == 1 && !strcmp(argv[0].key, dir_flag_name))
    {
        // 构建文件路径：目录 + 临时文件名
        std::string file_template{argv[0].value};
        file_template += "/trace_XXXXXX.json";

        // 在指定目录创建临时文件
        int fd = mkstemps(file_template.data(), 5);
        if (fd == -1)
        {
            perror("GPERF mkstemps error: ");
            return false;
        }

        trace_file = fdopen(fd, "w");
    }
    // 情况4：参数格式错误
    else
    {
        fprintf(stderr,
            "GPERF Error! Arguments must be -fplugin-arg-%s-%s=FILENAME or "
            "-fplugin-arg-%s-%s=DIRECTORY\n",
            PLUGIN_NAME, flag_name, PLUGIN_NAME, dir_flag_name);
        return false;
    }

    // 如果成功创建/打开文件，初始化输出系统
    if (trace_file)
    {
        GccTrace::init_output_file(trace_file);
        return true;
    }
    else
    {
        return false;
    }
}

// GCC插件初始化函数 - 这是插件的入口点
// 参数：
//   plugin_info - 插件信息结构，包含名称、参数等
//   ver - GCC版本信息，用于兼容性检查
// 返回值：成功返回0，失败返回-1
int plugin_init(struct plugin_name_args* plugin_info,
    struct plugin_gcc_version* ver)
{
    // 定义插件信息结构
    static struct plugin_info gcc_trace_info = {
        .version = "V1.0",
        .help = "GccTrace time traces of the compilation."
    };

    // 记录编译开始时间（关键的时间基准）
    GccTrace::COMPILATION_START = GccTrace::clock_t::now();

    // 设置输出文件系统
    if (!setup_output(plugin_info->argc, plugin_info->argv))
    {
        return -1;  // 初始化失败
    }

    // ============ 注册插件回调函数（按编译流程顺序）============

    // 1. 注册插件基本信息
    register_callback(PLUGIN_NAME, PLUGIN_INFO, nullptr, &gcc_trace_info);

    // 2. 注册编译单元开始回调（最早调用）
    register_callback(PLUGIN_NAME, PLUGIN_START_UNIT,
        &GccTrace::cb_start_compilation, nullptr);

    // 3. 注册声明处理完成回调（标记预处理结束）
    register_callback(PLUGIN_NAME, PLUGIN_FINISH_DECL,
        &GccTrace::cb_finish_decl, nullptr);

    // 4. 注册函数解析完成回调（处理每个函数的解析）
    register_callback(PLUGIN_NAME, PLUGIN_FINISH_PARSE_FUNCTION,
        &GccTrace::cb_finish_parse_function, nullptr);

    // 5. 注册优化pass执行回调（追踪每个优化阶段）
    register_callback(PLUGIN_NAME, PLUGIN_PASS_EXECUTION,
        &GccTrace::cb_pass_execution, nullptr);

    // 6. 注册编译完成回调（最后调用，触发数据输出）
    register_callback(PLUGIN_NAME, PLUGIN_FINISH,
        &GccTrace::cb_plugin_finish, nullptr);

    return 0;  // 插件初始化成功
}
