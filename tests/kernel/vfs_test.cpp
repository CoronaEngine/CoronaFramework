#include "../test_framework.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

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
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
