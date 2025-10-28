#pragma once
#include <string>
#include <string_view>

#include "corona/kernel/i_logger.h"
#include "corona/kernel/system_base.h"

namespace Corona::Kernel {

// 简单的测试系统
class DummySystem : public SystemBase {
   public:
    DummySystem() = default;
    ~DummySystem() override = default;

    std::string_view get_name() const override {
        return "DummySystem";
    }

    int get_priority() const override {
        return 50;  // 中等优先级
    }

    int get_target_fps() const override {
        return 10;  // 10 FPS，便于观察
    }

    bool initialize(ISystemContext* ctx) override {
        auto logger = ctx->logger();
        logger->info("DummySystem initializing...");
        return true;
    }

    void shutdown() override {
        if (context()) {
            context()->logger()->info("DummySystem shutting down...");
        }
    }

    void update() override {
        // 每10帧输出一次
        if (frame_number() % 10 == 0) {
            auto logger = context()->logger();
            logger->info("DummySystem update - Frame: " + std::to_string(frame_number()) +
                         ", DeltaTime: " + std::to_string(delta_time()));
        }
    }

    void sync() override {
        // 同步点
    }
};

}  // namespace Corona::Kernel
