// GCC性能追踪插件的JSON输出接口头文件

#pragma once          // 头文件保护，防止重复包含

#include "comm.h"     // 项目核心数据结构（TraceEvent, FinishedFunction等）
#include "plugin.h"   // 插件回调接口（write_all_functions, write_all_scopes等）
#include "tracking.h" // 追踪控制接口（write_preprocessing_events, write_opt_pass_events等）

#include <json.h>     // GCC内置JSON序列化库（用于生成Chrome Tracing格式的JSON文件）

// ==================== 命名空间声明 ====================
namespace GccTrace
{
    // ==================== 输出模块接口函数 ====================

    /**
     * @brief 初始化输出文件系统
     *
     * 创建JSON根对象，设置Chrome Tracing格式的元数据，准备事件数组容器。
     * 必须在插件初始化时调用，且只能调用一次。
     *
     * @param file 已打开的文件句柄（由setup_output函数提供）
     * @note 该函数会设置全局状态，包括output_json和output_events_list
     */
    void init_output_file(FILE* file);

    /**
     * @brief 添加单个追踪事件到输出队列
     *
     * 将收集到的编译事件转换为JSON格式，并添加到输出缓冲区。
     * 每个事件会生成一对"B"（开始）和"E"（结束）记录。
     *
     * @param event 要添加的追踪事件（包含名称、类别、时间跨度等）
     * @note 内部会过滤短于MINIMUM_EVENT_LENGTH_NS（1ms）的事件
     * @note 自动分配唯一UID确保开始/结束事件正确配对
     */
    void add_event(const TraceEvent& event);

    /**
     * @brief 写入所有追踪事件并完成输出
     *
     * 插件的主输出入口函数，在编译结束时调用。
     * 执行顺序：
     * 1. 添加TU（整个编译单元）总时间事件
     * 2. 调用各模块的写入函数（预处理、优化pass、函数、作用域）
     * 3. 序列化JSON到文件
     * 4. 清理内存资源
     *
     * @note 此函数由cb_plugin_finish回调触发
     */
    void write_all_events();

    /**
     * @brief 写入单个追踪事件
     *
     * 直接写入单个事件到输出（可能用于调试或特殊场景）。
     * 注意：该函数在perf_output.cpp中未实现，可能是预留接口或废弃函数。
     *
     * @param event 要写入的追踪事件
     * @param bool 控制参数（可能表示是否立即刷新、是否格式化输出等）
     * @todo 确认此函数是否需要实现或移除
     */
    void write_event(const TraceEvent&, bool);
}

// ==================== 模块依赖关系说明 ====================
/**
 * 本模块是项目的输出层，依赖关系如下：
 *
 *        comm.h（数据定义）           外部：json.h（GCC JSON库）
 *              ↓                            ↓
 *         perf_output.h（本文件）
 *              ↓
 *     ┌───────┼───────┐
 *     ↓       ↓       ↓
 * plugin.h  tracking.h  perf_output.cpp（实现）
 *     ↓       ↓
 * 函数/作用域  预处理/优化pass
 *
 * 数据流向：
 * 1. 各追踪模块 → 收集事件数据 → TraceEvent
 * 2. TraceEvent → add_event() → JSON格式转换
 * 3. JSON数据 → write_all_events() → trace.json文件
 *
 * 关键设计：
 * - 延迟写入：所有事件先收集在内存中，编译结束时一次性写入
 * - 事件过滤：跳过短于1ms的事件，减少噪音和文件大小
 * - 时间转换：内部使用纳秒，输出转换为微秒（Chrome Tracing标准）
 * - 版本兼容：处理GCC 14+与旧版本的JSON dump API差异
 */
