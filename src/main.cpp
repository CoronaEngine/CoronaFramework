#include <iostream>
#include <memory>
#include <string>

#include "corona/pal/i_file_system.h"
#include "corona/pal/i_window.h"

// Forward declarations of factory functions
namespace Corona::PAL {
std::unique_ptr<IFileSystem> create_file_system();
std::unique_ptr<IWindow> create_window();
}  // namespace Corona::PAL

int main() {
    std::cout << "=== Corona Framework Test ===" << std::endl;

    // Test 1: File System
    std::cout << "\n[Test 1] File System Test" << std::endl;
    auto fs = Corona::PAL::create_file_system();

    // Write test
    std::string test_content = "Hello from Corona Framework!";
    std::vector<std::byte> data;
    data.reserve(test_content.size());
    for (char c : test_content) {
        data.push_back(static_cast<std::byte>(c));
    }

    if (fs->write_all_bytes("test.txt", data)) {
        std::cout << "  ✓ File write successful" << std::endl;
    } else {
        std::cout << "  ✗ File write failed" << std::endl;
    }

    // Exists test
    if (fs->exists("test.txt")) {
        std::cout << "  ✓ File exists check successful" << std::endl;
    } else {
        std::cout << "  ✗ File exists check failed" << std::endl;
    }

    // Read test
    auto read_data = fs->read_all_bytes("test.txt");
    if (!read_data.empty()) {
        std::string read_content(reinterpret_cast<const char*>(read_data.data()), read_data.size());
        std::cout << "  ✓ File read successful: " << read_content << std::endl;
    } else {
        std::cout << "  ✗ File read failed" << std::endl;
    }

    // Test 2: Window System (commented out for now as it requires user interaction)
    std::cout << "\n[Test 2] Window System Test" << std::endl;
    std::cout << "  ℹ Window test skipped (requires GUI)" << std::endl;

    /*
    auto window = Corona::PAL::create_window();
    if (window->create("Corona Test Window", 800, 600)) {
        std::cout << "  ✓ Window creation successful" << std::endl;

        while (window->is_open()) {
            window->poll_events();
        }
    } else {
        std::cout << "  ✗ Window creation failed" << std::endl;
    }
    */

    std::cout << "\n=== All tests completed ===" << std::endl;
    return 0;
}