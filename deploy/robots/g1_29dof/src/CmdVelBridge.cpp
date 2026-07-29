#include "CmdVelBridge.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>

struct CmdVelBridge::Impl
{
    rclcpp::Node::SharedPtr node;
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr sub;
    std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
    std::thread spin_thread;
};

CmdVelBridge& CmdVelBridge::instance()
{
    static CmdVelBridge bridge;
    return bridge;
}

CmdVelBridge::CmdVelBridge()
: impl_(std::make_unique<Impl>())
{
}

CmdVelBridge::~CmdVelBridge() = default;

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

    impl_->node = std::make_shared<rclcpp::Node>(node_name);
    impl_->sub = impl_->node->create_subscription<geometry_msgs::msg::Twist>(
        topic_name,
        rclcpp::SensorDataQoS(),
        [this](const geometry_msgs::msg::Twist::SharedPtr msg) {
            lin_x_.store(static_cast<float>(msg->linear.x), std::memory_order_relaxed);
            lin_y_.store(static_cast<float>(msg->linear.y), std::memory_order_relaxed);
            ang_z_.store(static_cast<float>(msg->angular.z), std::memory_order_relaxed);
            last_msg_ns_.store(steady_now_ns(), std::memory_order_release);
        });

    impl_->executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    impl_->executor->add_node(impl_->node);
    impl_->spin_thread = std::thread([this]() {
        try {
            impl_->executor->spin();
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

    if (impl_->executor) {
        impl_->executor->cancel();
    }
    if (impl_->spin_thread.joinable()) {
        impl_->spin_thread.join();
    }
    if (impl_->executor && impl_->node) {
        impl_->executor->remove_node(impl_->node);
    }
    impl_->sub.reset();
    impl_->node.reset();
    impl_->executor.reset();

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
