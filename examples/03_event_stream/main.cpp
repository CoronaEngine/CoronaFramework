// EventStream Example - 展示响应式数据流
// 演示如何使用EventStream进行流式数据处理、转换和过滤

#include <iostream>
#include <string>
#include <vector>

#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_stream.h"

using namespace Corona::Kernel;

// ========================================
// 定义数据结构
// ========================================

struct SensorData {
    int sensor_id;
    double value;
    std::string unit;
};

struct ProcessedData {
    int sensor_id;
    double processed_value;
    bool is_anomaly;
};

// ========================================
// 主程序
// ========================================

int main() {
    std::cout << "=== Corona Framework - EventStream Example ===" << std::endl;
    std::cout << std::endl;

    // 初始化内核
    auto& kernel = KernelContext::instance();
    if (!kernel.initialize()) {
        std::cerr << "Failed to initialize kernel!" << std::endl;
        return 1;
    }

    auto* logger = kernel.logger();

    // ========================================
    // 示例 1: 基本的流订阅
    // ========================================
    std::cout << "[Example 1] Basic Stream Subscription" << std::endl;

    auto stream1 = create_event_stream<int>();
    
    // 订阅流
    stream1->subscribe([](int value) {
        std::cout << "  Received: " << value << std::endl;
    });

    // 发送数据
    stream1->next(10);
    stream1->next(20);
    stream1->next(30);
    std::cout << std::endl;

    // ========================================
    // 示例 2: 流转换 (map)
    // ========================================
    std::cout << "[Example 2] Stream Transformation (map)" << std::endl;

    auto stream2 = create_event_stream<int>();
    
    // 转换:将每个值乘以2
    auto doubled_stream = stream2->map<int>([](int value) {
        return value * 2;
    });

    doubled_stream->subscribe([](int value) {
        std::cout << "  Doubled value: " << value << std::endl;
    });

    stream2->next(5);
    stream2->next(10);
    stream2->next(15);
    std::cout << std::endl;

    // ========================================
    // 示例 3: 流过滤 (filter)
    // ========================================
    std::cout << "[Example 3] Stream Filtering" << std::endl;

    auto stream3 = create_event_stream<int>();
    
    // 过滤:只保留偶数
    auto even_stream = stream3->filter([](int value) {
        return value % 2 == 0;
    });

    even_stream->subscribe([](int value) {
        std::cout << "  Even number: " << value << std::endl;
    });

    for (int i = 1; i <= 10; ++i) {
        stream3->next(i);
    }
    std::cout << std::endl;

    // ========================================
    // 示例 4: 链式操作
    // ========================================
    std::cout << "[Example 4] Chained Operations" << std::endl;

    auto stream4 = create_event_stream<int>();
    
    // 链式操作:过滤 -> 转换 -> 订阅
    stream4->filter([](int value) {
        return value > 0;  // 只保留正数
    })->map<int>([](int value) {
        return value * value;  // 平方
    })->subscribe([](int value) {
        std::cout << "  Squared positive: " << value << std::endl;
    });

    stream4->next(-5);  // 被过滤掉
    stream4->next(3);   // 输出 9
    stream4->next(0);   // 被过滤掉
    stream4->next(4);   // 输出 16
    std::cout << std::endl;

    // ========================================
    // 示例 5: 实际应用 - 传感器数据处理
    // ========================================
    std::cout << "[Example 5] Real-world Example - Sensor Data Processing" << std::endl;

    auto sensor_stream = create_event_stream<SensorData>();
    
    // 数据管道:过滤异常值 -> 归一化 -> 检测异常
    sensor_stream
        ->filter([](const SensorData& data) {
            // 过滤:只处理有效范围内的数据
            return data.value >= 0 && data.value <= 100;
        })
        ->map<ProcessedData>([](const SensorData& data) {
            // 转换:归一化并检测异常
            double normalized = data.value / 100.0;
            bool anomaly = (normalized > 0.9 || normalized < 0.1);
            
            return ProcessedData{
                data.sensor_id,
                normalized,
                anomaly
            };
        })
        ->subscribe([logger](const ProcessedData& data) {
            // 处理结果
            std::string msg = "Sensor " + std::to_string(data.sensor_id) + 
                            ": " + std::to_string(data.processed_value);
            
            if (data.is_anomaly) {
                msg += " ⚠️ ANOMALY DETECTED!";
                logger->warning(msg);
            } else {
                logger->info(msg);
            }
        });

    // 模拟传感器数据
    sensor_stream->next(SensorData{1, 45.5, "°C"});
    sensor_stream->next(SensorData{2, 95.0, "°C"});  // 异常值
    sensor_stream->next(SensorData{1, 50.2, "°C"});
    sensor_stream->next(SensorData{3, 5.0, "°C"});   // 异常值
    sensor_stream->next(SensorData{2, 110.0, "°C"}); // 超出范围,被过滤
    std::cout << std::endl;

    // ========================================
    // 示例 6: 错误处理
    // ========================================
    std::cout << "[Example 6] Error Handling" << std::endl;

    auto stream6 = create_event_stream<int>();
    
    stream6->subscribe(
        [](int value) {
            std::cout << "  Value: " << value << std::endl;
        },
        [](const std::exception& e) {
            std::cerr << "  ❌ Error: " << e.what() << std::endl;
        }
    );

    stream6->next(100);
    
    try {
        throw std::runtime_error("Simulated error");
    } catch (const std::exception& e) {
        stream6->error(e);
    }
    std::cout << std::endl;

    // ========================================
    // 示例 7: 流完成
    // ========================================
    std::cout << "[Example 7] Stream Completion" << std::endl;

    auto stream7 = create_event_stream<int>();
    
    stream7->subscribe(
        [](int value) {
            std::cout << "  Processing: " << value << std::endl;
        },
        [](const std::exception&) {},
        []() {
            std::cout << "  ✓ Stream completed!" << std::endl;
        }
    );

    stream7->next(1);
    stream7->next(2);
    stream7->next(3);
    stream7->complete();
    std::cout << std::endl;

    // ========================================
    // 示例 8: 背压处理
    // ========================================
    std::cout << "[Example 8] Backpressure Handling" << std::endl;

    auto stream8 = create_event_stream<int>(3);  // 缓冲区大小为3
    
    int processed_count = 0;
    stream8->subscribe([&processed_count](int value) {
        processed_count++;
        std::cout << "  Processing #" << processed_count << ": " << value << std::endl;
        // 模拟慢速消费者
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    // 快速生产数据
    for (int i = 1; i <= 5; ++i) {
        stream8->next(i);
        std::cout << "  Sent: " << i << std::endl;
    }

    // 等待处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    std::cout << std::endl;

    // ========================================
    // 性能测试
    // ========================================
    std::cout << "[Performance Test] Processing 100,000 events..." << std::endl;

    auto perf_stream = create_event_stream<int>();
    
    int count = 0;
    perf_stream->subscribe([&count](int) {
        count++;
    });

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100000; ++i) {
        perf_stream->next(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "  Processed " << count << " events in " 
              << duration.count() << " ms" << std::endl;
    std::cout << "  Throughput: " << (count * 1000.0 / duration.count()) 
              << " events/second" << std::endl;

    // 清理
    kernel.shutdown();

    std::cout << std::endl;
    std::cout << "=== Example completed successfully ===" << std::endl;
    return 0;
}
