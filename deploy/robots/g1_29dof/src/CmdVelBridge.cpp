#include "CmdVelBridge.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

CmdVelBridge& CmdVelBridge::instance()
{
    static CmdVelBridge bridge;
    return bridge;
}

int64_t CmdVelBridge::steady_now_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void CmdVelBridge::start(const std::string& node_name, const std::string& topic_name)
{
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::lock_guard<std::mutex> lock(ros_mutex_);

    if (!rclcpp::ok()) {
        int argc = 0;
        char** argv = nullptr;
        rclcpp::init(argc, argv);
    }

    node_ = std::make_shared<rclcpp::Node>(node_name);
    sub_ = node_->create_subscription<geometry_msgs::msg::Twist>(
        topic_name,
        rclcpp::SensorDataQoS(),
        [this](const geometry_msgs::msg::Twist::SharedPtr msg) {
            lin_x_.store(static_cast<float>(msg->linear.x), std::memory_order_relaxed);
            lin_y_.store(static_cast<float>(msg->linear.y), std::memory_order_relaxed);
            ang_z_.store(static_cast<float>(msg->angular.z), std::memory_order_relaxed);
            last_msg_ns_.store(steady_now_ns(), std::memory_order_release);
        });

    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
    spin_thread_ = std::thread([this]() {
        try {
            executor_->spin();
        } catch (const std::exception& e) {
            std::cerr << "ROS2 /cmd_vel bridge executor stopped: " << e.what() << std::endl;
        }
    });

    std::cout << "ROS2 cmd_vel bridge started: subscribing to " << topic_name << std::endl;
}

void CmdVelBridge::stop()
{
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return;
    }

    std::lock_guard<std::mutex> lock(ros_mutex_);

    if (executor_) {
        executor_->cancel();
    }
    if (spin_thread_.joinable()) {
        spin_thread_.join();
    }
    if (executor_ && node_) {
        executor_->remove_node(node_);
    }
    sub_.reset();
    node_.reset();
    executor_.reset();

    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
}

std::array<float, 3> CmdVelBridge::command(const YAML::Node& ranges,
                                           double timeout_sec) const
{
    const int64_t last_msg_ns = last_msg_ns_.load(std::memory_order_acquire);
    const int64_t timeout_ns = static_cast<int64_t>(timeout_sec * 1e9);
    if (last_msg_ns == 0 || steady_now_ns() - last_msg_ns > timeout_ns) {
        return {0.0f, 0.0f, 0.0f};
    }

    auto clamp_range = [&ranges](float value, const char* key) -> float {
        if (!ranges || !ranges[key] || ranges[key].IsNull()) {
            return value;
        }
        const float min_value = ranges[key][0].as<float>();
        const float max_value = ranges[key][1].as<float>();
        return std::clamp(value, min_value, max_value);
    };

    return {
        clamp_range(lin_x_.load(std::memory_order_relaxed), "lin_vel_x"),
        clamp_range(lin_y_.load(std::memory_order_relaxed), "lin_vel_y"),
        clamp_range(ang_z_.load(std::memory_order_relaxed), "ang_vel_z")
    };
}
