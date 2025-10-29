#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "../test_framework.h"
#include "corona/kernel/core/i_vfs.h"

namespace fs = std::filesystem;
using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// VFS Basic Tests
// ========================================

TEST(VFS, CreateVFS) {
    auto vfs = create_vfs();
    ASSERT_TRUE(vfs != nullptr);
}

TEST(VFS, MountDirectory) {
    auto vfs = create_vfs();

    // Mount current directory to /data
    bool result = vfs->mount("/data", ".");
    ASSERT_TRUE(result);
}

TEST(VFS, UnmountDirectory) {
    auto vfs = create_vfs();

    vfs->mount("/data", ".");
    vfs->unmount("/data");

    // After unmount, resolve should return empty or the original path
    std::string resolved = vfs->resolve("/data/test.txt");
    // VFS should not resolve unmounted paths
}

TEST(VFS, ResolveVirtualPath) {
    auto vfs = create_vfs();

    // Create a test directory
    const char* test_dir = "test_vfs_mount";
    fs::create_directory(test_dir);

    vfs->mount("/assets", test_dir);

    std::string resolved = vfs->resolve("/assets/config.txt");
    ASSERT_TRUE(resolved.find(test_dir) != std::string::npos);
    ASSERT_TRUE(resolved.find("config.txt") != std::string::npos);

    // Cleanup
    vfs->unmount("/assets");
    fs::remove(test_dir);
}

// ========================================
// File Operation Tests
// ========================================

TEST(VFS, WriteAndReadFile) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_rw";
    fs::create_directory(test_dir);

    vfs->mount("/temp", test_dir);

    // Write file
    std::string test_data = "Hello VFS World!";
    std::vector<std::byte> write_data;
    for (char c : test_data) {
        write_data.push_back(static_cast<std::byte>(c));
    }

    bool write_result = vfs->write_file("/temp/test.txt", write_data);
    ASSERT_TRUE(write_result);

    // Read file
    auto read_data = vfs->read_file("/temp/test.txt");
    ASSERT_EQ(read_data.size(), test_data.size());

    // Verify content
    std::string read_string;
    for (auto byte : read_data) {
        read_string += static_cast<char>(byte);
    }
    ASSERT_EQ(read_string, test_data);

    // Cleanup
    vfs->unmount("/temp");
    fs::remove_all(test_dir);
}

TEST(VFS, FileExists) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_exists";
    fs::create_directory(test_dir);

    vfs->mount("/check", test_dir);

    // File doesn't exist initially
    ASSERT_FALSE(vfs->exists("/check/nonexistent.txt"));

    // Create file
    std::vector<std::byte> data;
    data.push_back(static_cast<std::byte>('x'));
    vfs->write_file("/check/exists.txt", data);

    // File should exist now
    ASSERT_TRUE(vfs->exists("/check/exists.txt"));

    // Cleanup
    vfs->unmount("/check");
    fs::remove_all(test_dir);
}

// ========================================
// Directory Operation Tests
// ========================================

TEST(VFS, CreateDirectory) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_mkdir";
    fs::create_directory(test_dir);

    vfs->mount("/root", test_dir);

    // Create subdirectory
    bool result = vfs->create_directory("/root/subdir");
    ASSERT_TRUE(result);

    // Verify directory exists
    std::string physical_path = vfs->resolve("/root/subdir");
    ASSERT_TRUE(fs::exists(physical_path));

    // Cleanup
    vfs->unmount("/root");
    fs::remove_all(test_dir);
}

TEST(VFS, ListDirectory) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_list";
    fs::create_directory(test_dir);

    vfs->mount("/list", test_dir);

    // Create test files using physical filesystem to ensure they exist
    std::ofstream f1((fs::path(test_dir) / "file1.txt").string());
    f1 << "test";
    f1.close();

    std::ofstream f2((fs::path(test_dir) / "file2.txt").string());
    f2 << "test";
    f2.close();

    std::ofstream f3((fs::path(test_dir) / "file3.txt").string());
    f3 << "test";
    f3.close();

    // List directory
    auto files = vfs->list_directory("/list");

    // Should have at least 3 files
    ASSERT_TRUE(files.size() >= 3);

    // Check if our files are in the list
    bool found1 = false, found2 = false, found3 = false;
    for (const auto& file : files) {
        if (file.find("file1.txt") != std::string::npos) found1 = true;
        if (file.find("file2.txt") != std::string::npos) found2 = true;
        if (file.find("file3.txt") != std::string::npos) found3 = true;
    }

    ASSERT_TRUE(found1);
    ASSERT_TRUE(found2);
    ASSERT_TRUE(found3);

    // Cleanup
    vfs->unmount("/list");
    fs::remove_all(test_dir);
}

// ========================================
// Multiple Mount Points Tests
// ========================================

TEST(VFS, MultipleMountPoints) {
    auto vfs = create_vfs();

    const char* dir1 = "test_vfs_mount1";
    const char* dir2 = "test_vfs_mount2";

    fs::create_directory(dir1);
    fs::create_directory(dir2);

    // Mount two directories
    vfs->mount("/assets", dir1);
    vfs->mount("/config", dir2);

    // Create files in different mount points
    std::vector<std::byte> data;
    data.push_back(static_cast<std::byte>('x'));

    vfs->write_file("/assets/asset.txt", data);
    vfs->write_file("/config/config.txt", data);

    // Verify both files exist
    ASSERT_TRUE(vfs->exists("/assets/asset.txt"));
    ASSERT_TRUE(vfs->exists("/config/config.txt"));

    // Cleanup
    vfs->unmount("/assets");
    vfs->unmount("/config");
    fs::remove_all(dir1);
    fs::remove_all(dir2);
}

// ========================================
// Error Handling Tests
// ========================================

TEST(VFS, ReadNonExistentFile) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_error";
    fs::create_directory(test_dir);
    vfs->mount("/error", test_dir);

    // Try to read file that doesn't exist
    auto data = vfs->read_file("/error/nonexistent.txt");
    ASSERT_TRUE(data.empty());

    // Cleanup
    vfs->unmount("/error");
    fs::remove_all(test_dir);
}

TEST(VFS, WriteToInvalidPath) {
    auto vfs = create_vfs();

    // Try to write without mounting
    std::vector<std::byte> data{static_cast<std::byte>('x')};
    bool result = vfs->write_file("/unmounted/file.txt", data);
    ASSERT_FALSE(result);
}

TEST(VFS, ExistsOnUnmountedPath) {
    auto vfs = create_vfs();

    // Check existence on unmounted path
    bool exists = vfs->exists("/unmounted/file.txt");
    ASSERT_FALSE(exists);
}

TEST(VFS, ListNonExistentDirectory) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_list_error";
    fs::create_directory(test_dir);
    vfs->mount("/listtest", test_dir);

    // Try to list non-existent subdirectory
    auto files = vfs->list_directory("/listtest/nonexistent");
    ASSERT_TRUE(files.empty());

    // Cleanup
    vfs->unmount("/listtest");
    fs::remove_all(test_dir);
}

// ========================================
// Edge Cases and Boundary Tests
// ========================================

TEST(VFS, EmptyFileName) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_empty";
    fs::create_directory(test_dir);
    vfs->mount("/empty", test_dir);

    // Try operations with empty filename
    std::vector<std::byte> data{static_cast<std::byte>('x')};
    bool result = vfs->write_file("/empty/", data);
    ASSERT_FALSE(result);

    // Cleanup
    vfs->unmount("/empty");
    fs::remove_all(test_dir);
}

TEST(VFS, VeryLongPath) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_long";
    fs::create_directory(test_dir);
    vfs->mount("/long", test_dir);

    // Create a very long filename (but still reasonable)
    std::string long_name = "/long/";
    for (int i = 0; i < 100; ++i) {
        long_name += "a";
    }
    long_name += ".txt";

    std::vector<std::byte> data{static_cast<std::byte>('x')};
    bool result = vfs->write_file(long_name, data);
    // May succeed or fail depending on OS limits, just shouldn't crash

    // Cleanup
    vfs->unmount("/long");
    fs::remove_all(test_dir);
}

TEST(VFS, SpecialCharactersInFilename) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_special";
    fs::create_directory(test_dir);
    vfs->mount("/special", test_dir);

    // Try filename with spaces (should work)
    std::vector<std::byte> data{static_cast<std::byte>('x')};
    bool result1 = vfs->write_file("/special/test file.txt", data);
    // Should work on most systems

    if (result1) {
        ASSERT_TRUE(vfs->exists("/special/test file.txt"));
    }

    // Cleanup
    vfs->unmount("/special");
    fs::remove_all(test_dir);
}

TEST(VFS, WriteEmptyFile) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_empty_file";
    fs::create_directory(test_dir);
    vfs->mount("/emptyfile", test_dir);

    // Write empty file
    std::vector<std::byte> empty_data;
    bool result = vfs->write_file("/emptyfile/empty.txt", empty_data);
    ASSERT_TRUE(result);

    // Read it back
    auto read_data = vfs->read_file("/emptyfile/empty.txt");
    ASSERT_TRUE(read_data.empty());

    // Cleanup
    vfs->unmount("/emptyfile");
    fs::remove_all(test_dir);
}

TEST(VFS, OverwriteExistingFile) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_overwrite";
    fs::create_directory(test_dir);
    vfs->mount("/overwrite", test_dir);

    // Write file
    std::vector<std::byte> data1{
        static_cast<std::byte>('H'),
        static_cast<std::byte>('e'),
        static_cast<std::byte>('l'),
        static_cast<std::byte>('l'),
        static_cast<std::byte>('o')};
    vfs->write_file("/overwrite/test.txt", data1);

    // Overwrite with different data
    std::vector<std::byte> data2{
        static_cast<std::byte>('B'),
        static_cast<std::byte>('y'),
        static_cast<std::byte>('e')};
    vfs->write_file("/overwrite/test.txt", data2);

    // Read back
    auto read_data = vfs->read_file("/overwrite/test.txt");
    ASSERT_EQ(read_data.size(), 3u);
    ASSERT_EQ(static_cast<char>(read_data[0]), 'B');
    ASSERT_EQ(static_cast<char>(read_data[1]), 'y');
    ASSERT_EQ(static_cast<char>(read_data[2]), 'e');

    // Cleanup
    vfs->unmount("/overwrite");
    fs::remove_all(test_dir);
}

TEST(VFS, LargeFileReadWrite) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_large";
    fs::create_directory(test_dir);
    vfs->mount("/large", test_dir);

    // Create 1MB data
    std::vector<std::byte> large_data(1024 * 1024);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<std::byte>(i % 256);
    }

    // Write large file
    bool write_result = vfs->write_file("/large/bigfile.dat", large_data);
    ASSERT_TRUE(write_result);

    // Read it back
    auto read_data = vfs->read_file("/large/bigfile.dat");
    ASSERT_EQ(read_data.size(), large_data.size());

    // Verify a few bytes
    ASSERT_EQ(read_data[0], large_data[0]);
    ASSERT_EQ(read_data[1000], large_data[1000]);
    ASSERT_EQ(read_data[large_data.size() - 1], large_data[large_data.size() - 1]);

    // Cleanup
    vfs->unmount("/large");
    fs::remove_all(test_dir);
}

TEST(VFS, MultipleUnmount) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_multi_unmount";
    fs::create_directory(test_dir);

    vfs->mount("/multi", test_dir);
    vfs->unmount("/multi");
    vfs->unmount("/multi");  // Should not crash

    // Cleanup
    fs::remove_all(test_dir);
}

TEST(VFS, UnmountNonExistentPath) {
    auto vfs = create_vfs();

    // Should not crash
    vfs->unmount("/never_mounted");
}

TEST(VFS, PathNormalization) {
    auto vfs = create_vfs();

    const char* test_dir = "test_vfs_norm";
    fs::create_directory(test_dir);
    vfs->mount("/norm", test_dir);

    // Write with double slashes
    std::vector<std::byte> data{static_cast<std::byte>('x')};
    vfs->write_file("/norm//test.txt", data);

    // Try to read with normalized path
    ASSERT_TRUE(vfs->exists("/norm/test.txt"));

    // Cleanup
    vfs->unmount("/norm");
    fs::remove_all(test_dir);
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
