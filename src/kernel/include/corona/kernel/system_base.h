#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "i_system.h"
#include "i_system_context.h"

namespace Corona::Kernel {

// 系统基类，提供线程管理和基础功能
class SystemBase : public ISystem {
   public:
    SystemBase()
        : state_(SystemState::idle),
          should_run_(false),
          is_paused_(false),
          context_(nullptr),
          target_fps_(60),
          frame_number_(0),
          last_delta_time_(0.0f) {}

    virtual ~SystemBase() {
        if (thread_.joinable()) {
            stop();
        }
    }

    SystemState get_state() const override {
        return state_.load(std::memory_order_acquire);
    }

    int get_target_fps() const override {
        return target_fps_;
    }

    void set_target_fps(int fps) {
        target_fps_ = fps;
    }

    void start() override {
        if (state_ != SystemState::idle && state_ != SystemState::stopped) {
            return;
        }

        should_run_.store(true, std::memory_order_release);
        is_paused_.store(false, std::memory_order_release);
        state_.store(SystemState::running, std::memory_order_release);

        thread_ = std::thread(&SystemBase::thread_loop, this);
    }

    void pause() override {
        if (state_ != SystemState::running) {
            return;
        }

        is_paused_.store(true, std::memory_order_release);
        state_.store(SystemState::paused, std::memory_order_release);
    }

    void resume() override {
        if (state_ != SystemState::paused) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(pause_mutex_);
            is_paused_.store(false, std::memory_order_release);
            state_.store(SystemState::running, std::memory_order_release);
        }
        pause_cv_.notify_one();
    }

    void stop() override {
        if (state_ == SystemState::stopped || state_ == SystemState::idle) {
            return;
        }

        state_.store(SystemState::stopping, std::memory_order_release);
        should_run_.store(false, std::memory_order_release);

        // 唤醒可能在等待的线程
        {
            std::lock_guard<std::mutex> lock(pause_mutex_);
            is_paused_.store(false, std::memory_order_release);
        }
        pause_cv_.notify_one();

        if (thread_.joinable()) {
            thread_.join();
        }

        state_.store(SystemState::stopped, std::memory_order_release);
    }

   protected:
    // 获取系统上下文
    ISystemContext* context() {
        return context_;
    }
    const ISystemContext* context() const {
        return context_;
    }

    // 获取当前帧号
    uint64_t frame_number() const {
        return frame_number_;
    }

    // 获取上一帧的 delta time（秒）
    float delta_time() const {
        return last_delta_time_;
    }

    // 线程循环（可由子类覆盖以自定义行为）
    virtual void thread_loop() {
        auto last_time = std::chrono::high_resolution_clock::now();

        // 计算帧时间间隔
        std::chrono::microseconds frame_duration(0);
        if (target_fps_ > 0) {
            frame_duration = std::chrono::microseconds(1000000 / target_fps_);
        }

        while (should_run_.load(std::memory_order_acquire)) {
            auto frame_start = std::chrono::high_resolution_clock::now();

            // 检查暂停状态
            if (is_paused_.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(pause_mutex_);
                pause_cv_.wait(lock, [this] {
                    return !is_paused_.load(std::memory_order_acquire) ||
                           !should_run_.load(std::memory_order_acquire);
                });
                last_time = std::chrono::high_resolution_clock::now();
                continue;
            }

            // 计算 delta_time
            auto current_time = std::chrono::high_resolution_clock::now();
            last_delta_time_ = std::chrono::duration<float>(current_time - last_time).count();
            last_time = current_time;

            // 调用子类的更新逻辑（无参数）
            update();

            frame_number_++;

            // 帧率限制
            if (target_fps_ > 0) {
                auto frame_end = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start);

                if (elapsed < frame_duration) {
                    std::this_thread::sleep_for(frame_duration - elapsed);
                }
            }
        }
    }

   private:
    friend class SystemManager;  // 允许 SystemManager 设置 context

    void set_context(ISystemContext* context) {
        context_ = context;
    }

    std::atomic<SystemState> state_;
    std::atomic<bool> should_run_;
    std::atomic<bool> is_paused_;
    std::thread thread_;
    std::mutex pause_mutex_;
    std::condition_variable pause_cv_;

    ISystemContext* context_;
    int target_fps_;
    uint64_t frame_number_;
    float last_delta_time_;
};

}  // namespace Corona::Kernel
