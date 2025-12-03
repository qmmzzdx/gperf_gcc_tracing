// GCC性能追踪插件的预处理与优化阶段追踪接口头文件

#pragma once              // 头文件保护，防止重复包含

#include "perf_output.h"  // JSON输出接口（add_event函数依赖）
#include "plugin.h"       // 插件接口（声明对称性，实际可能不直接依赖）

namespace GccTrace
{
    // ==================== 预处理阶段追踪接口组 ====================

    /**
     * @brief 开始预处理一个文件（进入#include）
     *
     * 当GCC开始处理一个文件包含时调用，记录文件的预处理开始时间。
     * 主要功能：
     * 1. 记录文件开始预处理的时间戳
     * 2. 处理循环包含（circular includes）边界情况
     * 3. 将文件压入预处理栈，跟踪嵌套包含关系
     * 4. 获取包含路径信息，用于文件名规范化
     *
     * @param file_name 被包含的文件名
     * @param pfile GCC预处理状态机（cpp_reader指针），用于获取包含目录信息
     *              - 如果为nullptr，表示无法获取路径信息
     * @note 由cb_file_change回调在LC_ENTER时调用
     * @note 过滤特殊文件名：空指针和"<command-line>"
     */
    void start_preprocess_file(const char* file_name, cpp_reader* pfile);

    /**
     * @brief 结束预处理一个文件（离开#include）
     *
     * 当GCC完成处理一个文件包含时调用，记录文件的预处理结束时间。
     * 主要功能：
     * 1. 记录文件结束预处理的时间戳
     * 2. 从预处理栈弹出当前文件
     * 3. 更新函数解析时间戳基准（避免时间重叠）
     *
     * @note 由cb_file_change回调在LC_LEAVE时调用
     * @note 自动处理栈管理，确保与start_preprocess_file配对
     */
    void end_preprocess_file();

    /**
     * @brief 强制结束预处理阶段
     *
     * 清理所有未关闭的文件包含，确保数据一致性。
     * 使用场景：
     * 1. 编译错误提前退出
     * 2. 声明处理完成时（cb_finish_decl回调）
     * 3. 写入预处理事件前的安全检查
     *
     * 实现机制：
     * 循环调用end_preprocess_file()直到预处理栈清空
     *
     * @note 安全函数，可多次调用
     */
    void finish_preprocessing_stage();

    /**
     * @brief 写入所有预处理事件
     *
     * 将收集到的所有文件包含追踪事件转换为JSON格式并输出。
     * 处理流程：
     * 1. 确保预处理阶段完全结束（调用finish_preprocessing_stage）
     * 2. 遍历所有预处理文件
     * 3. 为每个文件创建预处理事件（使用规范化文件名）
     * 4. 跳过循环包含的特殊标记（CIRCULAR_POISON_VALUE）
     *
     * 输出内容：
     * - 每个#include文件的处理时间跨度
     * - 使用规范化文件名（相对包含路径）
     *
     * @note 由write_all_events统一调用
     */
    void write_preprocessing_events();

    // ==================== 优化阶段追踪接口组 ====================

    /**
     * @brief 开始追踪一个优化pass的执行
     *
     * 当GCC开始执行一个优化pass时调用，记录pass的开始时间。
     * 处理逻辑：
     * 1. 结束上一个pass的追踪（如果存在）
     * 2. 将上一个pass保存到历史记录
     * 3. 开始新pass的追踪，记录开始时间
     *
     * @param pass GCC优化pass对象指针
     *             包含pass名称、类型、静态编号等信息
     * @note 由cb_pass_execution回调调用
     * @note 时间戳微调（+1纳秒）避免pass事件重叠
     */
    void start_opt_pass(const opt_pass* pass);

    /**
     * @brief 写入所有优化pass事件
     *
     * 将收集到的所有优化pass追踪事件转换为JSON格式并输出。
     * 输出内容：
     * 1. pass名称
     * 2. pass类型（GIMPLE_PASS, RTL_PASS等）
     * 3. 执行时间跨度
     * 4. 额外参数：静态pass编号（static_pass_number）
     *
     * GCC优化pass类型：
     * - GIMPLE_PASS: 高级中间表示优化
     * - RTL_PASS: 低级寄存器传输级优化
     * - SIMPLE_IPA_PASS: 简单过程间分析
     * - IPA_PASS: 完整过程间分析
     *
     * @note 由write_all_events统一调用
     * @note 遍历pass_events向量生成事件
     */
    void write_opt_pass_events();

} // namespace GccTrace

// ==================== 模块设计说明 ====================
/**
 * 本模块是编译过程技术细节的追踪接口层，分为两个子系统：
 *
 * 一、预处理追踪系统：
 *    文件包含栈：std::stack<std::string> preprocessing_stack
 *    时间记录：map_t<std::string, int64_t> preprocess_start/end
 *    特殊处理：循环包含检测（CIRCULAR_POISON_VALUE）
 *    路径系统：文件名规范化（绝对路径→相对包含路径）
 *
 * 二、优化pass追踪系统：
 *    当前pass：OptPassEvent last_pass
 *    历史记录：std::vector<OptPassEvent> pass_events
 *    pass类型转换：pass_type函数（GCC类型→EventCategory）
 *
 * 数据流：
 *    GCC回调 → tracking接口 → 内部存储 → 输出时转换为TraceEvent
 *
 * 关键设计：
 * 1. 边界情况处理：循环包含、路径解析失败、冲突文件名
 * 2. 资源安全：realpath内存释放、栈清理保证
 * 3. 性能优化：哈希映射快速查找、向量连续存储
 *
 * 相关文件：
 * - plugin.cc: 包含cb_file_change和cb_pass_execution回调
 * - tracking.cc: 所有接口的具体实现
 * - perf_output.h: 通过add_event函数输出JSON
 */
