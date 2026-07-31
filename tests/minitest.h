#pragma once
// minitest — 轻量级 C++ 单元测试框架
// 提供与 Google Test 兼容的 API 子集，零外部依赖

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================
// 断言宏
// ============================================================
#define MINITEST_FAIL() \
    do { \
        ::minitest::Fail(__FILE__, __LINE__, "FAIL() called"); \
    } while (false)

#define EXPECT_EQ(a, b) \
    ::minitest::ExpectEq(__FILE__, __LINE__, #a, #b, a, b)
#define EXPECT_NE(a, b) \
    ::minitest::ExpectNe(__FILE__, __LINE__, #a, #b, a, b)
#define EXPECT_GT(a, b) \
    ::minitest::ExpectGt(__FILE__, __LINE__, #a, #b, a, b)
#define EXPECT_GE(a, b) \
    ::minitest::ExpectGe(__FILE__, __LINE__, #a, #b, a, b)
#define EXPECT_LE(a, b) \
    ::minitest::ExpectLe(__FILE__, __LINE__, #a, #b, a, b)
#define EXPECT_LT(a, b) \
    ::minitest::ExpectLt(__FILE__, __LINE__, #a, #b, a, b)
#define EXPECT_TRUE(x) \
    ::minitest::ExpectTrue(__FILE__, __LINE__, #x, x)
#define EXPECT_FALSE(x) \
    ::minitest::ExpectTrue(__FILE__, __LINE__, #x, !(x))

// 需要使用 operator<< 的类型，用 EXPECT_EQ 替代
#define EXPECT_STREQ(a, b) \
    ::minitest::ExpectEq(__FILE__, __LINE__, #a, #b, std::string(a), std::string(b))

#define EXPECT_NO_THROW(stmt) \
    do { \
        try { stmt; } catch (const std::exception& e) { \
            ::minitest::Fail(__FILE__, __LINE__, \
                std::string("Expected no throw, got: ") + e.what()); \
        } catch (...) { \
            ::minitest::Fail(__FILE__, __LINE__, "Expected no throw"); \
        } \
    } while (false)

#define EXPECT_THROW(stmt, exception_type) \
    do { \
        bool caught = false; \
        try { stmt; } \
        catch (const exception_type&) { caught = true; } \
        catch (...) {} \
        if (!caught) { \
            ::minitest::Fail(__FILE__, __LINE__, \
                "Expected throw of " #exception_type); \
        } \
    } while (false)

// ASSERT 类宏（与 EXPECT 相同，但失败后由测试函数自然返回）
#define ASSERT_TRUE(x)  EXPECT_TRUE(x)
#define ASSERT_FALSE(x) EXPECT_FALSE(x)
#define ASSERT_EQ(a, b)  EXPECT_EQ(a, b)
#define ASSERT_NE(a, b)  EXPECT_NE(a, b)
#define ASSERT_GT(a, b)  EXPECT_GT(a, b)

#define FAIL() MINITEST_FAIL()

// ============================================================
// 测试注册宏
// ============================================================
#define TEST(suite, name) \
    void minitest_##suite##_##name(); \
    static ::minitest::TestRegistrar \
        minitest_reg_##suite##_##name(#suite "." #name, \
                                     minitest_##suite##_##name); \
    void minitest_##suite##_##name()

#define TEST_F(suite, name) \
    void minitest_##suite##_##name(); \
    static ::minitest::TestRegistrar \
        minitest_reg_##suite##_##name(#suite "." #name, \
                                     minitest_##suite##_##name); \
    void minitest_##suite##_##name()

// ============================================================
// 框架实现
// ============================================================
namespace minitest {

using TestFunction = std::function<void()>;

struct TestCase {
    std::string name;
    TestFunction func;
};

// 全局测试注册表
inline std::vector<TestCase>& GetTests() {
    static std::vector<TestCase> tests;
    return tests;
}

struct TestRegistrar {
    TestRegistrar(const std::string& name, TestFunction func) {
        GetTests().push_back({name, std::move(func)});
    }
};

// 运行所有测试，返回失败数
inline int RunAllTests() {
    int passed = 0;
    int failed = 0;
    int total = static_cast<int>(GetTests().size());

    std::cout << "[==========] Running " << total << " tests.\n";

    for (auto& test : GetTests()) {
        std::cout << "[ RUN      ] " << test.name << "\n";
        try {
            test.func();
            std::cout << "[       OK ] " << test.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "[  FAILED  ] " << test.name << "\n";
            std::cerr << "  " << e.what() << "\n";
            ++failed;
        }
    }

    std::cout << "[==========] " << total << " tests ran.\n";
    std::cout << "[  PASSED  ] " << passed << " tests.\n";
    if (failed > 0) {
        std::cout << "[  FAILED  ] " << failed << " tests.\n";
    }
    return failed;
}

// ============================================================
// 断言辅助
// ============================================================
inline void Fail(const char* file, int line, const std::string& msg) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << msg;
    throw std::runtime_error(oss.str());
}

template <typename T, typename U = T>
void ExpectEq(const char* file, int line,
              const char* a_expr, const char* b_expr,
              const T& a, const U& b) {
    if (!(a == static_cast<T>(b))) {
        std::ostringstream oss;
        oss << file << ":" << line << ": EXPECT_EQ(" << a_expr
            << ", " << b_expr << ") failed";
        throw std::runtime_error(oss.str());
    }
}

template <typename T, typename U = T>
void ExpectNe(const char* file, int line,
              const char* a_expr, const char* b_expr,
              const T& a, const U& b) {
    if (!(a != static_cast<T>(b))) {
        std::ostringstream oss;
        oss << file << ":" << line << ": EXPECT_NE(" << a_expr
            << ", " << b_expr << ") failed";
        throw std::runtime_error(oss.str());
    }
}

template <typename T, typename U = T>
void ExpectGt(const char* file, int line,
              const char* a_expr, const char* b_expr,
              const T& a, const U& b) {
    if (!(a > static_cast<T>(b))) {
        std::ostringstream oss;
        oss << file << ":" << line << ": EXPECT_GT(" << a_expr
            << ", " << b_expr << ") failed";
        throw std::runtime_error(oss.str());
    }
}

template <typename T, typename U = T>
void ExpectGe(const char* file, int line,
              const char* a_expr, const char* b_expr,
              const T& a, const U& b) {
    if (!(a >= static_cast<T>(b))) {
        std::ostringstream oss;
        oss << file << ":" << line << ": EXPECT_GE(" << a_expr
            << ", " << b_expr << ") failed";
        throw std::runtime_error(oss.str());
    }
}

template <typename T, typename U = T>
void ExpectLe(const char* file, int line,
              const char* a_expr, const char* b_expr,
              const T& a, const U& b) {
    if (!(a <= static_cast<T>(b))) {
        std::ostringstream oss;
        oss << file << ":" << line << ": EXPECT_LE(" << a_expr
            << ", " << b_expr << ") failed";
        throw std::runtime_error(oss.str());
    }
}

template <typename T, typename U = T>
void ExpectLt(const char* file, int line,
              const char* a_expr, const char* b_expr,
              const T& a, const U& b) {
    if (!(a < static_cast<T>(b))) {
        std::ostringstream oss;
        oss << file << ":" << line << ": EXPECT_LT(" << a_expr
            << ", " << b_expr << ") failed";
        throw std::runtime_error(oss.str());
    }
}

inline void ExpectTrue(const char* file, int line,
                        const char* expr, bool value) {
    if (!value) {
        std::ostringstream oss;
        oss << file << ":" << line << ": EXPECT_TRUE(" << expr << ") failed";
        throw std::runtime_error(oss.str());
    }
}

}  // namespace minitest
