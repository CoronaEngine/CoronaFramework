// 简单的测试框架
#pragma once
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace CoronaTest {

struct TestResult {
    std::string name;
    bool passed;
    std::string message;
    double duration_ms;
};

class TestRunner {
   public:
    static TestRunner& instance() {
        static TestRunner runner;
        return runner;
    }

    void add_test(const std::string& name, std::function<void()> test_func) {
        tests_.push_back({name, test_func});
    }

    int run_all() {
        int passed = 0;
        int failed = 0;

        std::cout << "========================================\n";
        std::cout << "Running " << tests_.size() << " tests\n";
        std::cout << "========================================\n\n";

        for (const auto& [name, test_func] : tests_) {
            std::cout << "[ RUN      ] " << name << "\n";

            auto start = std::chrono::high_resolution_clock::now();
            bool test_passed = false;
            std::string error_message;

            try {
                test_func();
                test_passed = true;
            } catch (const std::exception& e) {
                error_message = e.what();
            } catch (...) {
                error_message = "Unknown exception";
            }

            auto end = std::chrono::high_resolution_clock::now();
            double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

            if (test_passed) {
                std::cout << "[       OK ] " << name << " (" << duration_ms << " ms)\n";
                passed++;
            } else {
                std::cout << "[  FAILED  ] " << name << " (" << duration_ms << " ms)\n";
                if (!error_message.empty()) {
                    std::cout << "  Error: " << error_message << "\n";
                }
                failed++;
            }
            std::cout << "\n";
        }

        std::cout << "========================================\n";
        std::cout << "Test Results: " << passed << " passed, " << failed << " failed\n";
        std::cout << "========================================\n";

        return failed > 0 ? 1 : 0;
    }

   private:
    std::vector<std::pair<std::string, std::function<void()>>> tests_;
};

class TestRegistrar {
   public:
    TestRegistrar(const std::string& name, std::function<void()> test_func) {
        TestRunner::instance().add_test(name, test_func);
    }
};

// 测试宏
#define TEST(test_suite, test_name)                                        \
    void test_suite##_##test_name();                                       \
    static CoronaTest::TestRegistrar test_suite##_##test_name##_registrar( \
        #test_suite "." #test_name, test_suite##_##test_name);             \
    void test_suite##_##test_name()

#define ASSERT_TRUE(condition)                                         \
    do {                                                               \
        if (!(condition)) {                                            \
            throw std::runtime_error("Assertion failed: " #condition); \
        }                                                              \
    } while (0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ(a, b)                                                  \
    do {                                                                 \
        if ((a) != (b)) {                                                \
            throw std::runtime_error("Assertion failed: " #a " == " #b); \
        }                                                                \
    } while (0)

#define ASSERT_NE(a, b)                                                  \
    do {                                                                 \
        if ((a) == (b)) {                                                \
            throw std::runtime_error("Assertion failed: " #a " != " #b); \
        }                                                                \
    } while (0)

#define ASSERT_GT(a, b)                                                 \
    do {                                                                \
        if ((a) <= (b)) {                                               \
            throw std::runtime_error("Assertion failed: " #a " > " #b); \
        }                                                               \
    } while (0)

#define ASSERT_GE(a, b)                                                  \
    do {                                                                 \
        if ((a) < (b)) {                                                 \
            throw std::runtime_error("Assertion failed: " #a " >= " #b); \
        }                                                                \
    } while (0)

#define ASSERT_LT(a, b)                                                 \
    do {                                                                \
        if ((a) >= (b)) {                                               \
            throw std::runtime_error("Assertion failed: " #a " < " #b); \
        }                                                               \
    } while (0)

#define ASSERT_LE(a, b)                                                  \
    do {                                                                 \
        if ((a) > (b)) {                                                 \
            throw std::runtime_error("Assertion failed: " #a " <= " #b); \
        }                                                                \
    } while (0)

#define ASSERT_NO_THROW(expression)                                                        \
    do {                                                                                   \
        try {                                                                              \
            expression;                                                                    \
        } catch (...) {                                                                    \
            throw std::runtime_error("Exception thrown when none expected: " #expression); \
        }                                                                                  \
    } while (0)

}  // namespace CoronaTest
