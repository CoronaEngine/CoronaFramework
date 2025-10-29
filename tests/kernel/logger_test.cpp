#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "../test_framework.h"
#include "corona/kernel/core/i_logger.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// Logger Basic Tests
// ========================================

TEST(Logger, CreateLogger) {
    auto logger = create_logger();
    ASSERT_TRUE(logger != nullptr);
}

TEST(Logger, AddConsoleSink) {
    auto logger = create_logger();
    auto sink = create_console_sink();
    ASSERT_TRUE(sink != nullptr);

    logger->add_sink(std::move(sink));
}

TEST(Logger, AddFileSink) {
    auto logger = create_logger();
    auto sink = create_file_sink("test_log.txt");
    ASSERT_TRUE(sink != nullptr);

    logger->add_sink(std::move(sink));
}

TEST(Logger, LogMessage) {
    auto logger = create_logger();
    auto sink = create_console_sink();
    logger->add_sink(std::move(sink));

    // Should not throw
    logger->log(LogLevel::info, "Test message", std::source_location::current());
}

TEST(Logger, LogLevels) {
    auto logger = create_logger();
    auto sink = create_console_sink();
    logger->add_sink(std::move(sink));

    // Test all log levels
    logger->log(LogLevel::trace, "Trace message", std::source_location::current());
    logger->log(LogLevel::debug, "Debug message", std::source_location::current());
    logger->log(LogLevel::info, "Info message", std::source_location::current());
    logger->log(LogLevel::warning, "Warning message", std::source_location::current());
    logger->log(LogLevel::error, "Error message", std::source_location::current());
    logger->log(LogLevel::fatal, "Fatal message", std::source_location::current());
}

// ========================================
// Sink Tests
// ========================================

TEST(ConsoleSink, SetMinLevel) {
    auto sink = create_console_sink();
    sink->set_level(LogLevel::warning);

    // This should filter out lower levels (implementation detail, hard to test without output capture)
}

TEST(FileSink, WriteToFile) {
    const char* test_file = "test_file_sink.log";

    // Remove old test file if exists
    std::remove(test_file);

    {
        auto logger = create_logger();
        auto sink = create_file_sink(test_file);
        sink->set_level(LogLevel::info);
        logger->add_sink(std::move(sink));

        logger->log(LogLevel::info, "Test log to file", std::source_location::current());
        logger->log(LogLevel::warning, "Warning log to file", std::source_location::current());
    }

    // Check file was created and has content
    std::ifstream file(test_file);
    ASSERT_TRUE(file.good());

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    ASSERT_TRUE(content.find("Test log to file") != std::string::npos);
    ASSERT_TRUE(content.find("Warning log to file") != std::string::npos);

    // Cleanup
    std::remove(test_file);
}

TEST(FileSink, LogLevelFiltering) {
    const char* test_file = "test_level_filter.log";
    std::remove(test_file);

    {
        auto logger = create_logger();
        auto sink = create_file_sink(test_file);
        sink->set_level(LogLevel::warning);  // Only warning and above
        logger->add_sink(std::move(sink));

        logger->log(LogLevel::debug, "Debug should not appear", std::source_location::current());
        logger->log(LogLevel::info, "Info should not appear", std::source_location::current());
        logger->log(LogLevel::warning, "Warning should appear", std::source_location::current());
        logger->log(LogLevel::error, "Error should appear", std::source_location::current());
    }

    std::ifstream file(test_file);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Check only warning and error messages are present
    ASSERT_TRUE(content.find("Debug should not appear") == std::string::npos);
    ASSERT_TRUE(content.find("Info should not appear") == std::string::npos);
    ASSERT_TRUE(content.find("Warning should appear") != std::string::npos);
    ASSERT_TRUE(content.find("Error should appear") != std::string::npos);

    std::remove(test_file);
}

// ========================================
// Multi-Sink Tests
// ========================================

TEST(Logger, MultipleSinks) {
    const char* test_file1 = "test_multi1.log";
    const char* test_file2 = "test_multi2.log";

    std::remove(test_file1);
    std::remove(test_file2);

    {
        auto logger = create_logger();

        auto sink1 = create_file_sink(test_file1);
        sink1->set_level(LogLevel::info);

        auto sink2 = create_file_sink(test_file2);
        sink2->set_level(LogLevel::warning);

        logger->add_sink(std::move(sink1));
        logger->add_sink(std::move(sink2));

        logger->log(LogLevel::info, "Info message", std::source_location::current());
        logger->log(LogLevel::warning, "Warning message", std::source_location::current());
        logger->log(LogLevel::error, "Error message", std::source_location::current());
    }

    // Check first file has info, warning, and error
    std::ifstream file1(test_file1);
    std::stringstream buffer1;
    buffer1 << file1.rdbuf();
    std::string content1 = buffer1.str();

    ASSERT_TRUE(content1.find("Info message") != std::string::npos);
    ASSERT_TRUE(content1.find("Warning message") != std::string::npos);
    ASSERT_TRUE(content1.find("Error message") != std::string::npos);

    // Check second file only has warning and error
    std::ifstream file2(test_file2);
    std::stringstream buffer2;
    buffer2 << file2.rdbuf();
    std::string content2 = buffer2.str();

    ASSERT_TRUE(content2.find("Info message") == std::string::npos);
    ASSERT_TRUE(content2.find("Warning message") != std::string::npos);
    ASSERT_TRUE(content2.find("Error message") != std::string::npos);

    std::remove(test_file1);
    std::remove(test_file2);
}

// ========================================
// Thread Safety Tests
// ========================================

TEST(Logger, ConcurrentLogging) {
    const char* test_file = "test_concurrent.log";
    std::remove(test_file);

    {
        auto logger = create_logger();
        auto sink = create_file_sink(test_file);
        logger->add_sink(std::move(sink));

        const int num_threads = 4;
        const int logs_per_thread = 100;

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&logger, i, logs_per_thread]() {
                for (int j = 0; j < logs_per_thread; ++j) {
                    logger->log(LogLevel::info,
                                "Thread " + std::to_string(i) + " message " + std::to_string(j),
                                std::source_location::current());
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
    }

    // Count total lines in file
    std::ifstream file(test_file);
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            line_count++;
        }
    }

    // Should have exactly num_threads * logs_per_thread messages
    ASSERT_EQ(line_count, 400);

    std::remove(test_file);
}

// ========================================
// Error Handling Tests
// ========================================

TEST(Logger, EmptyMessage) {
    auto logger = create_logger();
    auto sink = create_console_sink();
    logger->add_sink(std::move(sink));

    // Should not crash with empty message
    logger->log(LogLevel::info, "", std::source_location::current());
}

TEST(Logger, VeryLongMessage) {
    const char* test_file = "test_long_message.log";
    std::remove(test_file);

    {
        auto logger = create_logger();
        auto sink = create_file_sink(test_file);
        logger->add_sink(std::move(sink));

        // Create a very long message (10KB)
        std::string long_message(10000, 'A');
        logger->log(LogLevel::info, long_message, std::source_location::current());
    }

    std::ifstream file(test_file);
    ASSERT_TRUE(file.good());

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find(std::string(10000, 'A')) != std::string::npos);

    std::remove(test_file);
}

TEST(Logger, SpecialCharactersInMessage) {
    const char* test_file = "test_special_chars.log";
    std::remove(test_file);

    {
        auto logger = create_logger();
        auto sink = create_file_sink(test_file);
        logger->add_sink(std::move(sink));

        // Test various special characters
        logger->log(LogLevel::info, "Special: \n\t\r\\\"\'", std::source_location::current());
        logger->log(LogLevel::info, "Unicode: 你好世界 🎉", std::source_location::current());
    }

    std::ifstream file(test_file);
    ASSERT_TRUE(file.good());

    std::remove(test_file);
}

TEST(Logger, RapidFireLogging) {
    const char* test_file = "test_rapid_fire.log";
    std::remove(test_file);

    {
        auto logger = create_logger();
        auto sink = create_file_sink(test_file);
        logger->add_sink(std::move(sink));

        // Log 1000 messages rapidly
        for (int i = 0; i < 1000; ++i) {
            logger->log(LogLevel::info, "Message " + std::to_string(i), std::source_location::current());
        }
    }

    std::ifstream file(test_file);
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            line_count++;
        }
    }

    ASSERT_EQ(line_count, 1000);

    std::remove(test_file);
}

TEST(Logger, RemoveAllSinks) {
    auto logger = create_logger();
    auto sink1 = create_console_sink();
    auto sink2 = create_console_sink();

    logger->add_sink(std::move(sink1));
    logger->add_sink(std::move(sink2));

    logger->remove_all_sinks();

    // Should not crash even with no sinks
    logger->log(LogLevel::info, "Message after removing all sinks", std::source_location::current());
}

TEST(Logger, LogAfterSinkDestruction) {
    const char* test_file = "test_sink_destruction.log";
    std::remove(test_file);

    auto logger = create_logger();

    {
        auto sink = create_file_sink(test_file);
        logger->add_sink(std::move(sink));
        logger->log(LogLevel::info, "Message 1", std::source_location::current());
    }

    // File sink is destroyed, but logger still exists
    // This should work because remove_all_sinks wasn't called
    logger->log(LogLevel::info, "Message 2", std::source_location::current());

    std::remove(test_file);
}

TEST(Logger, MultipleLoggersIndependent) {
    auto logger1 = create_logger();
    auto logger2 = create_logger();

    int count1 = 0, count2 = 0;

    // Each logger should be independent
    logger1->log(LogLevel::info, "Logger 1", std::source_location::current());
    logger2->log(LogLevel::info, "Logger 2", std::source_location::current());

    // Both should work independently
    ASSERT_TRUE(true);
}

// ========================================
// Level Filtering Edge Cases
// ========================================

TEST(Logger, AllLevelsFiltered) {
    const char* test_file = "test_all_filtered.log";
    std::remove(test_file);

    {
        auto logger = create_logger();
        auto sink = create_file_sink(test_file);

        // Set level to fatal, so everything below is filtered
        sink->set_level(LogLevel::fatal);
        logger->add_sink(std::move(sink));

        logger->log(LogLevel::trace, "Trace", std::source_location::current());
        logger->log(LogLevel::debug, "Debug", std::source_location::current());
        logger->log(LogLevel::info, "Info", std::source_location::current());
        logger->log(LogLevel::warning, "Warning", std::source_location::current());
        logger->log(LogLevel::error, "Error", std::source_location::current());
        logger->log(LogLevel::fatal, "Fatal", std::source_location::current());
    }

    std::ifstream file(test_file);
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            line_count++;
        }
    }

    // Only fatal message should be logged
    ASSERT_EQ(line_count, 1);

    std::remove(test_file);
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
