# Corona Framework Tests

本目录包含 Corona Framework 的所有测试代码。

## 测试结构

```
tests/
├── kernel/               # Kernel 层测试
│   ├── logger_test.cpp
│   ├── event_bus_test.cpp
│   ├── vfs_test.cpp
│   ├── plugin_manager_test.cpp
│   ├── system_manager_test.cpp
│   └── thread_safety_test.cpp
├── pal/                  # PAL 层测试
│   └── file_system_test.cpp
└── CMakeLists.txt
```

## 运行测试

```bash
# 构建测试
cmake -B build
cmake --build build --target tests

# 运行所有测试
cd build
ctest

# 运行特定测试
./tests/kernel_tests
```

## 使用 ThreadSanitizer

```bash
# 编译时启用 ThreadSanitizer
cmake -B build -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build

# 运行测试检测数据竞争
./build/tests/kernel_tests
```

## 测试框架

当前使用简单的自定义测试宏，未来可以考虑集成：
- Google Test
- Catch2
- Boost.Test
