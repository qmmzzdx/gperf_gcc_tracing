// GCC探针插件综合测试套件
// 用于验证gperf插件对各种C++编译特性的追踪能力

// ==================== 测试配置 ====================
// 通过定义宏控制要测试的特性
#define TEST_BASIC_INCLUDES      1  // 基础包含测试
#define TEST_MACRO_EXPANSION     1  // 宏展开测试  
#define TEST_NAMESPACES          1  // 命名空间测试
#define TEST_CLASS_HIERARCHY     1  // 类层次结构测试
#define TEST_TEMPLATES           1  // 模板测试
#define TEST_CONSTEXPR           1  // 编译期计算测试
#define TEST_LAMBDAS             1  // Lambda表达式测试
#define TEST_INLINE_ASM          0  // 内联汇编测试（可选）

// ==================== 第一部分：基础包含测试 ====================
#if TEST_BASIC_INCLUDES
#include <iostream>      // 标准IO流
#include <vector>        // 动态数组
#include <string>        // 字符串
#include <cmath>         // 数学函数
#include <algorithm>     // 算法
#include <memory>        // 智能指针
#include <cstring>       // C风格字符串操作（修复：添加这个头文件）
#endif

// ==================== 第二部分：宏系统测试 ====================
#if TEST_MACRO_EXPANSION

// 简单宏定义
#define PI 3.14159265359
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SQUARE(x) ((x) * (x))

// 条件编译宏
#ifdef DEBUG
#define LOG(msg) std::cout << "DEBUG: " << msg << std::endl
#else
#define LOG(msg) 
#endif

// 多层嵌套宏（测试深度展开）
#define REPEAT_1(x) x
#define REPEAT_2(x) REPEAT_1(x), REPEAT_1(x)
#define REPEAT_4(x) REPEAT_2(x), REPEAT_2(x)
#define REPEAT_8(x) REPEAT_4(x), REPEAT_4(x)
#define REPEAT_16(x) REPEAT_8(x), REPEAT_8(x)

// 变参宏测试 - 使用更安全的方式
#define LOG_ARGS(...) do { \
    std::cout << __VA_ARGS__; \
    std::cout << std::endl; \
} while(0)

#endif // TEST_MACRO_EXPANSION

// ==================== 第三部分：命名空间测试 ====================
#if TEST_NAMESPACES

namespace Math {
    constexpr double E = 2.71828182846;

    inline int add(int a, int b)
    {
        return a + b;
    }

    template<typename T>
    T abs(T value)
    {
        return value < 0 ? -value : value;
    }
}

namespace Physics {
    constexpr double G = 6.67430e-11;

    class Vector3D {
    public:
        double x, y, z;
        Vector3D(double x, double y, double z) : x(x), y(y), z(z) {}

        double magnitude() const
        {
            return std::sqrt(x * x + y * y + z * z);
        }
    };
}

// 嵌套命名空间测试
namespace Outer {
    namespace Inner {
        constexpr int VALUE = 42;

        struct Config {
            static constexpr int DEFAULT_SIZE = 100;
        };
    }

    using Inner::VALUE;
}

// 匿名命名空间测试
namespace {
    const char* UNKNOWN = "unknown";
    int internal_counter = 0;
}

#endif // TEST_NAMESPACES

// ==================== 第四部分：类层次结构测试 ====================
#if TEST_CLASS_HIERARCHY

// 基类
class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
    virtual void draw() const
    {
        std::cout << "Drawing shape" << std::endl;
    }

protected:
    int id;
    static int next_id;
};

int Shape::next_id = 0;

// 派生类 - 矩形
class Rectangle : public Shape {
public:
    Rectangle(double w, double h) : width(w), height(h)
    {
        id = next_id++;
    }

    double area() const override
    {
        return width * height;
    }

    void draw() const override
    {
        std::cout << "Drawing rectangle " << id
            << " (" << width << "x" << height << ")" << std::endl;
    }

    // 成员函数重载测试
    void scale(double factor)
    {
        width *= factor;
        height *= factor;
    }

    void scale(double w_factor, double h_factor)
    {
        width *= w_factor;
        height *= h_factor;
    }

private:
    double width;
    double height;
};

// 派生类 - 圆形
class Circle : public Shape {
public:
    Circle(double r) : radius(r)
    {
        id = next_id++;
    }

    double area() const override
    {
        return PI * radius * radius;
    }

    // 未覆盖draw()，使用基类版本

private:
    double radius;
};

// 模板类测试
template<typename T, size_t N>
class FixedArray {
public:
    FixedArray()
    {
        for (size_t i = 0; i < N; ++i)
        {
            data[i] = T{};
        }
    }

    T& operator[](size_t index)
    {
        return data[index];
    }

    const T& operator[](size_t index) const
    {
        return data[index];
    }

    size_t size() const { return N; }

private:
    T data[N];
};

#endif // TEST_CLASS_HIERARCHY

// ==================== 第五部分：模板高级特性测试 ====================
#if TEST_TEMPLATES

// 基础函数模板
template<typename T>
T findMax(const T& a, const T& b)
{
    return a > b ? a : b;
}

// 使用std::string重载
inline std::string findMax(const std::string& a, const std::string& b)
{
    return a > b ? a : b;
}

// 可变参数模板
template<typename... Args>
void printAll(Args... args)
{
    // 使用辅助函数处理折叠表达式
    (std::cout << ... << args) << std::endl;  // C++17折叠表达式
}

// 模板元编程 - 编译期阶乘计算
template<int N>
struct Factorial {
    static constexpr int value = N * Factorial<N - 1>::value;
};

template<>
struct Factorial<0> {
    static constexpr int value = 1;
};

// 概念约束（C++20）
#if __cplusplus >= 202002L
#include <type_traits>  // 添加类型特性头文件
template<typename T>
concept Arithmetic = std::is_arithmetic_v<T>;

template<Arithmetic T>
T square(T x)
{
    return x * x;
}
#endif

#endif // TEST_TEMPLATES

// ==================== 第六部分：编译期计算测试 ====================
#if TEST_CONSTEXPR

// constexpr函数
constexpr int fibonacci(int n)
{
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// 编译期字符串处理
constexpr size_t stringLength(const char* str)
{
    size_t len = 0;
    while (str[len] != '\0') ++len;
    return len;
}

// 编译期数组操作
template<typename T, size_t N>
constexpr T arraySum(const T(&arr)[N])
{
    T sum = T{};
    for (size_t i = 0; i < N; ++i)
    {
        sum += arr[i];
    }
    return sum;
}

#endif // TEST_CONSTEXPR

// ==================== 第七部分：Lambda表达式测试 ====================
#if TEST_LAMBDAS

// Lambda类型测试
auto createMultiplier = [](double factor) {
    return [factor](double value) {
        return value * factor;
    };
};

// 泛型Lambda
auto genericAdder = [](auto a, auto b) {
    return a + b;
};

// 在函数中使用Lambda
void processNumbers(const std::vector<int>& numbers)
{
    // 捕获局部变量
    int threshold = 10;

    // Lambda作为算法参数
    int count = std::count_if(numbers.begin(), numbers.end(),
        [threshold](int n) {
        return n > threshold;
    });

    // 转换操作
    std::vector<int> doubled;
    std::transform(numbers.begin(), numbers.end(),
        std::back_inserter(doubled),
        [](int n) { return n * 2; });

    // 立即调用Lambda
    [&doubled]() {
        std::cout << "Doubled vector size: " << doubled.size() << std::endl;
    }();
}

#endif // TEST_LAMBDAS

// ==================== 第八部分：内联汇编测试（可选） ====================
#if TEST_INLINE_ASM && defined(__x86_64__)

inline uint64_t readTimestampCounter()
{
    uint32_t lo, hi;
    __asm__ __volatile__(
        "rdtsc"
        : "=a" (lo), "=d" (hi)
    );
    return ((uint64_t)hi << 32) | lo;
}

#endif // TEST_INLINE_ASM

// ==================== 主函数：整合所有测试 ====================
int main()
{
    std::cout << "GCC Perf Tracing Plugin Test Suite" << std::endl;
    std::cout << "==================================" << std::endl;

    // 测试1：宏展开
#if TEST_MACRO_EXPANSION
    std::cout << "\n1. Macro Expansion Test:" << std::endl;
    std::cout << "PI = " << PI << std::endl;
    std::cout << "MAX(5, 10) = " << MAX(5, 10) << std::endl;
    std::cout << "SQUARE(7) = " << SQUARE(7) << std::endl;

    // 深度宏展开测试
    int values[] = {REPEAT_16(42)};  // 展开为16个42
    std::cout << "Array size after REPEAT_16: "
        << sizeof(values) / sizeof(values[0]) << std::endl;

    // 使用字符串流或分开输出
    std::cout << "Variadic macro test: ";
    std::cout << 1 << " " << 2 << " " << 3 << " end" << std::endl;

    // 或者使用修改后的LOG_ARGS宏
    LOG_ARGS("Variadic macro test with LOG_ARGS: " << 1 << " " << 2 << " " << 3 << " end");
#endif

    // 测试2：命名空间
#if TEST_NAMESPACES
    std::cout << "\n2. Namespace Test:" << std::endl;
    std::cout << "Math::E = " << Math::E << std::endl;
    std::cout << "Math::add(3, 4) = " << Math::add(3, 4) << std::endl;
    std::cout << "Physics::G = " << Physics::G << std::endl;
    std::cout << "Outer::Inner::VALUE = " << Outer::Inner::VALUE << std::endl;
    std::cout << "Anonymous namespace variable: " << UNKNOWN << std::endl;
#endif

    // 测试3：类层次结构
#if TEST_CLASS_HIERARCHY
    std::cout << "\n3. Class Hierarchy Test:" << std::endl;

    Rectangle rect(5.0, 3.0);
    Circle circle(2.5);

    std::cout << "Rectangle area: " << rect.area() << std::endl;
    std::cout << "Circle area: " << circle.area() << std::endl;

    rect.draw();
    circle.draw();

    // 模板类实例化
    FixedArray<double, 10> doubleArray;
    FixedArray<std::string, 5> stringArray;

    std::cout << "Double array size: " << doubleArray.size() << std::endl;
    std::cout << "String array size: " << stringArray.size() << std::endl;
#endif

    // 测试4：模板
#if TEST_TEMPLATES
    std::cout << "\n4. Template Test:" << std::endl;
    std::cout << "findMax(3, 7) = " << findMax(3, 7) << std::endl;

    // 使用std::string而不是字符串字面量
    std::string hello = "hello";
    std::string world = "world";
    std::cout << "findMax(\"hello\", \"world\") = "
        << findMax(hello, world) << std::endl;

    printAll("Template", " ", "variadic", " ", "test");

    std::cout << "Factorial<5> = " << Factorial<5>::value << std::endl;

#if __cplusplus >= 202002L
    std::cout << "square(4.5) = " << square(4.5) << std::endl;
#endif
#endif

    // 测试5：编译期计算
#if TEST_CONSTEXPR
    std::cout << "\n5. Compile-time Computation Test:" << std::endl;
    constexpr int fib10 = fibonacci(10);
    std::cout << "fibonacci(10) = " << fib10 << std::endl;

    constexpr size_t len = stringLength("Hello");
    std::cout << "Length of \"Hello\" = " << len << std::endl;

    constexpr int arr[] = {1, 2, 3, 4, 5};
    constexpr int sum = arraySum(arr);
    std::cout << "Sum of {1,2,3,4,5} = " << sum << std::endl;
#endif

    // 测试6：Lambda表达式
#if TEST_LAMBDAS
    std::cout << "\n6. Lambda Expression Test:" << std::endl;

    auto doubler = createMultiplier(2.0);
    std::cout << "doubler(3.14) = " << doubler(3.14) << std::endl;

    std::cout << "genericAdder(3, 4.5) = " << genericAdder(3, 4.5) << std::endl;
    std::cout << "genericAdder(std::string(\"Hello\"), std::string(\" World\")) = "
        << genericAdder(std::string("Hello"), std::string(" World")) << std::endl;

    std::vector<int> numbers = {5, 12, 8, 20, 3};
    processNumbers(numbers);
#endif

    // 测试7：内联汇编
#if TEST_INLINE_ASM && defined(__x86_64__)
    std::cout << "\n7. Inline Assembly Test:" << std::endl;
    uint64_t tsc = readTimestampCounter();
    std::cout << "Timestamp counter: " << tsc << std::endl;
#endif

    std::cout << "\nAll tests completed!" << std::endl;
    return 0;
}
