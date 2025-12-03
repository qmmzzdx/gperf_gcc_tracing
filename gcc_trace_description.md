# gcc trace 功能模块及测试流程详细解析

# **perf_output.cpp 功能原理解析**

## **文件头注释**
```
// GCC性能追踪插件的JSON输出模块
// 负责将收集到的编译事件转换为Chrome Tracing格式的JSON文件
```
**功能原理**：明确模块职责——将内部事件数据转换为标准化的JSON格式，用于在Chrome Tracing等工具中可视化。

## **包含头文件**
```cpp
#include "perf_output.h"     // 包含JSON输出接口声明，提供函数实现
#include <gcc-plugin.h>      // 插件初始化、回调注册、GCC内部API
#include <plugin-version.h>  // GCC版本信息，用于条件编译处理API差异
#include <sys/types.h>       // 系统类型定义（如pid_t、size_t等）
#include <unistd.h>          // Unix标准函数（getpid、close、write等）
```

### **详细功能原理**：

**`#include "perf_output.h"`**
- **自包含实现**：确保实现文件包含对应的声明文件
- **编译检查**：编译器会验证声明与实现的一致性
- **依赖管理**：明确本模块依赖的接口

**`#include <gcc-plugin.h>`**
- **插件上下文**：虽然输出模块不直接使用插件API，但需要GCC环境上下文
- **内存分配器**：GCC内部可能有特殊的内存管理需求
- **错误处理**：使用GCC统一的错误报告机制

**`#include <plugin-version.h>`**
- **版本兼容性**：关键！GCC JSON库API在不同版本间有变化
- **条件编译**：根据`GCCPLUGIN_VERSION_MAJOR`宏选择不同实现
- **向后兼容**：确保插件能在多个GCC版本上工作

**系统头文件**
```cpp
#include <sys/types.h>  // 提供pid_t类型定义
#include <unistd.h>     // 提供getpid()系统调用
```
- **进程标识**：`getpid()`获取当前编译进程的PID
- **文件操作**：`close()`、`write()`等底层文件操作
- **平台特定**：确保在Unix-like系统上正常工作

## **命名空间和常量定义**
```cpp
namespace GccTrace
{
    // 最小事件长度常量：1毫秒（1000000纳秒）
    // 用于过滤过短的编译事件，避免生成过于庞大的追踪文件
    constexpr int MINIMUM_EVENT_LENGTH_NS = 1000000;  // 1ms
```

### **详细功能原理**：

**`namespace GccTrace`**
- **作用域隔离**：所有输出功能都封装在GccTrace命名空间内
- **符号管理**：避免与GCC内部或其他插件的符号冲突
- **模块化**：清晰的代码组织结构

**`MINIMUM_EVENT_LENGTH_NS`常量**
```cpp
constexpr int MINIMUM_EVENT_LENGTH_NS = 1000000;  // 1ms
```
- **性能优化**：过滤短事件有三个关键目的：
  1. **减少文件大小**：短事件数量可能极多，显著增加JSON文件大小
  2. **降低噪音**：微秒级事件通常是测量误差或系统调度干扰
  3. **提高可读性**：避免时间线过于拥挤，影响分析体验
- **值的选择**：1ms是经验值，足够捕获有意义的编译事件
- **`constexpr`优势**：编译时常量，无运行时开销，可优化

## **匿名命名空间**
```cpp
namespace // 匿名命名空间，限制符号只在当前文件可见
{
    // JSON输出系统的全局状态变量
    json::object* output_json;            // JSON根对象指针
    json::array* output_events_list;      // 事件数组指针（快速访问）
    static std::FILE* trace_file;         // 输出文件句柄（static限制作用域）
```

### **详细功能原理**：

**匿名命名空间**
- **内部实现隐藏**：JSON输出细节对外完全隐藏
- **链接安全**：所有符号都是内部链接，不会与其他编译单元冲突
- **封装性**：强制通过公有接口访问功能

**全局状态变量**
```cpp
json::object* output_json;            // JSON根对象指针
json::array* output_events_list;      // 事件数组指针（快速访问）
static std::FILE* trace_file;         // 输出文件句柄
```
**设计原理分析**：

1. **`json::object* output_json`**
   - **JSON文档根**：存储整个trace文件的完整结构
   - **手动内存管理**：使用原始指针是因为GCC JSON库需要显式释放
   - **单例模式**：整个编译过程只有一个输出JSON对象

2. **`json::array* output_events_list`**
   - **性能优化**：直接指针访问避免每次查找
   ```cpp
   // 如果没有这个指针，每次添加事件都需要：
   json::array* arr = (json::array*)output_json->get("traceEvents");
   arr->append(event);
   
   // 有这个指针后：
   output_events_list->append(event);  // 直接访问
   ```
   - **缓存友好**：减少哈希表查找开销
   - **类型安全**：存储为具体类型指针而非基类指针

3. **`static std::FILE* trace_file`**
   - **文件生命周期**：从插件初始化到编译结束一直打开
   - **`static`限定**：确保作用域仅限于本文件
   - **资源管理**：需要在`write_all_events()`中显式关闭

## **category_string()函数**
```cpp
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
```

### **详细功能原理**：

**`static const char* strings[10]`**
- **静态数组**：只初始化一次，生命周期持续到程序结束
- **内存效率**：相比每次调用都创建数组，节省大量内存分配
- **线程安全**：虽然单线程编译，但静态局部变量初始化是线程安全的（C++11起）

**数组内容与EventCategory枚举的严格对应**：
```cpp
// 必须与comm.h中的EventCategory顺序完全一致
enum EventCategory {          // 数组索引
    TU,                 // 0 -> "TU"
    PREPROCESS,         // 1 -> "PREPROCESS"
    FUNCTION,           // 2 -> "FUNCTION"
    STRUCT,             // 3 -> "STRUCT"
    NAMESPACE,          // 4 -> "NAMESPACE"
    GIMPLE_PASS,        // 5 -> "GIMPLE_PASS"
    RTL_PASS,           // 6 -> "RTL_PASS"
    SIMPLE_IPA_PASS,    // 7 -> "SIMPLE_IPA_PASS"
    IPA_PASS,           // 8 -> "IPA_PASS"
    UNKNOWN             // 9 -> "UNKNOWN"
};
```

**`return strings[(int)cat];`**
- **类型转换**：枚举值隐式转换为int，但显式转换更清晰
- **数组索引**：O(1)时间复杂度，极高效
- **边界安全**：依赖枚举值在有效范围内（0-9）

## **new_event()函数**
```cpp
json::object* new_event(const TraceEvent& event, int pid, int tid, TimeStamp ts,
    const char* phase, int this_uid)
```

### **参数详细功能原理**：

1. **`const TraceEvent& event`**
   - 引用传递：避免拷贝整个事件结构
   - `const`限定：确保函数不修改原始事件

2. **`int pid`**
   - 进程ID：标识哪个GCC进程生成了这个事件
   - 多进程编译时区分不同进程

3. **`int tid`**
   - 线程ID：单线程编译固定为0
   - 为未来多线程编译预留

4. **`TimeStamp ts`**
   - 时间戳：纳秒单位的相对时间
   - 需要转换为微秒输出

5. **`const char* phase`**
   - 事件阶段："B"（Begin）或"E"（End）
   - Chrome Tracing要求每个事件有明确的开始和结束

6. **`int this_uid`**
   - 唯一标识符：用于配对开始和结束事件
   - 确保"B"和对应的"E"能正确关联

### **函数实现详细功能原理**：

**创建JSON对象**
```cpp
json::object* json_event = new json::object;
```
- **手动分配**：GCC JSON库使用new/delete管理内存
- **对象创建**：每个事件对应一个JSON对象

**设置基本属性**
```cpp
json_event->set("name", new json::string(event.name));
json_event->set("ph", new json::string(phase));
json_event->set("cat", new json::string(category_string(event.category)));
```
- **键值对设置**：GCC JSON库的set方法
- **字符串包装**：所有字符串值都需要包装为`json::string`对象
- **内存管理**：每个值都是独立分配的对象

**时间戳转换**
```cpp
json_event->set("ts",
    new json::float_number(static_cast<double>(ts) * 0.001L));
```
**详细转换原理**：
1. **内部单位**：纳秒（1e-9秒）
2. **Chrome Tracing标准**：微秒（1e-6秒）
3. **转换公式**：`微秒 = 纳秒 × 0.001`
4. **精度保证**：使用`long double`进行乘法，避免精度损失
5. **类型转换**：`static_cast<double>`确保浮点数运算

**进程和线程标识**
```cpp
json_event->set("pid", new json::integer_number(pid));
json_event->set("tid", new json::integer_number(tid));
```
- **整数类型**：使用`json::integer_number`而非`json::float_number`
- **必要性**：虽然线程ID固定为0，但Chrome Tracing要求必须有这个字段

**创建参数对象**
```cpp
json::object* args = new json::object();
args->set("UID", new json::integer_number(this_uid));
```
**UID功能原理**：
- **唯一性**：每个事件对（B+E）有相同的UID
- **配对机制**：Chrome Tracing通过UID关联开始和结束
- **递增分配**：简单但有效的唯一ID生成策略

**额外参数处理**
```cpp
if (event.args)
{
    for (auto& [key, value] : *event.args)
    {
        args->set(key.data(), new json::string(value.data()));
    }
}
```
**详细功能原理**：
1. **可选参数**：`event.args`是`std::optional`，可能为空
2. **C++17结构化绑定**：简洁遍历map键值对
3. **参数复制**：所有参数值都复制到JSON对象中
4. **示例**：函数事件有`{"file": "test.cpp"}`参数

**附加参数并返回**
```cpp
json_event->set("args", args);
return json_event;
```
- **参数附加**：args对象作为整体附加到事件
- **所有权转移**：json_event现在负责管理args的内存

## **init_output_file()函数**
```cpp
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
```

### **详细功能原理**：

**`trace_file = file;`**
- **文件句柄保存**：从`setup_output()`传递过来的已打开文件
- **后续使用**：在`write_all_events()`中写入和关闭

**创建JSON根对象**
```cpp
output_json = new json::object();
```
- **文档根**：整个JSON文档的顶层对象
- **内存分配**：需要最后在`write_all_events()`中delete

**设置时间单位**
```cpp
output_json->set("displayTimeUnit", new json::string("ns"));
```
**功能原理**：
- **显示提示**：告诉Chrome Tracing时间数据显示的单位
- **注意**：实际存储的是微秒，但显示时可以按纳秒解释
- **用户体验**：让用户看到更精确的时间数字

**设置时间原点**
```cpp
output_json->set("beginningOfTime",
    new json::integer_number(
        std::chrono::duration_cast<std::chrono::microseconds>(
            COMPILATION_START.time_since_epoch())
        .count()));
```
**详细转换过程**：
1. **`COMPILATION_START.time_since_epoch()`**
   - 获取从系统纪元（1970-01-01）到编译开始的时间间隔
   - 返回`std::chrono::duration`对象

2. **`duration_cast<std::chrono::microseconds>()`**
   - 将时间间隔转换为微秒精度
   - 可能涉及舍入，但微秒精度足够

3. **`.count()`**
   - 提取微秒计数值（`int64_t`类型）

4. **包装为JSON整数**
   - 创建`json::integer_number`对象
   - 设置为`beginningOfTime`字段

**功能意义**：
- **绝对时间参考**：允许将相对时间戳转换为绝对时间
- **多文件关联**：如果同时追踪多个编译，可以对齐时间线
- **调试信息**：知道编译发生的具体时间

**创建事件数组**
```cpp
output_json->set("traceEvents", new json::array());
```
- **标准字段**：Chrome Tracing要求事件存储在`traceEvents`数组中
- **数组容器**：所有事件按顺序添加到这个数组

**获取数组指针**
```cpp
output_events_list = (json::array*)output_json->get("traceEvents");
```
**功能原理**：
1. **`get("traceEvents")`**：从JSON对象中获取值指针
2. **类型转换**：返回值是`json::value*`，需要转换为具体类型
3. **性能优化**：保存指针避免每次添加事件时的查找开销
4. **缓存机制**：只需在初始化时获取一次

## **add_event()函数**
```cpp
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
```

### **详细功能原理**：

**静态变量**
```cpp
static int pid = getpid();
static int tid = 0;
static int UID = 0;
```
**设计原理**：
1. **单次初始化**：静态局部变量只在第一次进入函数时初始化
2. **进程ID**：`getpid()`系统调用获取当前进程ID
3. **线程ID**：GCC单线程编译，固定为0
4. **UID计数器**：从0开始递增，确保唯一性

**事件长度过滤**
```cpp
if ((event.ts.end - event.ts.start) < MINIMUM_EVENT_LENGTH_NS)
{
    return;  // 事件太短，直接返回
}
```
**过滤策略分析**：
- **计算持续时间**：`end - start`得到事件长度（纳秒）
- **比较阈值**：与1ms（1,000,000纳秒）比较
- **提前返回**：不满足条件直接返回，避免后续处理开销
- **优化效果**：可能过滤掉80%以上的微事件

**UID分配**
```cpp
int this_uid = UID++;
```
**分配机制**：
- **原子性**：单线程环境，`UID++`是安全的
- **递增性**：确保每个事件对的UID不同
- **范围**：从0开始，理论上支持4,294,967,296个事件

**生成JSON记录**
```cpp
output_events_list->append(
    new_event(event, pid, tid, event.ts.start, "B", this_uid));
output_events_list->append(
    new_event(event, pid, tid, event.ts.end, "E", this_uid));
```
**成对事件生成原理**：
1. **开始事件**（phase="B"）
   - 时间戳：`event.ts.start`
   - 表示事件开始

2. **结束事件**（phase="E"）
   - 时间戳：`event.ts.end`
   - 表示事件结束

3. **相同UID**：两个事件有相同的`this_uid`
4. **顺序保证**：开始事件在前，结束事件在后

**内存管理考虑**：
- **每次调用创建两个JSON对象**
- 由`output_events_list`管理生命周期
- 最终在`write_all_events()`中统一释放

## **write_all_events()函数**
```cpp
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
```

### **详细功能原理**：

**添加TU事件**
```cpp
add_event(TraceEvent{"TU", EventCategory::TU, {0, ns_from_start()}, std::nullopt});
```
**TU事件特殊性**：
1. **名称固定**："TU"表示Translation Unit
2. **时间范围**：从0（编译开始）到当前时间（编译结束）
3. **无参数**：`std::nullopt`表示没有额外参数
4. **总览作用**：提供整个编译过程的总时间

**按顺序写入事件**
```cpp
write_preprocessing_events();  // 1. 预处理事件
write_opt_pass_events();       // 2. 优化pass事件
write_all_functions();         // 3. 函数解析事件
write_all_scopes();            // 4. 作用域事件
```
**顺序设计原理**：
1. **时间线顺序**：基本按编译流程的时间顺序
2. **逻辑分组**：同类事件集中在一起便于分析
3. **依赖关系**：某些模块的写入函数会调用`add_event()`

**序列化JSON（版本兼容）**
```cpp
#if GCCPLUGIN_VERSION_MAJOR >= 14
    // GCC 14及以上版本：支持格式化参数
    output_json->dump(trace_file, /*formatted=*/false);
#else
    // GCC 13及以下版本：简化API
    output_json->dump(trace_file);
#endif
```
**版本兼容性处理**：

**GCC 14+ API**：
```cpp
// 原型：void dump(FILE* f, bool formatted = true) const;
output_json->dump(trace_file, false);  // 不格式化，紧凑输出
```
- **格式化控制**：`false`表示紧凑输出，无多余空格换行
- **文件大小**：紧凑输出可减少30-50%的文件大小
- **解析性能**：紧凑JSON解析更快

**GCC 13- API**：
```cpp
// 原型：void dump(FILE* f) const;
output_json->dump(trace_file);  // 默认可能格式化
```
- **简化API**：没有格式化参数
- **可能格式化**：不同版本行为可能不同

**条件编译原理**：
- **编译时判断**：根据GCC版本选择不同代码路径
- **宏定义**：`GCCPLUGIN_VERSION_MAJOR`由GCC提供
- **向后兼容**：确保插件在老版本GCC上也能工作

**关闭文件**
```cpp
fclose(trace_file);
```
**文件操作原理**：
1. **刷新缓冲区**：确保所有数据写入磁盘
2. **释放资源**：操作系统文件描述符
3. **错误处理**：忽略可能的错误（编译已结束）

**清理内存**
```cpp
output_events_list = nullptr;
delete output_json;
```
**内存清理原理**：

1. **`output_events_list = nullptr`**
   - 防止野指针：这个指针即将失效
   - 防御性编程：避免后续误用

2. **`delete output_json`**
   - **递归释放**：JSON库的delete会递归释放所有子对象
   - **内存回收**：释放整个JSON文档占用的内存
   - **必要性**：避免内存泄漏，插件可能被多次调用

**JSON对象树结构**：
```
output_json (object)
├── displayTimeUnit (string)
├── beginningOfTime (integer)
└── traceEvents (array)
    ├── 事件1 (object)
    │   ├── name (string)
    │   ├── ph (string)
    │   ├── ...
    │   └── args (object)
    ├── 事件2 (object)
    └── ...
```

**delete操作效果**：
- 从根对象开始递归delete
- 每个JSON对象负责delete其子对象
- 最终释放所有分配的内存

## **perf_output.cpp的核心功能流程**

```
初始化阶段：
1. init_output_file() 被调用
   ↓
2. 创建JSON根对象，设置元数据
   ↓
3. 创建事件数组，获取快速访问指针
   ↓

运行时阶段：
4. 各模块调用 add_event()
   ↓
5. 事件过滤（<1ms跳过）
   ↓
6. 分配UID，生成开始/结束事件对
   ↓
7. 添加到事件数组
   ↓

结束阶段：
8. write_all_events() 被调用
   ↓
9. 添加TU总时间事件
   ↓
10. 调用各模块输出函数
   ↓
11. 序列化JSON到文件（版本兼容）
   ↓
12. 关闭文件，清理内存
```

# **plugin.cpp 功能原理解析**

## **文件头注释**
```
// GCC性能追踪插件的主入口模块
// 负责插件初始化、回调函数注册和GCC事件处理
```
**功能原理**：这是整个插件的**中枢系统**，连接GCC编译器和追踪逻辑。负责在GCC编译流程的关键节点插入钩子（hooks），捕获编译事件。

## **包含头文件系统**

### **项目内部头文件**
```cpp
#include "plugin.h"             // 包含插件接口声明，提供回调函数的实现
```
**功能原理**：
- **模块连接**：`plugin.h`定义了插件对外的接口声明
- **声明-实现分离**：确保头文件中声明的函数在这里实现
- **编译检查**：编译器会验证函数签名的一致性

### **GCC内部头文件系统**
```cpp
#include <options.h>            // GCC编译选项处理和解析
#include <tree-check.h>         // GCC树节点验证和调试工具
#include <tree-pass.h>          // GCC优化pass定义和管理
#include <tree.h>               // GCC抽象语法树（AST）核心数据结构定义
#include <cp/cp-tree.h>         // C++特定的树节点类型和操作函数
#include "c-family/c-pragma.h"  // 预处理指令（#pragma）处理
#include "cpplib.h"             // C++预处理库核心实现
```

**详细功能原理分析**：

**`<options.h>`**
```cpp
// 提供GCC命令行选项解析机制
// 虽然本插件未直接使用，但可能需要理解GCC选项上下文
```

**`<tree-check.h>`**
```cpp
// 提供DEBUG_TREE宏和树节点验证函数
// 原理：GCC的树节点有复杂的内部结构，需要验证工具
// 用途：调试时确保tree节点的有效性
```

**`<tree-pass.h>`**
```cpp
// 核心结构：struct opt_pass
// 功能原理：
// 1. 定义GCC优化pass的基类结构
// 2. 包含pass名称、类型、执行函数指针等
// 3. 插件通过此头文件了解优化pass的结构
// 示例：
struct opt_pass {
    const char *name;            // pass名称
    opt_pass_type type;          // pass类型
    int static_pass_number;      // 静态编号
    // ... 其他成员
};
```

**`<tree.h>`**
```cpp
// GCC抽象语法树（AST）核心头文件
// 功能原理：
// 1. 定义tree类型：typedef struct tree_node *tree
// 2. tree_node是GCC内部表示程序结构的统一节点类型
// 3. 所有C/C++语法元素都表示为tree节点
// 关键宏：
DECL_NAME(tree)      // 获取声明名称
TREE_CODE(tree)      // 获取节点类型代码
DECL_CONTEXT(tree)   // 获取声明上下文（作用域）
```

**`<cp/cp-tree.h>`**
```cpp
// C++特定的树节点类型扩展
// 功能原理：
// 1. 定义C++专属的树节点代码（tree_code）
// 2. 例如：NAMESPACE_DECL, TEMPLATE_DECL等
// 3. 提供C++特定的宏和函数
// 关键树节点代码：
NAMESPACE_DECL    // C++命名空间声明
RECORD_TYPE       // 结构体/类类型
UNION_TYPE        // 联合体类型
```

**`"c-family/c-pragma.h"`**
```cpp
// 处理#pragma预处理指令
// 功能原理：虽然本插件不处理#pragma，但GCC可能需要这个头文件
// 用于GCC内部一致性检查
```

**`"cpplib.h"`**
```cpp
// C++预处理库核心头文件
// 功能原理：
// 1. 定义cpp_reader结构：GCC预处理器的状态机
// 2. 关键结构体：line_map_ordinary（行号映射）
// 3. 提供预处理回调函数机制
// 核心数据结构：
struct cpp_reader {
    struct cpp_callbacks *cb;  // 回调函数表
    // ... 预处理状态
};
struct line_map_ordinary {
    const char *file_name;    // 文件名
    int reason;               // 变更原因（LC_ENTER/LC_LEAVE）
};
```

## **GPL兼容性声明**
```cpp
int plugin_is_GPL_compatible = 1;
```

**详细功能原理**：

**法律层面**：
- **GPL要求**：GCC本身是GPL许可证，插件必须兼容GPL才能链接使用
- **技术限制**：GCC插件API被视为GCC的"衍生作品"
- **值1含义**：表示插件代码以GPLv3或更高版本发布

**编译层面**：
```cpp
// GCC内部检查机制：
extern "C" int plugin_is_GPL_compatible;

// 编译时GCC会检查：
1. 查找这个符号
2. 验证值为非零
3. 如果未定义或为0，拒绝加载插件

// 技术实现：
// GCC在dlopen()插件后，通过dlsym()查找这个符号
// 这是GCC插件的安全机制
```

## **命名空间**
```cpp
namespace GccTrace
{
```

**功能原理**：
- **作用域隔离**：避免与GCC内部或其他插件的符号冲突
- **模块化设计**：清晰标识属于本项目的代码
- **C++特性**：相比C的static函数，提供更好的封装性

## **GCC回调函数实现**

### **cb_finish_parse_function函数**

```cpp
void cb_finish_parse_function(void* gcc_data, void* user_data)
```

**函数签名原理**：
```cpp
// GCC回调函数的标准签名
// 参数1: void* gcc_data - GCC传递的数据（类型擦除）
// 参数2: void* user_data - 用户数据（插件注册时传入，通常为nullptr）
// 返回: void - 所有GCC回调都返回void
```

**类型转换**
```cpp
tree decl = (tree)gcc_data;
```
**功能原理**：
- **tree类型**：GCC内部表示语法树的节点指针
- **类型擦除**：GCC回调使用void*保持通用性
- **安全转换**：信任GCC传入的是合法的tree节点

**获取源代码位置**
```cpp
auto expanded_location = expand_location(decl->decl_minimal.locus);
```
**详细分析**：
```cpp
// decl->decl_minimal.locus 结构：
struct decl_minimal {
    location_t locus;  // 位置信息的压缩表示
};

// expand_location() 函数：
// 输入：压缩的位置编码（location_t）
// 输出：expanded_location结构体
struct expanded_location {
    const char *file;   // 源文件名
    int line;           // 行号
    int column;         // 列号
};
```

**获取函数名称**
```cpp
auto decl_name = decl_as_string(decl, 0);
```
**功能原理**：
```cpp
// decl_as_string() 是GCC内置函数
// 参数1: tree节点
// 参数2: 标志位（0表示默认）
// 返回值: const char* 格式化后的字符串

// 示例转换：
// 原始函数: void foo::bar(int x)
// 转换后: "foo::bar(int)"
// 注意：包含命名空间、类名，但不包含返回类型
```

**获取父作用域**
```cpp
auto parent_decl = DECL_CONTEXT(decl);
```
**DECL_CONTEXT宏原理**：
```cpp
// 在tree.h中定义：
#define DECL_CONTEXT(NODE) (DECL_CHECK(NODE)->decl_common.context)

// 功能：获取声明所在的上下文（作用域）
// 可能返回：
// 1. NAMESPACE_DECL - C++命名空间
// 2. RECORD_TYPE - 结构体/类
// 3. TRANSLATION_UNIT_DECL - 全局作用域
// 4. NULL - 无上下文
```

**初始化作用域信息**
```cpp
const char* scope_name = nullptr;
GccTrace::EventCategory scope_type = GccTrace::EventCategory::UNKNOWN;
```
**设计原理**：
- **nullptr默认**：表示全局作用域或无作用域
- **类型安全**：使用枚举而非整数表示类型
- **UNKNOWN初始值**：防止未初始化错误

**作用域信息提取**
```cpp
if (parent_decl)
{
    if (TREE_CODE(parent_decl) != TRANSLATION_UNIT_DECL)
    {
        scope_name = decl_as_string(parent_decl, 0);
        
        switch (TREE_CODE(parent_decl))
        {
            case NAMESPACE_DECL:
                scope_type = GccTrace::EventCategory::NAMESPACE;
                break;
            case RECORD_TYPE:
                scope_type = GccTrace::EventCategory::STRUCT;
                break;
            case UNION_TYPE:
                scope_type = GccTrace::EventCategory::STRUCT;
                break;
            default:
                fprintf(stderr, "Unkown tree code %d\n", TREE_CODE(parent_decl));
                break;
        }
    }
}
```

**TREE_CODE宏详细原理**：
```cpp
// 在tree.h中定义：
#define TREE_CODE(NODE) ((enum tree_code)(NODE)->base.code)

// 功能：获取tree节点的类型代码
// tree_code是包含数百种节点类型的枚举

// 本插件关注的节点类型：
TRANSLATION_UNIT_DECL = 1,   // 翻译单元（全局作用域）
NAMESPACE_DECL = 115,        // C++命名空间
RECORD_TYPE = 6,             // 结构体/类类型
UNION_TYPE = 7,              // 联合体类型

// 设计原理：用整数代码高效表示节点类型
```

**switch语句设计原理**：
1. **NAMESPACE_DECL** → **NAMESPACE**：标准C++命名空间
2. **RECORD_TYPE** → **STRUCT**：包含结构体、类、模板类
3. **UNION_TYPE** → **STRUCT**：联合体也归类为结构类型
4. **default分支**：未知类型输出警告，帮助调试

**传递函数信息**
```cpp
end_parse_function(FinishedFunction{
    gcc_data,                // GCC树节点指针（保持原始类型）
    decl_name,               // 函数签名
    expanded_location.file,  // 定义所在的源文件
    scope_name,              // 所属作用域名称（可为空）
    scope_type               // 作用域类型
});
```

**结构体初始化原理**：
```cpp
// C++20指定的初始化语法（designated initializers）
// 优点：清晰明确，不依赖字段顺序

FinishedFunction{
    .decl = gcc_data,                // 原始指针，用于调试或扩展
    .name = decl_name,               // 函数签名（如"std::vector::push_back"）
    .file_name = expanded_location.file, // 绝对路径或相对路径
    .scope_name = scope_name,        // 可能为nullptr（全局函数）
    .scope_type = scope_type         // 作用域类型的枚举
};

// 数据流转：GCC回调 → FinishedFunction → tracking.cpp处理
```

### **cb_plugin_finish函数**
```cpp
void cb_plugin_finish(void* gcc_data, void* user_data)
{
    write_all_events();
}
```

**编译结束回调原理**：
- **触发时机**：GCC完成所有编译工作后，即将退出时
- **参数忽略**：通常为nullptr，不需要处理
- **核心功能**：触发最终的JSON输出和资源清理

**执行流程**：
```
cb_plugin_finish() 被GCC调用
    ↓
write_all_events() 执行
    ↓
1. 添加TU总时间事件
2. 写入预处理事件
3. 写入优化pass事件  
4. 写入函数事件
5. 写入作用域事件
6. 序列化JSON到文件
7. 清理内存
```

### **old_file_change_cb函数指针**
```cpp
void (*old_file_change_cb)(cpp_reader*, const line_map_ordinary*);
```

**函数指针原理**：
```cpp
// 声明格式：返回类型 (*指针名)(参数列表)
// 作用：保存GCC原始的回调函数指针

// 对比学习：
typedef void (*CallbackType)(cpp_reader*, const line_map_ordinary*);
CallbackType old_file_change_cb;  // 等价声明
```

**Hook设计模式**：
```cpp
// 经典Hook实现三部曲：
1. 保存原始指针：old_cb = current_cb
2. 替换为新函数：current_cb = my_cb  
3. 在my_cb中调用old_cb：old_cb(...)

// 本插件实现：
cpp_cbs->file_change = &cb_file_change;
// 在cb_file_change()中：(*old_file_change_cb)(...);
```

### **cb_file_change函数**

**检查新映射**
```cpp
if (new_map)
{
    const char* file_name = ORDINARY_MAP_FILE_NAME(new_map);
```
**原理分析**：
- **new_map**：新的行号映射，表示文件切换
- **ORINARY_MAP_FILE_NAME宏**：从line_map_ordinary获取文件名
- **可能为空**：某些文件（如命令行）没有文件名

**根据变更原因处理**
```cpp
if (file_name)
{
    switch (new_map->reason)
    {
        case LC_ENTER:
            start_preprocess_file(file_name, pfile);
            break;
        case LC_LEAVE:
            end_preprocess_file();
            break;
        default:
            // 忽略其他原因的文件变更
            break;
    }
}
```

**line_map_ordinary::reason枚举原理**：
```cpp
// 在line-map.h中定义：
enum lc_reason {
    LC_ENTER = 0,    // 进入新文件（开始#include）
    LC_LEAVE = 1,    // 离开当前文件
    LC_RENAME = 2,   // 文件重命名（很少见）
    // ... 其他原因
};

// 工作原理：
// GCC预处理每个文件时创建line_map_ordinary结构
// reason字段表示为什么创建这个映射
```

**调用原始回调**
```cpp
(*old_file_change_cb)(pfile, new_map);
```

**链式调用原理**：
```cpp
// 确保不破坏GCC原有功能
// 可能还有其他插件或GCC内部依赖这个回调

// 调用流程：
用户代码 → GCC预处理 → cb_file_change（插件） → old_file_change_cb（原始） → GCC内部处理

// 错误处理：如果old_file_change_cb为nullptr？
// 理论上不会，但安全起见应该检查
```

### **cb_start_compilation函数**

**开始主文件预处理**
```cpp
start_preprocess_file(main_input_filename, nullptr);
```

**main_input_filename原理**：
```cpp
// GCC全局变量定义：
extern const char *main_input_filename;

// 特性：
1. 指向命令行指定的主源文件
2. 可能是相对路径或绝对路径
3. 在编译开始时设置
4. pfile=nullptr：主文件没有包含路径信息
```

**Hook预处理回调**
```cpp
cpp_callbacks* cpp_cbs = cpp_get_callbacks(parse_in);
old_file_change_cb = cpp_cbs->file_change;
cpp_cbs->file_change = &cb_file_change;
```

**cpp_get_callbacks详细原理**：
```cpp
// 函数原型：cpp_callbacks *cpp_get_callbacks(cpp_reader*)

// cpp_reader结构：
struct cpp_reader {
    struct cpp_callbacks {
        void (*file_change)(cpp_reader*, const line_map_ordinary*);
        // ... 其他20+个回调函数
    } *cb;
};

// parse_in是什么？
// 在插件上下文中，parse_in是GCC内部的cpp_reader实例
// 表示当前翻译单元的预处理器状态
```

**回调替换时机原理**：
- **编译开始时**：确保能捕获所有文件包含事件
- **替代方案**：在插件初始化时替换（可能错过早期事件）
- **安全性**：保存原始指针，避免内存泄漏

### **cb_pass_execution函数**
```cpp
void cb_pass_execution(void* gcc_data, void* user_data)
{
    auto pass = (opt_pass*)gcc_data;
    start_opt_pass(pass);
}
```

**opt_pass结构关键成员**：
```cpp
struct opt_pass {
    const char *name;                 // pass名称（如"expand")
    opt_pass_type type;               // pass类型枚举
    int static_pass_number;          // 唯一编号
    struct opt_pass *sub;            // 子pass链表
    struct opt_pass *next;           // 下一个pass
    // ... 执行函数等其他成员
};
```

**优化pass执行流程**：
```
GCC优化阶段开始
    ↓
cb_pass_execution() 第一次调用
    ↓
start_opt_pass(pass1) 开始追踪pass1
    ↓
pass1执行...
    ↓
cb_pass_execution() 第二次调用  
    ↓
start_opt_pass(pass2) // 自动结束pass1，开始pass2
    ↓
...重复直到所有pass完成
```

### **cb_finish_decl函数**
```cpp
void cb_finish_decl(void* gcc_data, void* user_data)
{
    finish_preprocessing_stage();
}
```

**声明处理完成时机原理**：
```cpp
// 什么是"声明"？
// 在C/C++中：函数声明、变量声明、类型声明等

// 为什么此时结束预处理？
// 编译流程：预处理 → 声明解析 → 函数体解析 → 优化
// 声明解析完成后，预处理阶段理论上结束

// 安全考虑：可能有未关闭的#include（如错误情况）
// finish_preprocessing_stage()会清理所有未关闭文件
```

## **插件全局函数**

### **PLUGIN_NAME常量**
```cpp
static const char* PLUGIN_NAME = "gperf";
```

**命名原理**：
```cpp
// "gperf" = "GCC Performance"的缩写
// 与Google的gperftools命名相似，便于记忆

// GCC插件命名约定：
// 通常简短、小写、描述性
// 在命令行中使用：-fplugin=libgperf.so
```

### **setup_output函数**

**参数名称定义**
```cpp
const char* flag_name = "trace";
const char* dir_flag_name = "trace-dir";
```

**GCC插件参数传递原理**：
```cpp
// 命令行格式：
gcc -fplugin=libgperf.so -fplugin-arg-gperf-trace=output.json

// 解析后：
plugin_argument argv[] = {
    {.key = "trace", .value = "output.json"}
};

// 设计原理：
// "trace": 直接指定输出文件
// "trace-dir": 指定输出目录，自动生成文件名
```

**文件句柄初始化**
```cpp
FILE* trace_file = nullptr;
```
**nullptr vs NULL**：
```cpp
// C++11引入nullptr，类型安全
// NULL本质上是整数0，可能引发重载歧义
// 现代C++推荐使用nullptr
```

**情况1-无参数**
```cpp
if (argc == 0)
{
    char file_template[] = "/tmp/trace_XXXXXX.json";
    int fd = mkstemps(file_template, 5);
    trace_file = fdopen(fd, "w");
}
```

**mkstemps函数原理**：
```cpp
// 原型：int mkstemps(char *template, int suffixlen)
// 功能：创建唯一的临时文件

// 工作原理：
1. template必须包含6个连续的'X'：XXXXXX
2. mkstemps用随机字符替换XXXXXX
3. suffixlen是后缀长度（.json = 5）
4. 返回文件描述符fd

// 示例转换：
"/tmp/trace_XXXXXX.json" → "/tmp/trace_abc123.json"

// 安全性：避免临时文件冲突
```

**情况2-直接指定文件**
```cpp
else if (argc == 1 && !strcmp(argv[0].key, flag_name))
{
    trace_file = fopen(argv[0].value, "w");
    if (!trace_file)
    {
        fprintf(stderr, "GPERF Error! Couldn't open %s for writing\n", 
                argv[0].value);
    }
}
```

**文件打开模式**：
```cpp
// "w"模式特性：
1. 写入模式，文件不存在则创建
2. 文件存在则截断为0
3. 返回FILE*指针，失败返回NULL

// 错误处理：输出详细错误信息
// 注意：fopen不设置errno，perror()可能不准确
```

**情况3-指定目录**
```cpp
else if (argc == 1 && !strcmp(argv[0].key, dir_flag_name))
{
    std::string file_template{argv[0].value};
    file_template += "/trace_XXXXXX.json";
    
    int fd = mkstemps(file_template.data(), 5);
    trace_file = fdopen(fd, "w");
}
```

**C++字符串与C接口兼容**：
```cpp
// file_template.data() 原理：
// C++17前：返回const char*，不能修改
// C++17起：data()返回char*（如果字符串非const）
// 这里使用.data()而非.c_str()，因为mkstemps需要可修改

// 安全考虑：确保目录存在且有写入权限
// 改进建议：可添加mkdir()创建目录
```

**情况4-参数错误**
```cpp
else
{
    fprintf(stderr,
        "GPERF Error! Arguments must be -fplugin-arg-%s-%s=FILENAME or "
        "-fplugin-arg-%s-%s=DIRECTORY\n",
        PLUGIN_NAME, flag_name, PLUGIN_NAME, dir_flag_name);
    return false;
}
```

**错误信息格式化原理**：
```cpp
// 帮助用户理解正确格式：
-fplugin-arg-gperf-trace=FILENAME
或
-fplugin-arg-gperf-trace-dir=DIRECTORY

// 设计考虑：清晰、具体、可操作的错误信息
```

**初始化输出**
```cpp
if (trace_file)
{
    GccTrace::init_output_file(trace_file);
    return true;
}
else
{
    return false;
}
```

**初始化流程**：
```
setup_output()成功
    ↓
trace_file有效
    ↓  
init_output_file(trace_file)
    ↓
创建JSON根对象
设置元数据
获取事件数组指针
    ↓
返回true，插件继续初始化
```

### **plugin_init函数**

**插件信息结构**
```cpp
static struct plugin_info gcc_trace_info = {
    .version = "V1.0",
    .help = "GccTrace time traces of the compilation."
};
```

**plugin_info结构原理**：
```cpp
struct plugin_info {
    const char *version;  // 插件版本
    const char *help;     // 帮助信息（显示在gcc --help=plugin）
    // 可能还有其他字段
};

// 设计用途：
1. version: 用于兼容性检查和调试
2. help: 用户可通过gcc --help=plugin查看插件信息
```

**记录编译开始时间**
```cpp
GccTrace::COMPILATION_START = GccTrace::clock_t::now();
```

**时间基准原理**：
```cpp
// COMPILATION_START是全局时间点
// 所有事件时间戳都以此为基准计算相对时间

// 为什么不使用绝对时间？
// 1. 相对时间更直观（编译耗时多少）
// 2. 避免系统时间跳变影响
// 3. 便于多个编译过程的时间对齐
```

**设置输出系统**
```cpp
if (!setup_output(plugin_info->argc, plugin_info->argv))
{
    return -1;  // 初始化失败
}
```

**插件初始化失败处理**：
```cpp
// 返回-1表示插件初始化失败
// GCC会：1. 输出错误信息 2. 卸载插件 3. 继续编译（如果没有插件依赖）

// 为什么不是返回0继续编译？
// 如果输出系统失败，插件无法正常工作，应该完全禁用
```

**回调函数注册**

**register_callback函数原理**：
```cpp
// 原型：void register_callback(const char *plugin_name,
//                             enum plugin_event event,
//                             plugin_callback_func callback,
//                             void *user_data);

// 参数解析：
1. plugin_name: 插件标识，用于调试和错误报告
2. event: 事件类型枚举（何时调用回调）
3. callback: 回调函数指针
4. user_data: 传递给回调的用户数据（通常nullptr）

// 注册顺序的设计原理：
1. PLUGIN_INFO最早注册（基本信息）
2. PLUGIN_START_UNIT（编译开始时）
3. PLUGIN_FINISH_DECL（声明结束时）
4. PLUGIN_FINISH_PARSE_FUNCTION（函数解析完时）
5. PLUGIN_PASS_EXECUTION（优化pass执行时）
6. PLUGIN_FINISH（编译结束时）

// 这个顺序基本按照编译流程时间线
```

**各个事件类型的详细原理**：

**1. PLUGIN_START_UNIT**
```cpp
// 触发时机：开始编译一个翻译单元（源文件）
// 对应函数：cb_start_compilation
// 作用：开始主文件预处理，Hook预处理回调
```

**2. PLUGIN_FINISH_DECL**
```cpp
// 触发时机：完成一个顶层声明的解析
// 对应函数：cb_finish_decl  
// 作用：标记预处理阶段结束
```

**3. PLUGIN_FINISH_PARSE_FUNCTION**
```cpp
// 触发时机：完成一个函数体的解析（语法分析）
// 对应函数：cb_finish_parse_function
// 作用：记录函数解析耗时和上下文信息
```

**4. PLUGIN_PASS_EXECUTION**
```cpp
// 触发时机：开始执行一个优化pass
// 对应函数：cb_pass_execution
// 作用：记录优化pass的执行耗时
```

**5. PLUGIN_FINISH**
```cpp
// 触发时机：整个编译过程完成
// 对应函数：cb_plugin_finish
// 作用：触发最终输出和清理
```

**返回成功**
```cpp
return 0;  // 插件初始化成功
```

**返回值含义**：
```cpp
// 0: 成功，GCC加载插件
// -1: 失败，GCC卸载插件
// 其他值：保留，目前都视为失败
```

## **编译流程与插件交互的完整时序**

```
时序图：

GCC启动
    ↓
plugin_init()              // 插件初始化
    ↓                       注册6个回调函数
GCC开始编译 main.cpp
    ↓
cb_start_compilation()     // 开始编译单元
    ↓                       Hook预处理回调
GCC预处理阶段
    ↓
cb_file_change()多次调用  // 处理每个#include
    ↓                     记录文件开始/结束时间
GCC解析声明
    ↓  
cb_finish_decl()          // 声明解析完成
    ↓                     结束预处理阶段
GCC解析函数
    ↓
cb_finish_parse_function() // 每个函数解析完成
    ↓                     记录函数信息
GCC优化阶段
    ↓
cb_pass_execution()多次调用 // 每个优化pass
    ↓                     记录pass执行时间
GCC完成编译
    ↓
cb_plugin_finish()        // 编译完成
    ↓                     写入所有事件到JSON
插件卸载
```

# **tracking.cpp 功能原理解析**

## **文件头注释**
```
// GCC性能追踪的数据管理模块
// 负责编译事件的数据收集、处理和存储
```
**功能原理**：这是插件的**核心数据引擎**，负责收集、处理和存储所有编译事件数据。它是插件回调系统和输出系统之间的桥梁。

## **包含头文件系统**

### **GCC插件API**
```cpp
#include <gcc-plugin.h>          // GCC插件框架核心头文件（提供插件API）
```
**功能原理**：
- **基础依赖**：虽然本模块主要处理数据，但仍需要GCC插件环境
- **宏定义**：提供GCC内部宏和常量定义
- **内存管理**：GCC内部可能使用特定的内存分配器

### **C++标准库容器**
```cpp
#include <stack>                 // 标准库：栈容器（用于预处理文件包含栈管理）
#include <string>                // 标准库：字符串（存储文件名、作用域名等）
#include <vector>                // 标准库：向量容器（存储事件列表，支持快速遍历）
```
**详细功能原理**：

**`<stack>`**：
```cpp
// 为什么用stack而不是vector？
// 1. 文件包含是典型的后进先出（LIFO）结构
// 2. stack提供清晰的语义：push(进入文件), pop(离开文件)
// 3. 最小接口：不需要随机访问，只需要栈顶操作

// 底层实现：默认使用deque，内存连续性好
std::stack<std::string> preprocessing_stack;
// 等效于：std::deque<std::string> + push_back/pop_back
```

**`<string>`**：
```cpp
// std::string vs const char*
// 选择std::string的原因：
// 1. 生命周期管理：自动管理内存，避免悬垂指针
// 2. 值语义：可以安全存储在容器中
// 3. 操作丰富：支持starts_with、substr等C++20方法

// 内存考虑：文件名可能很长，string使用SSO（短字符串优化）
// SSO原理：短字符串（<16字符）存储在栈上，长字符串堆分配
```

**`<vector>`**：
```cpp
// 为什么事件列表用vector而不是list？
// 1. 缓存友好：连续内存，预取效率高
// 2. 遍历性能：O(1)随机访问，适合最终批量输出
// 3. 内存效率：没有链表节点的额外开销

// 设计模式：收集时emplace_back，输出时顺序遍历
std::vector<ScopeEvent> scope_events;      // 作用域事件
std::vector<FunctionEvent> function_events; // 函数事件
```

### **GCC内部头文件**
```cpp
#include "c-family/c-pragma.h"   // GCC预处理指令支持（#pragma处理）
#include "cpplib.h"              // GCC C++预处理库（cpp_reader等预处理状态机）
#include "tracking.h"            // 项目内部头文件：本模块的接口声明
#include <tree-pass.h>           // GCC优化pass定义（opt_pass结构体和类型枚举）
```

**`c-family/c-pragma.h`**
```cpp
// 虽然不是直接处理#pragma，但包含可能需要的类型定义
// GCC内部头文件之间有复杂的依赖关系
```

**`cpplib.h`**
```cpp
// 关键结构：cpp_reader、line_map_ordinary
// 提供预处理器的完整状态信息
// 用于文件包含追踪和路径解析
```

**`tracking.h`**
```cpp
// 自包含声明：确保实现与声明一致
// 包含本模块所有公共接口的声明
```

**`<tree-pass.h>`**
```cpp
// 提供opt_pass结构定义和优化pass类型枚举
// 关键枚举：opt_pass_type（GIMPLE_PASS, RTL_PASS等）
```

## **命名空间和全局变量**
```cpp
namespace GccTrace
{
    // 全局编译开始时间点定义（在comm.h中声明）
    time_point_t COMPILATION_START;
```

**COMPILATION_START全局变量**：
```cpp
// 声明-定义模式：
// comm.h中声明：extern time_point_t COMPILATION_START;
// tracking.cpp中定义：time_point_t COMPILATION_START;

// 为什么在这里定义？
// 1. 单一定义原则：避免多个编译单元重复定义
// 2. 初始化控制：在plugin.cpp的plugin_init()中初始化
// 3. 可见性：所有内部函数都需要访问这个时间基准
```

## **匿名命名空间**
```cpp
namespace // 匿名命名空间，限制符号只在当前文件可见
```
**设计原理**：
- **封装性**：所有内部数据结构对外完全隐藏
- **链接安全**：避免与其他编译单元的符号冲突
- **访问控制**：强制通过公共接口访问数据

## **预处理追踪数据结构**

### **时间记录映射**
```cpp
map_t<std::string, int64_t> preprocess_start;  // 文件 -> 开始时间（纳秒）
map_t<std::string, int64_t> preprocess_end;    // 文件 -> 结束时间（纳秒）
```

**详细设计原理**：
```cpp
// 为什么用两个map而不是一个包含时间跨度的map？
// 1. 开始和结束可能在不同时间记录
// 2. 文件可能被多次包含（多版本编译）
// 3. 循环包含的特殊处理

// map_t是类型别名：using map_t = std::unordered_map<Key, Value>
// 为什么用unordered_map而不是map？
// 1. 哈希表平均O(1)查找，比红黑树O(log n)快
// 2. 不需要按键排序（输出时顺序不重要）
// 3. 文件名作为键，哈希冲突概率低

// 键类型：std::string（完整文件路径）
// 值类型：int64_t（纳秒时间戳）
```

### **预处理文件栈**
```cpp
std::stack<std::string> preprocessing_stack;
```

**栈状态示例**：
```cpp
// 编译main.cpp包含a.h，a.h包含b.h
开始编译：
栈: [] (空)

处理main.cpp：
栈: ["main.cpp"]

处理#include "a.h"：
栈: ["main.cpp", "a.h"]

处理#include "b.h"：  
栈: ["main.cpp", "a.h", "b.h"]

处理完b.h：
栈: ["main.cpp", "a.h"]

处理完a.h：
栈: ["main.cpp"]

处理完main.cpp：
栈: []
```

### **循环包含毒丸值**
```cpp
const char* CIRCULAR_POISON_VALUE = "CIRCULAR_POISON_VALUE";
```

**循环包含问题原理**：
```cpp
// 场景：
// a.h: #include "b.h"
// b.h: #include "a.h"  // 循环包含！

// 处理流程：
1. 开始a.h → 栈: [a.h]
2. 进入b.h → 栈: [a.h, b.h]  
3. 再次进入a.h（检测到循环）→ 使用毒丸值
4. 毒丸值作用：标记但不追踪内层包含

// 为什么需要毒丸值？
// 避免无限递归和栈溢出
// 保持数据结构一致性
```

### **函数时间戳基准**
```cpp
TimeStamp last_function_parsed_ts = 0;
```

**时间戳调整原理**：
```cpp
// 问题：连续函数可能在相同时间开始/结束
// 原因：函数解析时间可能小于计时器分辨率

// 解决方案：强制时间戳不同
last_function_parsed_ts + 3  // 每个函数开始时间+3ns
now + 3                      // 函数结束时间+3ns  

// 效果：确保每个函数事件时间跨度不重叠
// 在Chrome Tracing中正确显示为独立事件
```

## **优化pass追踪数据结构**

### **OptPassEvent结构**
```cpp
struct OptPassEvent
{
    const opt_pass* pass;  // GCC优化pass对象
    TimeSpan ts;           // pass执行的时间跨度
};
```

**opt_pass结构关键成员**：
```cpp
struct opt_pass {
    const char *name;                 // pass名称，如"expand"
    opt_pass_type type;               // 类型枚举
    int static_pass_number;           // 静态编号（编译时确定）
    // 其他：next, sub, execute函数指针等
};
```

**为什么存储指针而不是复制**：
```cpp
// 1. opt_pass是GCC内部结构，可能很大
// 2. 指针大小固定（8字节），复制成本低
// 3. pass对象生命周期与编译过程一致
// 4. 通过指针访问原始pass信息（名称、类型、编号）
```

### **pass事件存储**
```cpp
OptPassEvent last_pass;                  // 当前正在执行的pass
std::vector<OptPassEvent> pass_events;   // 所有pass的历史记录
```

**双缓冲设计原理**：
```cpp
// last_pass: 当前活跃pass的临时存储
// pass_events: 已完成的pass永久记录

// 工作流程：
1. start_opt_pass(pass1) → last_pass = {pass1, {start, 0}}
2. start_opt_pass(pass2) → 
   a. last_pass.ts.end = now
   b. pass_events.push_back(last_pass)  // 保存pass1
   c. last_pass = {pass2, {now+1, 0}}   // 开始pass2
3. 重复...

// 为什么需要vector存储？
// 最终需要按顺序输出所有pass事件
```

## **文件名规范化系统**

### **设计目标**：
```
原始路径: /usr/include/c++/11/iostream
包含目录: /usr/include/c++/11
规范化: iostream

原始路径: /home/user/project/src/utils.h  
包含目录: /home/user/project
规范化: src/utils.h
```

### **映射数据结构**
```cpp
map_t<std::string, std::string> file_to_include_directory;
map_t<std::string, std::string> normalized_files_map;
set_t<std::string> normalized_files;
set_t<std::string> conflicted_files;
```

**四层映射系统原理**：

**1. file_to_include_directory**
```cpp
// 键：完整文件路径
// 值：包含该文件的目录路径
// 用途：记录文件的包含关系
// 示例：{"/usr/include/stdio.h": "/usr/include"}
```

**2. normalized_files_map**
```cpp
// 键：完整文件路径
// 值：规范化后的相对路径
// 用途：路径转换查找表
// 示例：{"/usr/include/stdio.h": "stdio.h"}
```

**3. normalized_files**
```cpp
// 值：规范化文件名（相对路径）
// 用途：快速检查是否已存在相同规范化名称
// 实现：unordered_set，O(1)查找
```

**4. conflicted_files**
```cpp
// 值：冲突的规范化文件名
// 用途：标记有歧义的文件名
// 场景：不同目录有相同相对路径的文件
// 示例：/project1/src/main.cpp 和 /project2/src/main.cpp
//       都规范化成"src/main.cpp" → 冲突！
```

### **register_include_location函数**

**检查是否已注册**
```cpp
if (!file_to_include_directory.contains(file_name))
```
**C++20 contains方法原理**：
```cpp
// 传统方法：find() != end()
if (file_to_include_directory.find(file_name) != 
    file_to_include_directory.end())

// C++20 contains：更清晰、可能更高效
// 实现可能是：return find(key) != end();
```

**存储包含关系**
```cpp
std::string file_std = file_name;
file_to_include_directory[file_name] = dir_name;
auto& folder_std = file_to_include_directory[file_std];
```

**引用使用原理**：
```cpp
// auto& folder_std 创建一个引用
// 目的：避免后续查找的开销
// 等价于：
std::string& folder_std = file_to_include_directory[file_std];
```

**starts_with检查**
```cpp
if (file_std.starts_with(folder_std))
```
**C++20 starts_with原理**：
```cpp
// 传统方法：比较子字符串
if (file_std.substr(0, folder_std.size()) == folder_std)

// C++20 starts_with：更清晰、可能优化
// 实现：memcmp或SSE指令优化
```

**计算相对路径**
```cpp
auto normalized_file = file_std.substr(folder_std.size() + 1);
```
**+1原理**：
```cpp
// 假设：
file_std = "/usr/include/stdio.h" (长度24)
folder_std = "/usr/include" (长度13)

// 计算：
folder_std.size() = 13
+1 = 14  // 跳过路径分隔符'/'
substr(14) = "stdio.h"
```

**存储映射**
```cpp
normalized_files_map[file_std] = normalized_file;
```

**冲突检测**
```cpp
if (normalized_files.contains(normalized_file))
{
    conflicted_files.insert(normalized_file);
}
else
{
    normalized_files.insert(normalized_file);
}
```

**冲突处理策略**：
```cpp
// 第一次看到"stdio.h"：normalized_files插入
// 第二次看到"stdio.h"（可能来自不同目录）：
// 1. conflicted_files插入"stdio.h" 
// 2. 后续所有"stdio.h"都视为冲突
// 3. 冲突文件使用原始路径显示
```

**路径异常处理**
```cpp
else
{
    fprintf(stderr, "GPERF warning: Can't normalize paths %s and %s\n", 
            file_name, dir_name);
}
```
**可能的异常场景**：
```cpp
// 1. 符号链接导致路径不一致
// 2. 网络文件系统路径问题
// 3. GCC预处理器的路径计算错误
```

### **normalized_file_name函数**

**无冲突返回相对路径**
```cpp
if (normalized_files_map.contains(file_name) &&
    !conflicted_files.contains(normalized_files_map[file_name]))
{
    return normalized_files_map[file_name].data();
}
```

**安全访问链**：
```cpp
1. 检查文件是否已注册：contains(file_name)
2. 获取规范化名称：normalized_files_map[file_name]
3. 检查是否冲突：contains(normalized_name)
4. 返回：data()获取C风格字符串
```

**冲突或未注册返回原始路径**
```cpp
else
{
    return file_name;
}
```

**设计哲学**：
- **优雅降级**：冲突时使用原始路径，仍能工作
- **信息保留**：原始路径包含完整信息
- **用户友好**：冲突时可能在日志中警告

### **pass_type函数**

**类型转换switch**
```cpp
switch (type)
{
    case opt_pass_type::GIMPLE_PASS:
        return GIMPLE_PASS;
    case opt_pass_type::RTL_PASS:
        return RTL_PASS;
    case opt_pass_type::SIMPLE_IPA_PASS:
        return SIMPLE_IPA_PASS;
    case opt_pass_type::IPA_PASS:
        return IPA_PASS;
}
```

**GCC优化pass类型详解**：

**GIMPLE_PASS**：
```cpp
// GIMPLE：GCC的高级中间表示
// 特点：与机器无关，控制流图形式
// 典型pass：树优化、死代码消除、内联等
```

**RTL_PASS**：
```cpp  
// RTL：Register Transfer Language
// 特点：低级表示，接近汇编
// 典型pass：寄存器分配、指令调度、窥孔优化
```

**SIMPLE_IPA_PASS**：
```cpp
// IPA：Interprocedural Analysis（过程间分析）
// SIMPLE：轻量级，限制过程间信息
// 典型pass：局部过程间优化
```

**IPA_PASS**：
```cpp
// 完整的过程间分析
// 典型pass：全程序优化、跨模块内联
```

**默认返回UNKNOWN**
```cpp
return UNKNOWN;
```
**防御性编程**：处理未知pass类型，避免崩溃

## **函数和作用域事件存储**

### **ScopeEvent结构**
```cpp
struct ScopeEvent
{
    std::string name;       // 作用域名称
    EventCategory type;     // 作用域类型（STRUCT 或 NAMESPACE）
    TimeSpan ts;            // 时间跨度
};
std::vector<ScopeEvent> scope_events;
```

**作用域事件示例**：
```cpp
// 编译以下代码时：
namespace ns1 {
    class A {
        void foo() {}  // 函数1
        void bar() {}  // 函数2
    };
}

// 生成的scope_events：
[
    {name: "ns1", type: NAMESPACE, ts: {t1, t4}},
    {name: "A", type: STRUCT, ts: {t2, t3}}
]
```

### **FunctionEvent结构**
```cpp
struct FunctionEvent
{
    std::string name;      // 函数签名（包含命名空间和类名）
    const char* file_name; // 定义所在的源文件
    TimeSpan ts;           // 解析时间跨度
};
std::vector<FunctionEvent> function_events;
```

**文件指针设计**：
```cpp
// 为什么file_name用const char*而非std::string？
// 1. 通常是指向字符串字面量或GCC内部字符串
// 2. 不需要修改，只需要引用
// 3. 节省内存：避免复制长文件路径

// 安全假设：文件名字符串在编译期间有效
// 实际存储在expanded_location.file中，生命周期足够
```

## **公共接口实现**

### **finish_preprocessing_stage函数**

**循环清理栈**
```cpp
while (!preprocessing_stack.empty())
{
    end_preprocess_file();
    last_function_parsed_ts = ns_from_start();
}
```

**强制结束场景**：
```cpp
// 1. 编译错误提前退出
// 2. 声明处理完成（cb_finish_decl回调）
// 3. 写入预处理事件前的安全检查

// 为什么需要while循环？
// 处理嵌套包含未正常关闭的情况
```

**更新时间戳基准**
```cpp
last_function_parsed_ts = ns_from_start();
```
**目的**：确保后续函数事件从当前时间开始，避免时间重叠

### **start_preprocess_file函数**

**获取当前时间**
```cpp
auto now = ns_from_start();
```
**ns_from_start()原理**：
```cpp
// 定义在comm.h中：
inline TimeStamp ns_from_start()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock_t::now() - COMPILATION_START
    ).count();
}

// 返回相对于编译开始的纳秒数
```

**过滤特殊文件名**
```cpp
if (!file_name || !strcmp(file_name, "<command-line>"))
{
    return;
}
```

**特殊文件处理**：
```cpp
// nullptr：GCC内部可能传递空指针
// "<command-line>"：命令行定义的宏，不是实际文件
// 示例：gcc -DDEBUG=1 会创建<command-line>虚拟文件
```

**循环包含检测**
```cpp
if (preprocess_start.contains(file_name) &&
    !preprocess_end.contains(file_name))
{
    file_name = CIRCULAR_POISON_VALUE;
    pfile = nullptr;
}
```

**检测逻辑**：
```cpp
// 条件解释：
// 1. 文件已经开始处理（在preprocess_start中）
// 2. 但还没有结束（不在preprocess_end中）
// 3. 说明文件在栈中但未完成 → 循环包含

// 处理：替换为毒丸值，清空pfile（不需要路径信息）
```

**记录开始时间**
```cpp
if (!preprocess_start.contains(file_name))
{
    preprocess_start[file_name] = now;
}
```

**为什么只记录第一次**：
```cpp
// 文件可能被多次包含（如头文件保护）
// 只记录第一次开始时间，代表"首次进入"
// 后续包含视为同一处理过程
```

**压栈**
```cpp
preprocessing_stack.push(file_name);
```
**栈状态更新**：表示开始处理新文件

**路径规范化处理**
```cpp
if (pfile)
{
    auto cpp_buffer = cpp_get_buffer(pfile);
    auto cpp_file = cpp_get_file(cpp_buffer);
    auto dir = cpp_get_dir(cpp_file);
    
    auto real_dir_name = realpath(dir->name, nullptr);
    auto real_file_name = realpath(file_name, nullptr);
    
    if (real_dir_name && real_file_name)
    {
        register_include_location(real_file_name, real_dir_name);
    }
    
    // 清理realpath分配的内存
    if (real_dir_name) free(real_dir_name);
    if (real_file_name) free(real_file_name);
}
```

**GCC预处理器数据结构链**：
```cpp
cpp_reader → cpp_buffer → cpp_file → cpp_dir
                ↓              ↓         ↓
             状态机         文件信息   目录信息
```

**realpath函数原理**：
```cpp
// 原型：char *realpath(const char *path, char *resolved_path);
// 功能：解析符号链接，获取规范化的绝对路径

// 示例：
输入：/usr/include/../include/stdio.h
输出：/usr/include/stdio.h

// 内存管理：返回malloc分配的内存，需要free
```

### **end_preprocess_file函数**

**获取当前时间**
```cpp
auto now = ns_from_start();
```

**记录结束时间**
```cpp
if (!preprocess_end.contains(preprocessing_stack.top()))
{
    preprocess_end[preprocessing_stack.top()] = now;
}
```

**为什么检查contains**：
```cpp
// 同一文件可能多次调用end（错误恢复时）
// 只记录第一次结束时间
// 保持与开始时间的对称性
```

**弹出栈顶**
```cpp
preprocessing_stack.pop();
```
**栈状态更新**：表示完成当前文件处理

**更新时间戳基准**
```cpp
last_function_parsed_ts = now + 3;
```
**+3纳秒原理**：为下一个函数事件留出时间间隔，避免重叠

### **write_preprocessing_events函数**

**安全结束预处理**
```cpp
finish_preprocessing_stage();
```
**防御性编程**：确保所有文件都正确结束

**遍历所有预处理文件**
```cpp
for (const auto& [file, start] : preprocess_start)
{
    if (file == CIRCULAR_POISON_VALUE) continue;
    
    int64_t end = preprocess_end.at(file);
    
    add_event(TraceEvent{
        normalized_file_name(file.data()),
        EventCategory::PREPROCESS,
        {start, end},
        std::nullopt
    });
}
```

**C++17结构化绑定**：
```cpp
// 传统写法：
for (const auto& pair : preprocess_start) {
    const auto& file = pair.first;
    const auto& start = pair.second;
}

// C++17结构化绑定：
for (const auto& [file, start] : preprocess_start)
// 更清晰，直接解构pair
```

**preprocess_end.at()安全性**：
```cpp
// at()方法：如果key不存在，抛出std::out_of_range异常
// 相比operator[]更安全（不会意外插入新元素）

// 理论上每个开始都应该有对应的结束
// 但异常情况可能导致缺失，at()提供明确错误
```

### **start_opt_pass函数**

**获取当前时间**
```cpp
auto now = ns_from_start();
```

**结束上一个pass**
```cpp
last_pass.ts.end = now;
if (last_pass.pass)
{
    pass_events.emplace_back(last_pass);
}
```

**设计模式**：
```cpp
// 新pass开始 = 上一个pass结束
// 通过回调时序隐式确定pass边界

// 为什么检查last_pass.pass？
// 第一次调用时last_pass.pass为nullptr
// 跳过emplace_back，避免空pass记录
```

**开始新pass**
```cpp
last_pass.pass = pass;
last_pass.ts.start = now + 1;
```

**+1纳秒避免重叠**：
```cpp
// 确保pass事件在时间线上不重叠
// Chrome Tracing可能合并重叠事件
// +1ns创建微小间隙，保持事件独立性
```

### **write_opt_pass_events函数**

**遍历所有pass事件**
```cpp
for (const auto& event : pass_events)
{
    map_t<std::string, std::string> args;
    args["static_pass_number"] = std::to_string(event.pass->static_pass_number);
    
    add_event(TraceEvent{
        event.pass->name,
        pass_type(event.pass->type),
        event.ts,
        std::move(args)
    });
}
```

**额外参数设计**：
```cpp
// static_pass_number：GCC内部分配的pass编号
// 用途：在JSON输出中提供更多调试信息
// 用户可以通过编号查找GCC源码中的具体pass

// 为什么不包含更多信息？
// 保持JSON简洁，避免信息过载
// 其他信息可通过pass名称推断
```

**std::move优化**：
```cpp
// args是局部变量，即将销毁
// std::move将其资源转移给add_event
// 避免不必要的拷贝
```

### **end_parse_function函数**

**静态状态变量**
```cpp
static bool did_last_function_have_scope = false;
```
**静态变量特性**：
```cpp
// 生命周期：整个程序运行期间
// 作用域：仅限于这个函数
// 用途：在多次调用间保持状态
```

**获取当前时间**
```cpp
TimeStamp now = ns_from_start();
```

**计算时间跨度**
```cpp
TimeSpan ts{last_function_parsed_ts + 3, now};
last_function_parsed_ts = now;
```

**时间戳调整策略**：
```cpp
// +3纳秒：避免与上一个函数事件重叠
// 连续函数解析可能时间非常接近
// 微小偏移确保在Chrome Tracing中显示为独立事件
```

**存储函数事件**
```cpp
function_events.emplace_back(info.name, info.file_name, ts);
```

**emplace_back优势**：
```cpp
// 传统push_back：
function_events.push_back(FunctionEvent{info.name, info.file_name, ts});

// emplace_back：
function_events.emplace_back(info.name, info.file_name, ts);

// 区别：emplace_back直接构造，避免临时对象
```

**作用域事件处理**

**检查是否有作用域**
```cpp
if (info.scope_name)
```

**扩展现有作用域**
```cpp
if (!scope_events.empty() && did_last_function_have_scope &&
    scope_events.back().name == info.scope_name)
{
    scope_events.back().ts.end = ts.end + 1;
}
```

**作用域合并逻辑**：
```cpp
// 条件分析：
// 1. 作用域列表非空
// 2. 上一个函数有作用域（did_last_function_have_scope）
// 3. 当前作用域与上一个相同（name相等）

// 示例：
class A {
    void foo() {}  // 创建作用域A：{start: t1, end: t2}
    void bar() {}  // 扩展作用域A：{start: t1, end: t3}
}
```

**创建新作用域**
```cpp
else
{
    scope_events.emplace_back(
        info.scope_name,
        info.scope_type,
        TimeSpan{ts.start - 1, ts.end + 1}
    );
}
```

**时间微调原理**：
```cpp
// ts.start - 1：作用域稍早于函数开始
// ts.end + 1：作用域稍晚于函数结束

// 效果：在Chrome Tracing中，作用域事件包含函数事件
// 视觉上：作用域条包含函数条
```

**更新状态**
```cpp
did_last_function_have_scope = true;
}
else
{
    did_last_function_have_scope = false;
}
```

**状态机设计**：
```cpp
// 记录上一个函数是否有作用域
// 用于判断是否合并连续的作用域事件
```

### **write_all_scopes函数**

**遍历作用域事件**
```cpp
for (const auto& [name, type, ts] : scope_events)
{
    add_event(TraceEvent{
        name.data(),
        type,
        ts,
        std::nullopt
    });
}
```

**输出时机**：
```cpp
// 在write_all_events()中调用
// 在所有事件收集完成后统一输出
// 保持JSON文件中事件分组清晰
```

### **write_all_functions函数**

**遍历函数事件**
```cpp
for (const auto& [name, file_name, ts] : function_events)
{
    map_t<std::string, std::string> args;
    args["file"] = normalized_file_name(file_name);
    
    add_event(TraceEvent{
        name.data(),
        EventCategory::FUNCTION,
        ts,
        std::move(args)
    });
}
```

**函数事件特点**：
```cpp
// 1. 包含文件信息作为额外参数
// 2. 使用规范化文件名（如果无冲突）
// 3. 事件类型固定为FUNCTION

// JSON输出示例：
{
    "name": "my_namespace::MyClass::foo(int)",
    "cat": "FUNCTION", 
    "args": {"file": "src/myclass.cpp"},
    "ts": 123456789,
    "ph": "B"
}
```

## **整体架构总结**

### **数据流设计**：
```
原始事件 → 数据收集 → 中间存储 → 批量输出
(GCC回调)   (tracking.cpp)   (向量/映射)   (perf_output.cpp)
```

# 通过 test.cpp 全流程解析

## 一、整体工作流程概览

### 1.1 从源码到性能可视化的完整链路

```
C++源文件 (test.cpp)
    ↓
GCC编译器 + gperf插件
    ↓
编译过程实时追踪
    ↓
Chrome Tracing JSON文件 (trace.json)
    ↓
可视化分析 (chrome://tracing)
```

### 1.2 插件内部模块协作流程

```
gperf插件架构
├── plugin.cpp (GCC接口层/事件捕获)
│   ├── 回调函数注册
│   ├── GCC Hook机制
│   ├── 预处理拦截
│   └── 事件触发器
│
├── tracking.cpp (数据处理引擎)
│   ├── 数据结构管理
│   │   ├── 预处理时间映射
│   │   ├── 优化Pass记录
│   │   ├── 函数事件存储
│   │   └── 作用域事件存储
│   ├── 时间戳计算
│   ├── 路径规范化
│   ├── 循环包含检测
│   └── 作用域合并算法
│
└── perf_output.cpp (JSON输出引擎)
    ├── 序列化与写入
    ├── 版本兼容处理
    ├── 事件过滤(1ms阈值)
    ├── 时间戳转换(纳秒→微秒)
    ├── UID配对机制
    └── Chrome Tracing格式生成
```

```
plugin.cpp → tracking.cpp → perf_output.cpp
(GCC回调) → (数据处理) → (JSON输出)
```

## 二、编译事件追踪的完整生命周期

### 2.1 初始化阶段（插件加载）

```cpp
// plugin.cpp
GccTrace::COMPILATION_START = GccTrace::clock_t::now();
```

**关键操作**：
1. **时间基准建立**：记录编译开始绝对时间点
2. **输出系统初始化**：创建JSON根对象，设置元数据
3. **回调函数注册**：向GCC注册6个关键事件监听器

**注册的回调函数及其触发时机**：

| 回调函数 | 触发时机 | 对应编译阶段 |
|---------|---------|------------|
| `cb_start_compilation` | 翻译单元开始 | 编译启动 |
| `cb_finish_decl` | 声明解析完成 | 预处理结束 |
| `cb_finish_parse_function` | 函数体解析完成 | 语法分析 |
| `cb_pass_execution` | 优化pass开始执行 | 优化阶段 |
| `cb_plugin_finish` | 编译完全结束 | 输出阶段 |
| `cb_file_change` | 文件包含切换 | 预处理 |

### 2.2 预处理阶段追踪

#### 2.2.1 文件包含栈管理

```cpp
// tracking.cpp
std::stack<std::string> preprocessing_stack;

// plugin.cpp
void cb_file_change(cpp_reader* pfile, const line_map_ordinary* new_map)
{
    switch (new_map->reason)
    {
        case LC_ENTER: start_preprocess_file(file_name, pfile); break;
        case LC_LEAVE: end_preprocess_file(); break;
    }
}
```

**实际执行示例**：
```cpp
// test.cpp 内容：
#include <iostream>
#include "myheader.h"

// 处理流程：
1. 进入main.cpp: stack = ["main.cpp"]
2. 进入iostream: stack = ["main.cpp", "iostream"]
3. 离开iostream:  stack = ["main.cpp"]
4. 进入myheader.h: stack = ["main.cpp", "myheader.h"]
5. 离开myheader.h: stack = ["main.cpp"]
6. 离开main.cpp:  stack = []
```

#### 2.2.2 路径规范化系统

```cpp
// tracking.cpp
auto real_dir_name = realpath(dir->name, nullptr);
auto real_file_name = realpath(file_name, nullptr);
register_include_location(real_file_name, real_dir_name);
```

**路径处理示例**：
```
原始路径: /usr/local/include/c++/11/iostream
包含目录: /usr/local/include/c++/11
规范化后: iostream

原始路径: /home/user/project/src/utils.h  
包含目录: /home/user/project
规范化后: src/utils.h
```

### 2.3 函数解析阶段追踪

#### 2.3.1 函数信息提取流程

```cpp
// plugin.cpp
void cb_finish_parse_function(void* gcc_data, void* user_data)
{
    tree decl = (tree)gcc_data;
    auto decl_name = decl_as_string(decl, 0);        // "my_namespace::MyClass::foo(int)"
    auto parent_decl = DECL_CONTEXT(decl);           // 父作用域
    auto expanded_location = expand_location(...);   // 源文件位置
}
```

**函数上下文解析**：
```cpp
// 对于函数：void my_namespace::MyClass::foo(int x)
提取信息:
- 函数签名: "my_namespace::MyClass::foo(int)"
- 作用域名称: "my_namespace::MyClass"
- 作用域类型: STRUCT (类作用域)
- 源文件: /path/to/file.cpp
```

#### 2.3.2 时间戳防重叠机制

```cpp
// tracking.cpp
TimeSpan ts{last_function_parsed_ts + 3, now};
last_function_parsed_ts = now;
```

**设计原理**：
- **问题**：连续函数解析可能在同一纳秒完成
- **解决方案**：每个函数开始时间+3纳秒偏移
- **效果**：确保Chrome Tracing中事件不重叠显示

### 2.4 优化阶段追踪

#### 2.4.1 Pass执行追踪机制

```cpp
// plugin.cpp
void cb_pass_execution(void* gcc_data, void* user_data)
{
    auto pass = (opt_pass*)gcc_data;
    start_opt_pass(pass);  // 结束上一个pass，开始新pass
}
```

**GCC优化Pass类型**：
- **GIMPLE_PASS**：高级中间表示优化（如死代码消除）
- **RTL_PASS**：寄存器传输级优化（如寄存器分配）
- **SIMPLE_IPA_PASS**：简单过程间分析
- **IPA_PASS**：完整过程间分析

#### 2.4.2 Pass事件记录策略

```cpp
// tracking.cpp
void start_opt_pass(const opt_pass* pass)
{
    // 1. 结束上一个pass
    last_pass.ts.end = now;
    if (last_pass.pass) pass_events.emplace_back(last_pass);
    
    // 2. 开始新pass
    last_pass.pass = pass;
    last_pass.ts.start = now + 1;  // +1ns避免重叠
}
```

### 2.5 数据收集与存储

#### 2.5.1 四层数据结构体系

```cpp
// tracking.cpp
// 1. 预处理时间映射
map_t<std::string, int64_t> preprocess_start;  // 文件 -> 开始时间
map_t<std::string, int64_t> preprocess_end;    // 文件 -> 结束时间

// 2. 优化Pass记录
std::vector<OptPassEvent> pass_events;  // 所有Pass历史

// 3. 函数事件存储
std::vector<FunctionEvent> function_events;

// 4. 作用域事件存储  
std::vector<ScopeEvent> scope_events;
```

#### 2.5.2 作用域合并算法

```cpp
// tracking.cpp
if (!scope_events.empty() && did_last_function_have_scope &&
    scope_events.back().name == info.scope_name)
{
    // 扩展现有作用域
    scope_events.back().ts.end = ts.end + 1;
}
else
{
    // 创建新作用域
    scope_events.emplace_back(info.scope_name, info.scope_type,
                             TimeSpan{ts.start - 1, ts.end + 1});
}
```

**合并示例**：
```cpp
// 类中的连续函数：
class MyClass {
    void foo() {}  // 创建作用域MyClass: [t1-1, t2+1]
    void bar() {}  // 扩展作用域MyClass: [t1-1, t3+1]
    // 作用域合并，覆盖两个函数的时间范围
}
```

### 2.6 JSON输出阶段

#### 2.6.1 事件过滤策略

```cpp
// perf_output.cpp
if ((event.ts.end - event.ts.start) < MINIMUM_EVENT_LENGTH_NS)
{
    return;  // 跳过短于1ms的事件
}
```

**过滤逻辑**：
- **阈值**：1毫秒（1,000,000纳秒）
- **目的**：减少噪音，避免海量微事件淹没重要信息
- **效果**：通常过滤掉80%以上的微小事件

#### 2.6.2 Chrome Tracing格式生成

```cpp
// perf_output.cpp
output_events_list->append(
    new_event(event, pid, tid, event.ts.start, "B", this_uid));  // 开始事件
output_events_list->append(
    new_event(event, pid, tid, event.ts.end, "E", this_uid));    // 结束事件
```

**成对事件机制**：
- **B（Begin）**：事件开始，包含唯一UID
- **E（End）**：事件结束，包含相同UID
- **Chrome Tracing**：通过UID配对显示时间条

#### 2.6.3 时间戳转换

```cpp
// perf_output.cpp
json_event->set("ts", new json::float_number(
    static_cast<double>(ts) * 0.001L));  // 纳秒 → 微秒
```

**单位转换原理**：
- **内部存储**：纳秒（高精度）
- **Chrome Tracing标准**：微秒
- **转换公式**：`微秒 = 纳秒 × 0.001`
- **精度保持**：使用long double避免精度损失

#### 2.6.4 版本兼容性处理

```cpp
// perf_output.cpp
#if GCCPLUGIN_VERSION_MAJOR >= 14
    output_json->dump(trace_file, /*formatted=*/false);
#else
    output_json->dump(trace_file);
#endif
```

**GCC JSON API差异**：
- **GCC 14+**：支持格式化参数，可输出紧凑JSON
- **GCC 13-**：简化API，默认可能格式化
- **文件大小**：紧凑输出可减少30-50%文件体积

### 2.7 测试用例的全流程追踪

#### 2.7.1 test.cpp编译的完整事件流

```cpp
// test.cpp 综合测试，触发所有追踪点
#define TEST_BASIC_INCLUDES      1  // 触发预处理追踪
#define TEST_MACRO_EXPANSION     1  // 测试宏展开性能
#define TEST_NAMESPACES          1  // 触发命名空间追踪
#define TEST_CLASS_HIERARCHY     1  // 触发类和作用域追踪
#define TEST_TEMPLATES           1  // 测试模板实例化性能
#define TEST_CONSTEXPR           1  // 编译期计算追踪
#define TEST_LAMBDAS            1   // Lambda表达式追踪
```

#### 2.7.2 各测试模块的追踪效果

**1. 预处理追踪** (`TEST_BASIC_INCLUDES`)
```
事件类型: PREPROCESS
追踪文件: iostream, vector, string, cmath, algorithm, memory, cstring
时间数据: 每个头文件的解析耗时
```

**2. 宏展开追踪** (`TEST_MACRO_EXPANSION`)
```
深度宏: REPEAT_16(42) → 展开为16个42
变参宏: LOG_ARGS("test", 1, 2, 3)
追踪效果: 观察宏展开的编译时间消耗
```

**3. 命名空间追踪** (`TEST_NAMESPACES`)
```
作用域事件:
- Math命名空间: {name: "Math", type: NAMESPACE}
- Physics命名空间: {name: "Physics", type: NAMESPACE}
- Outer::Inner嵌套命名空间
- 匿名命名空间处理
```

**4. 类层次结构** (`TEST_CLASS_HIERARCHY`)
```
作用域合并示例:
class Shape (基类)
  ├── class Rectangle (派生类) → 作用域事件
  └── class Circle (派生类) → 作用域事件
  
模板类: FixedArray<double, 10> → 实例化追踪
```

**5. 模板系统** (`TEST_TEMPLATES`)
```
模板实例化事件:
- findMax<int>(3, 7)
- findMax<std::string>("hello", "world")
- 可变参数模板: printAll<const char*, const char*, ...>
- 模板元编程: Factorial<5>::value
- C++20概念: Arithmetic<T>约束
```

**6. 编译期计算** (`TEST_CONSTEXPR`)
```
constexpr函数追踪:
- fibonacci(10) 编译期递归计算
- stringLength("Hello") 编译期字符串处理
- arraySum({1,2,3,4,5}) 编译期数组求和
```

**7. Lambda表达式** (`TEST_LAMBDAS`)
```
Lambda类型追踪:
- createMultiplier → 返回闭包
- genericAdder → 泛型Lambda
- processNumbers中的捕获和算法Lambda
```

## 三、核心算法与数据结构详解

### 3.1 循环包含检测算法

```cpp
// tracking.cpp
if (preprocess_start.contains(file_name) &&
    !preprocess_end.contains(file_name))
{
    // 文件已开始但未结束 → 循环包含
    file_name = CIRCULAR_POISON_VALUE;
    pfile = nullptr;
}
```

**算法原理**：
- **状态检查**：文件在`start`中但不在`end`中
- **毒丸标记**：替换为特殊值，避免无限递归
- **边界处理**：跳过内层包含的追踪

### 3.2 文件名冲突检测

```cpp
// tracking.cpp
if (normalized_files.contains(normalized_file))
{
    conflicted_files.insert(normalized_file);  // 标记冲突
}
else
{
    normalized_files.insert(normalized_file);   // 注册成功
}
```

**冲突场景**：
```
/project1/src/main.cpp → 规范化: src/main.cpp
/project2/src/main.cpp → 规范化: src/main.cpp  ← 冲突!
处理: 冲突文件使用原始路径显示
```

### 3.3 时间戳防碰撞算法

```cpp
// 三个层次的防碰撞策略：
1. 函数事件: last_function_parsed_ts + 3
2. 作用域事件: ts.start - 1, ts.end + 1  
3. 优化Pass: now + 1

// 设计目的：确保Chrome Tracing中事件清晰分层显示
```

## 四、性能数据可视化分析

### 4.1 生成的JSON数据结构

```json
{
  "displayTimeUnit": "ns",
  "beginningOfTime": 1672531200000000,
  "traceEvents": [
    {
      "name": "TU",
      "ph": "B",
      "cat": "TU",
      "ts": 0,
      "pid": 12345,
      "tid": 0,
      "args": {"UID": 0}
    },
    {
      "name": "iostream",
      "ph": "B", 
      "cat": "PREPROCESS",
      "ts": 1000000,
      "pid": 12345,
      "tid": 0,
      "args": {"UID": 1}
    },
    {
      "name": "my_namespace::MyClass::foo(int)",
      "ph": "B",
      "cat": "FUNCTION", 
      "ts": 50000000,
      "pid": 12345,
      "tid": 0,
      "args": {"UID": 100, "file": "src/myclass.cpp"}
    }
  ]
}
```

### 4.2 Chrome Tracing分析要点

1. **时间轴视图**：水平时间轴显示所有事件
2. **颜色编码**：不同事件类别使用不同颜色
3. **层级显示**：作用域包含函数，清晰展示代码结构
4. **详细信息**：悬停显示事件名称、耗时、文件位置
5. **性能热点**：识别耗时最长的编译阶段

### 4.3 典型性能分析场景

**场景1：预处理时间过长**
```
问题表现: PREPROCESS事件占据大部分编译时间
可能原因: 头文件包含过多、循环包含、宏展开复杂
解决方案: 使用前置声明、PIMPL模式、减少头文件依赖
```

**场景2：模板实例化爆炸**
```
问题表现: FUNCTION事件中模板函数耗时显著
可能原因: 过度使用模板、递归实例化
解决方案: 显式实例化、模板特化、减少模板参数
```

**场景3：优化Pass瓶颈**
```
问题表现: GIMPLE_PASS或RTL_PASS耗时异常
可能原因: 复杂控制流、大量内联
解决方案: 调整优化级别、使用__attribute__((noinline))
```

## 五、高级功能与扩展

### 5.1 自定义事件追踪扩展

```cpp
// 扩展EventCategory枚举
enum EventCategory {
    // 现有类型...
    TEMPLATE_INST,      // 模板实例化
    INLINE_EXPANSION,   // 内联展开
    DEBUG_INFO,         // 调试信息生成
};

// 注册新的GCC回调
register_callback(PLUGIN_NAME, PLUGIN_EVENT_NEW, 
                  &custom_callback, nullptr);
```

### 5.2 多线程编译支持

```cpp
// 当前限制：单线程假设
// 扩展方案：
// 1. 线程本地存储(TLS)保存事件数据
// 2. 互斥锁保护共享数据结构
// 3. 线程ID作为Chrome Tracing的tid字段
```

### 5.3 远程分析与聚合

```cpp
// 扩展输出格式支持
void write_events_to_database(const TraceEvent& event);
void aggregate_multiple_compilations();
void generate_performance_report();
```

## 六、调试与故障排除

### 6.1 常见问题解决

**问题1：插件加载失败**
```bash
# 检查GCC版本兼容性
gcc --version
# 确保插件编译使用的GCC版本与运行版本一致
```

**问题2：输出文件为空**
```bash
# 检查事件过滤阈值
# 临时降低MINIMUM_EVENT_LENGTH_NS测试
# 检查回调函数是否正确注册
```

**问题3：时间戳异常**
```cpp
// 检查COMPILATION_START初始化时机
// 确保所有时间戳使用ns_from_start()统一计算
```

### 6.2 调试信息输出

```cpp
// 启用详细日志
#define DEBUG_TRACING 1

#ifdef DEBUG_TRACING
    fprintf(stderr, "GPERF DEBUG: %s:%d - %s\n", 
            __FILE__, __LINE__, message);
#endif
```
