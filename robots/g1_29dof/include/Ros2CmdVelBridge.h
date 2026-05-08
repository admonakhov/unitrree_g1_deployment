#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#ifdef UNITREE_HAVE_ROS2_CMD_VEL
#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#endif

class Ros2CmdVelBridge
{
public:
    struct Sample
    {
        std::array<float, 3> command {0.0f, 0.0f, 0.0f};
        bool recent = false;
        bool active = false;
    };

    static Ros2CmdVelBridge& instance()
    {
        static Ros2CmdVelBridge bridge;
        return bridge;
    }

    void configure(const YAML::Node& cfg)
    {
        enabled_ = cfg && cfg["enabled"] ? cfg["enabled"].as<bool>() : true;
        topic_ = cfg && cfg["topic"] ? cfg["topic"].as<std::string>() : "/cmd_vel";
        timeout_sec_ = cfg && cfg["timeout_sec"] ? cfg["timeout_sec"].as<float>() : 0.5f;
        node_name_ = cfg && cfg["node_name"] ? cfg["node_name"].as<std::string>() : "g1_cmd_vel_bridge";
    }

    void start()
    {
        if (!enabled_ || running_) {
            return;
        }

#ifdef UNITREE_HAVE_ROS2_CMD_VEL
        if (!rclcpp::ok()) {
            int argc = 0;
            char** argv = nullptr;
            rclcpp::init(argc, argv);
            owns_ros_context_ = true;
        }

        node_ = std::make_shared<rclcpp::Node>(node_name_);
        executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
        subscription_ = node_->create_subscription<geometry_msgs::msg::Twist>(
            topic_,
            rclcpp::QoS(10),
            [this](const geometry_msgs::msg::Twist::SharedPtr msg)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                command_[0] = static_cast<float>(msg->linear.x);
                command_[1] = static_cast<float>(msg->linear.y);
                command_[2] = static_cast<float>(msg->angular.z);
                last_update_ = std::chrono::steady_clock::now();
                has_command_ = true;
            }
        );

        executor_->add_node(node_);
        running_ = true;
        spin_thread_ = std::thread([this]() {
            spdlog::info("ROS2 cmd_vel bridge: listening on '{}'", topic_);
            executor_->spin();
        });
#else
        running_ = true;
        spdlog::warn("ROS2 cmd_vel bridge is enabled in config, but ROS2 dependencies were not found at build time.");
#endif
    }

    void stop()
    {
        if (!running_) {
            return;
        }

#ifdef UNITREE_HAVE_ROS2_CMD_VEL
        if (executor_) {
            executor_->cancel();
        }
        if (spin_thread_.joinable()) {
            spin_thread_.join();
        }
        if (executor_ && node_) {
            executor_->remove_node(node_);
        }
        subscription_.reset();
        executor_.reset();
        node_.reset();
        if (owns_ros_context_ && rclcpp::ok()) {
            rclcpp::shutdown();
        }
        owns_ros_context_ = false;
#endif

        running_ = false;
    }

    Sample sample() const
    {
        Sample sample;
        sample.active = enabled_;

        std::lock_guard<std::mutex> lock(mutex_);
        sample.command = command_;
        if (!has_command_) {
            return sample;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto age = std::chrono::duration_cast<std::chrono::duration<float>>(now - last_update_).count();
        sample.recent = age <= timeout_sec_;
        return sample;
    }

    ~Ros2CmdVelBridge()
    {
        stop();
    }

private:
    Ros2CmdVelBridge() = default;
    Ros2CmdVelBridge(const Ros2CmdVelBridge&) = delete;
    Ros2CmdVelBridge& operator=(const Ros2CmdVelBridge&) = delete;

    bool enabled_ = true;
    bool has_command_ = false;
    std::atomic<bool> running_ = false;
    float timeout_sec_ = 0.5f;
    std::string topic_ = "/cmd_vel";
    std::string node_name_ = "g1_cmd_vel_bridge";

    mutable std::mutex mutex_;
    std::array<float, 3> command_ {0.0f, 0.0f, 0.0f};
    std::chrono::steady_clock::time_point last_update_ = std::chrono::steady_clock::now();

#ifdef UNITREE_HAVE_ROS2_CMD_VEL
    bool owns_ros_context_ = false;
    std::shared_ptr<rclcpp::Node> node_;
    std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr subscription_;
    std::thread spin_thread_;
#endif
};
