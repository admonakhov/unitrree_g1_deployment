#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <yaml-cpp/yaml.h>

class CmdVelBridge
{
public:
    static CmdVelBridge& instance();

    CmdVelBridge(const CmdVelBridge&) = delete;
    CmdVelBridge& operator=(const CmdVelBridge&) = delete;
    ~CmdVelBridge();

    void start(const std::string& node_name = "g1_29dof_cmd_vel_bridge",
               const std::string& topic_name = "/cmd_vel");
    void stop();

    std::array<float, 3> command(const YAML::Node& ranges,
                                 double timeout_sec = 0.5) const;

private:
    struct Impl;

    CmdVelBridge();

    static int64_t steady_now_ns();

    std::atomic<float> lin_x_{0.0f};
    std::atomic<float> lin_y_{0.0f};
    std::atomic<float> ang_z_{0.0f};
    std::atomic<int64_t> last_msg_ns_{0};
    std::atomic<bool> started_{false};

    mutable std::mutex ros_mutex_;
    std::unique_ptr<Impl> impl_;
};
