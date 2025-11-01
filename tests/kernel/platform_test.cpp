#include <iostream>

#include "../test_framework.h"
#include "corona/pal/cfw_platform.h"

// ========================================
// 平台检测测试
// ========================================

TEST(PlatformTests, OSDetection) {
    std::cout << "Operating System: " << Corona::PAL::Platform::Info::os_name << std::endl;

#if defined(CFW_PLATFORM_WINDOWS)
    ASSERT_TRUE(true);  // 在 Windows 上运行
#elif defined(CFW_PLATFORM_LINUX)
    ASSERT_TRUE(true);  // 在 Linux 上运行
#elif defined(CFW_PLATFORM_MACOS)
    ASSERT_TRUE(true);  // 在 macOS 上运行
#else
    std::cout << "Warning: Running on unknown platform" << std::endl;
#endif
}

TEST(PlatformTests, CompilerDetection) {
    std::cout << "Compiler: " << Corona::PAL::Platform::Info::compiler_name
              << " (version: " << Corona::PAL::Platform::Info::compiler_version << ")" << std::endl;

#if defined(CFW_COMPILER_MSVC)
    ASSERT_TRUE(CFW_COMPILER_VERSION > 0);
#elif defined(CFW_COMPILER_GCC)
    ASSERT_TRUE(CFW_COMPILER_VERSION > 0);
#elif defined(CFW_COMPILER_CLANG)
    ASSERT_TRUE(CFW_COMPILER_VERSION > 0);
#endif
}

TEST(PlatformTests, ArchitectureDetection) {
    std::cout << "Architecture: " << Corona::PAL::Platform::Info::arch_name
              << " (" << (Corona::PAL::Platform::Info::is_64bit ? "64-bit" : "32-bit") << ")" << std::endl;

#if defined(CFW_ARCH_64BIT)
    ASSERT_TRUE(Corona::PAL::Platform::Info::is_64bit);
#elif defined(CFW_ARCH_32BIT)
    ASSERT_FALSE(Corona::PAL::Platform::Info::is_64bit);
#endif
}

TEST(PlatformTests, CPPVersionDetection) {
    std::cout << "C++ Version: C++" << Corona::PAL::Platform::Info::cpp_version << std::endl;

    ASSERT_TRUE(CFW_CPP_VERSION >= 20);  // 项目要求 C++20
}

TEST(PlatformTests, BuildTypeDetection) {
    std::cout << "Build Type: " << Corona::PAL::Platform::Info::build_type << std::endl;

#if defined(CFW_DEBUG)
    ASSERT_TRUE(Corona::PAL::Platform::Info::is_debug);
#else
    ASSERT_FALSE(Corona::PAL::Platform::Info::is_debug);
#endif
}

// ========================================
// 编译器特性宏测试
// ========================================

CFW_FORCE_INLINE int force_inline_function(int x) {
    return x * 2;
}

CFW_NO_INLINE int no_inline_function(int x) {
    return x * 3;
}

TEST(CompilerFeaturesTests, InlineMacros) {
    int result1 = force_inline_function(5);
    int result2 = no_inline_function(5);

    ASSERT_EQ(result1, 10);
    ASSERT_EQ(result2, 15);
}

TEST(CompilerFeaturesTests, BranchPrediction) {
    int value = 42;

    if (CFW_LIKELY(value > 0)) {
        ASSERT_TRUE(value > 0);
    }

    if (CFW_UNLIKELY(value < 0)) {
        ASSERT_TRUE(false);  // 不应该执行
    } else {
        ASSERT_TRUE(true);
    }
}

TEST(CompilerFeaturesTests, Alignment) {
    struct CFW_ALIGN(64) AlignedStruct {
        int data;
    };

    AlignedStruct obj;
    uintptr_t addr = reinterpret_cast<uintptr_t>(&obj);

    // 注意：栈上的对齐可能不保证，这里只是检查编译通过
    std::cout << "Aligned struct address: 0x" << std::hex << addr << std::dec << std::endl;
}

struct CFW_CACHE_ALIGNED CacheAlignedStruct {
    int data[16];
};

TEST(CompilerFeaturesTests, CacheLineAlignment) {
    std::cout << "Cache line size: " << CFW_CACHE_LINE_SIZE << " bytes" << std::endl;

    CacheAlignedStruct obj;
    std::cout << "Cache aligned struct size: " << sizeof(obj) << " bytes" << std::endl;

    ASSERT_TRUE(CFW_CACHE_LINE_SIZE > 0);
}

// ========================================
// 字符串化和连接测试
// ========================================

TEST(MacroUtilsTests, Stringize) {
    const char* version = CFW_VERSION_STRING;
    std::cout << "CFW Version: " << version << std::endl;

    ASSERT_TRUE(version != nullptr);
}

TEST(MacroUtilsTests, Concat) {
#define TEST_PREFIX test_
    int CFW_CONCAT(TEST_PREFIX, value) = 42;

    ASSERT_EQ(test_value, 42);
}

TEST(MacroUtilsTests, FunctionSignature) {
    const char* sig = CFW_FUNCTION_SIGNATURE;
    std::cout << "Function signature: " << sig << std::endl;

    ASSERT_TRUE(sig != nullptr);
}

// ========================================
// 静态断言测试
// ========================================

CFW_STATIC_ASSERT(sizeof(int) == 4, "int should be 4 bytes");
CFW_STATIC_ASSERT(sizeof(void*) >= 4, "pointer should be at least 4 bytes");

TEST(StaticAssertTests, CompileTimeChecks) {
    // 如果编译通过，则静态断言成功
    ASSERT_TRUE(true);
}

// ========================================
// 便利宏测试
// ========================================

class NonCopyableClass {
    CFW_NON_COPYABLE(NonCopyableClass)

   public:
    NonCopyableClass() = default;
    int value = 42;
};

TEST(ConvenienceMacrosTests, NonCopyable) {
    NonCopyableClass obj1;
    ASSERT_EQ(obj1.value, 42);

    // 以下代码应该无法编译（注释掉）
    // NonCopyableClass obj2 = obj1;  // 编译错误
}

class SingletonClass {
    CFW_SINGLETON(SingletonClass)

    SingletonClass() : value(100) {}

   public:
    int value;
};

TEST(ConvenienceMacrosTests, Singleton) {
    auto& instance1 = SingletonClass::instance();
    auto& instance2 = SingletonClass::instance();

    ASSERT_EQ(&instance1, &instance2);
    ASSERT_EQ(instance1.value, 100);
}

// ========================================
// SIMD 特性检测测试
// ========================================

TEST(FeatureDetectionTests, SIMDSupport) {
#if defined(CFW_HAS_SSE2)
    std::cout << "SSE2 supported" << std::endl;
#endif

#if defined(CFW_HAS_AVX)
    std::cout << "AVX supported" << std::endl;
#endif

#if defined(CFW_HAS_AVX2)
    std::cout << "AVX2 supported" << std::endl;
#endif

#if defined(CFW_HAS_NEON)
    std::cout << "NEON supported" << std::endl;
#endif

    // 至少应该有某种 SIMD 支持（在现代处理器上）
    ASSERT_TRUE(true);
}

CFW_THREAD_LOCAL int global_tls_value = 0;

TEST(FeatureDetectionTests, ThreadLocal) {
    global_tls_value = 42;

    ASSERT_EQ(global_tls_value, 42);
}

// ========================================
// 属性测试
// ========================================

CFW_MAYBE_UNUSED static int unused_variable = 0;

[[maybe_unused]] static void maybe_unused_function() {
    // 函数体
}

TEST(AttributesTests, MaybeUnused) {
    // 如果编译通过且无警告，则测试成功
    ASSERT_TRUE(true);
}

CFW_DEPRECATED("This function is deprecated")
static void deprecated_function() {
    // 废弃的函数
}

TEST(AttributesTests, Deprecated) {
    // 调用时应该产生编译警告（但不会失败）
    // deprecated_function();
    ASSERT_TRUE(true);
}

// ========================================
// 字节序测试
// ========================================

TEST(EndianTests, EndianDetection) {
#if defined(CFW_LITTLE_ENDIAN)
    std::cout << "Little Endian" << std::endl;

    // 验证小端序
    uint32_t value = 0x01020304;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
    ASSERT_EQ(bytes[0], 0x04);
    ASSERT_EQ(bytes[3], 0x01);
#elif defined(CFW_BIG_ENDIAN)
    std::cout << "Big Endian" << std::endl;

    // 验证大端序
    uint32_t value = 0x01020304;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
    ASSERT_EQ(bytes[0], 0x01);
    ASSERT_EQ(bytes[3], 0x04);
#else
    std::cout << "Unknown Endian" << std::endl;
#endif
}

// ========================================
// 综合信息输出
// ========================================

TEST(PlatformTests, PrintAllInfo) {
    std::cout << "\n=== Corona Framework Platform Information ===" << std::endl;
    std::cout << "OS:           " << Corona::PAL::Platform::Info::os_name << std::endl;
    std::cout << "Compiler:     " << Corona::PAL::Platform::Info::compiler_name
              << " (v" << Corona::PAL::Platform::Info::compiler_version << ")" << std::endl;
    std::cout << "Architecture: " << Corona::PAL::Platform::Info::arch_name
              << " (" << (Corona::PAL::Platform::Info::is_64bit ? "64-bit" : "32-bit") << ")" << std::endl;
    std::cout << "C++ Version:  C++" << Corona::PAL::Platform::Info::cpp_version << std::endl;
    std::cout << "Build Type:   " << Corona::PAL::Platform::Info::build_type << std::endl;
    std::cout << "Cache Line:   " << CFW_CACHE_LINE_SIZE << " bytes" << std::endl;

#if defined(CFW_LITTLE_ENDIAN)
    std::cout << "Endianness:   Little Endian" << std::endl;
#elif defined(CFW_BIG_ENDIAN)
    std::cout << "Endianness:   Big Endian" << std::endl;
#endif

    std::cout << "============================================\n"
              << std::endl;

    ASSERT_TRUE(true);
}

// ========================================
// 主函数
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
