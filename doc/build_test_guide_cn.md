# 构建与测试指南

本文档说明如何在 Windows 平台上配置、构建与验证 Corona Framework，并附带常用开发工具链建议。

## 环境要求
- C++20 编译器（推荐 MSVC 19.3x 或 clang-cl 16+）。
- CMake ≥ 3.26，建议配合 Ninja 多配置生成器。
- Python（可选，用于脚本或持续集成工具）。
- Git（用于获取子模块与提交）。

## 预置构建脚本
项目提供 `CMakePresets.json`，常用预设：
- `ninja-msvc`：Windows + MSVC + Ninja，多配置构建。
- `ninja-clang`：Windows + clang-cl。
- 如需 IDE 集成，可使用 `vs2022` 预设生成 Visual Studio 解决方案。

### 配置命令
```powershell
cmake --preset ninja-msvc
```
生成目录默认为 `build/`。

### 编译命令
```powershell
cmake --build build --config Debug
```
如需特定目标：
```powershell
cmake --build build --target corona_kernel --config Release
```
若要一次性构建全部测试，执行：
```powershell
cmake --build build --target tests --config Debug
```

## 运行测试
项目使用自研测试框架，所有测试都注册为 CTest 目标。
```powershell
cd build
ctest -C Debug --output-on-failure
```
测试生成的可执行文件位于 `build/tests/<Config>/`，可以按需直接运行，例如：
```powershell
.\build\tests\Debug\kernel_event_stream_test.exe
```

## 示例程序
`examples/` 目录提供事件、系统、VFS、游戏循环等六个示例：
```powershell
cmake --build build --target example_06_game_loop --config Debug
.\build\examples\Debug\example_06_game_loop.exe
```
示例默认依赖 `KernelContext` 初始化，演示如何组合日志、事件与系统管理。

## 代码格式化
使用仓库根目录的 `code-format.ps1` 调用 clang-format：
```powershell
.\code-format.ps1
```

## 常见问题速查
- **生成缓存损坏**：删除 `build/` 后重新执行 `cmake --preset ...`。
- **缺少 fast_io 依赖**：首次配置时 CMake 会自动通过 `FetchContent` 下载，请确保可以访问 GitHub。
- **测试超时**：多线程压力测试在 Debug 构建下可能较慢，可改用 `ctest -C Release`。
