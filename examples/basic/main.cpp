#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "../dummy_system/dummy_system.h"
#include "corona/kernel/core/i_logger.h"
#include "corona/kernel/core/kernel_context.h"
#include "corona/pal/i_file_system.h"

// Forward declarations of factory functions
namespace Corona::PAL {
std::unique_ptr<IFileSystem> create_file_system();
}  // namespace Corona::PAL

int main() {
    // Initialize Kernel
    auto& kernel = Corona::Kernel::KernelContext::instance();
    if (!kernel.initialize()) {
        std::cerr << "Failed to initialize kernel" << std::endl;
        return 1;
    }

    auto logger = kernel.logger();
    logger->info("=== Corona Framework Test ===");

    // Test 0: Logger with multiple sinks
    logger->info("[Test 0] Logger Sink Test");
    auto file_sink = Corona::Kernel::create_file_sink("corona.log");
    logger->add_sink(file_sink);
    logger->info("  [OK] Added file sink, now logging to both console and file");
    logger->debug("  This is a debug message");
    logger->warning("  This is a warning message");
    logger->error("  This is an error message");

    // Test 1: File System
    logger->info("[Test 1] File System Test");
    auto fs = Corona::PAL::create_file_system();

    // Write test
    std::string test_content = "Hello from Corona Framework!";
    std::vector<std::byte> data;
    data.reserve(test_content.size());
    for (char c : test_content) {
        data.push_back(static_cast<std::byte>(c));
    }

    if (fs->write_all_bytes("test.txt", data)) {
        logger->info("  [OK] File write successful");
    } else {
        logger->error("  [FAIL] File write failed");
    }

    // Exists test
    if (fs->exists("test.txt")) {
        logger->info("  [OK] File exists check successful");
    } else {
        logger->error("  [FAIL] File exists check failed");
    }

    // Read test
    auto read_data = fs->read_all_bytes("test.txt");
    if (!read_data.empty()) {
        std::string read_content(reinterpret_cast<const char*>(read_data.data()), read_data.size());
        logger->info("  [OK] File read successful: " + read_content);
    } else {
        logger->error("  [FAIL] File read failed");
    }

    // Test 2: VFS
    logger->info("[Test 2] Virtual File System Test");
    auto vfs = kernel.vfs();
    vfs->mount("/data/", "./data/");
    logger->info("  [OK] Mounted /data/ to ./data/");

    // Test 3: Event Bus
    logger->info("[Test 3] Event Bus Test");
    auto event_bus = kernel.event_bus();

    struct TestEvent {
        std::string message;
    };

    event_bus->subscribe<TestEvent>([logger](const TestEvent& event) {
        logger->info("  [OK] Event received: " + event.message);
    });

    event_bus->publish(TestEvent{"Hello from Event Bus!"});

    // Test 4: Multi-threaded System Architecture
    logger->info("[Test 4] Multi-threaded System Test");
    auto system_manager = kernel.system_manager();

    // Register a test system
    auto dummy_system = std::make_shared<Corona::Kernel::DummySystem>();
    system_manager->register_system(dummy_system);

    // Initialize all systems
    if (system_manager->initialize_all()) {
        logger->info("  [OK] Systems initialized");
    } else {
        logger->error("  [FAIL] Systems initialization failed");
        return 1;
    }

    // Start all systems
    system_manager->start_all();
    logger->info("  [OK] Systems started, running for 5 seconds...");

    // Let systems run for a while
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Stop all systems
    system_manager->stop_all();
    logger->info("  [OK] Systems stopped");

    // Shutdown all systems
    system_manager->shutdown_all();
    logger->info("  [OK] Systems shutdown");

    logger->info("=== All tests completed ===");

    // Shutdown Kernel
    kernel.shutdown();

    return 0;
}
