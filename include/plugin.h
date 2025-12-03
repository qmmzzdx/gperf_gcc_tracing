// GCC性能追踪插件的函数与作用域追踪接口头文件

#pragma once                 // 头文件保护，防止重复包含

#include <gcc-plugin.h>      // GCC插件框架核心头文件（提供plugin_init等API）
#include <pretty-print.h>    // GCC树节点美化输出工具（用于decl_as_string等函数）

#include "comm.h"            // 项目核心数据结构（FinishedFunction, EventCategory等）
#include "perf_output.h"     // JSON输出接口（add_event函数依赖）

namespace GccTrace
{
    // ==================== 函数/作用域追踪接口 ====================

    /**
     * @brief 处理函数解析完成事件
     *
     * 当GCC完成一个函数的语法解析时调用，从GCC的AST（抽象语法树）中提取
     * 函数信息并传递给追踪系统。
     *
     * 主要任务：
     * 1. 从GCC的tree节点提取函数信息（名称、位置、作用域等）
     * 2. 计算函数解析的时间跨度
     * 3. 记录到函数事件列表和相应作用域事件
     *
     * @param info FinishedFunction结构，包含：
     *   - decl: GCC tree节点指针（类型擦除为void*）
     *   - name: 函数完整签名（包含命名空间和参数类型）
     *   - file_name: 定义所在的源文件
     *   - scope_name: 所属作用域名称（命名空间或类名，可能为空）
     *   - scope_type: 作用域类型（NAMESPACE或STRUCT）
     *
     * @note 由cb_finish_parse_function回调调用，在tracking.cc中实现
     * @see tracking.cc中的end_parse_function实现
     */
    void end_parse_function(FinishedFunction info);

    /**
     * @brief 写入所有作用域（命名空间、类/结构体）事件
     *
     * 将收集到的所有作用域追踪事件转换为JSON格式并输出。
     * 作用域包括：
     * - NAMESPACE: C++命名空间
     * - STRUCT: 结构体、类、联合体
     *
     * 每个作用域事件包含：
     * 1. 作用域名称
     * 2. 作用域类型
     * 3. 时间跨度（进入和离开作用域的时间）
     *
     * @note 由write_all_events调用，在编译结束时统一输出
     * @note 在tracking.cc中实现，遍历scope_events向量
     */
    void write_all_scopes();

    /**
     * @brief 写入所有函数解析事件
     *
     * 将收集到的所有函数解析追踪事件转换为JSON格式并输出。
     * 每个函数事件包含：
     * 1. 函数完整签名
     * 2. 解析时间跨度
     * 3. 额外参数：定义所在的源文件（规范化路径）
     *
     * 输出格式符合Chrome Tracing标准，可用于性能可视化分析。
     *
     * @note 由write_all_events调用，在编译结束时统一输出
     * @note 在tracking.cc中实现，遍历function_events向量
     * @note 函数名称使用规范化路径，避免绝对路径导致的视觉混乱
     */
    void write_all_functions();

} // namespace GccTrace

// ==================== 模块角色说明 ====================
/**
 * 本模块是语言层面的追踪接口层，负责：
 *
 * 数据来源：GCC AST（抽象语法树）
 *           ↓
 *     cb_finish_parse_function（GCC回调）
 *           ↓
 *     end_parse_function（提取函数信息）
 *           ↓
 *   存储到 function_events / scope_events
 *           ↓
 * write_all_functions / write_all_scopes（输出时调用）
 *           ↓
 *        add_event（JSON转换）
 *
 * 关键特点：
 * 1. 作用域处理：智能合并连续函数的作用域，避免重复事件
 * 2. 时间对齐：微调时间戳避免Chrome Tracing显示重叠
 * 3. 路径规范化：将绝对路径转换为相对包含路径
 *
 * 相关文件：
 * - plugin.cc: 包含cb_finish_parse_function回调实现
 * - tracking.cc: 包含end_parse_function、write_all_functions、write_all_scopes实现
 * - comm.h: 定义FinishedFunction数据结构
 */
