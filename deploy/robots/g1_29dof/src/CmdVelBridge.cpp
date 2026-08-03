#include "CmdVelBridge.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct CmdVelBridge::Impl
{
    int socket_fd{-1};
    pid_t child_pid{-1};
    std::thread recv_thread;
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

CmdVelBridge::~CmdVelBridge()
{
    stop();
}

int64_t CmdVelBridge::steady_now_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void CmdVelBridge::start(const std::string& node_name, const std::string& topic_name)
{
    (void)node_name;

    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::lock_guard<std::mutex> lock(ros_mutex_);

    impl_->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (impl_->socket_fd < 0) {
        started_.store(false, std::memory_order_release);
        std::cerr << "ROS2 /cmd_vel bridge disabled: failed to create UDP socket: "
                  << std::strerror(errno) << std::endl;
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(impl_->socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "ROS2 /cmd_vel bridge disabled: failed to bind UDP socket: "
                  << std::strerror(errno) << std::endl;
        close(impl_->socket_fd);
        impl_->socket_fd = -1;
        started_.store(false, std::memory_order_release);
        return;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(impl_->socket_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
        std::cerr << "ROS2 /cmd_vel bridge disabled: failed to read UDP port: "
                  << std::strerror(errno) << std::endl;
        close(impl_->socket_fd);
        impl_->socket_fd = -1;
        started_.store(false, std::memory_order_release);
        return;
    }
    const int udp_port = ntohs(addr.sin_port);

    std::filesystem::path bridge_path = "./g1_cmd_vel_bridge_node";
    char exe_path[4096]{};
    const ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_len > 0) {
        exe_path[exe_len] = '\0';
        bridge_path = std::filesystem::path(exe_path).parent_path() / "g1_cmd_vel_bridge_node";
    }

    impl_->child_pid = fork();
    if (impl_->child_pid < 0) {
        std::cerr << "ROS2 /cmd_vel bridge disabled: failed to fork bridge process: "
                  << std::strerror(errno) << std::endl;
        close(impl_->socket_fd);
        impl_->socket_fd = -1;
        started_.store(false, std::memory_order_release);
        return;
    }

    if (impl_->child_pid == 0) {
        close(impl_->socket_fd);
        const std::string port_arg = std::to_string(udp_port);
        execl(bridge_path.c_str(), bridge_path.c_str(), topic_name.c_str(), port_arg.c_str(), nullptr);
        std::cerr << "ROS2 /cmd_vel bridge child exec failed for " << bridge_path
                  << ": " << std::strerror(errno) << std::endl;
        _exit(127);
    }

    impl_->recv_thread = std::thread([this]() {
        std::array<char, 128> buffer{};
        while (started_.load(std::memory_order_acquire)) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(impl_->socket_fd, &read_fds);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;

            const int ready = select(impl_->socket_fd + 1, &read_fds, nullptr, nullptr, &timeout);
            if (ready <= 0 || !FD_ISSET(impl_->socket_fd, &read_fds)) {
                continue;
            }

            const ssize_t n = recv(impl_->socket_fd, buffer.data(), buffer.size() - 1, 0);
            if (n <= 0) {
                continue;
            }
            buffer[static_cast<size_t>(n)] = '\0';

            float lin_x = 0.0f;
            float lin_y = 0.0f;
            float ang_z = 0.0f;
            if (std::sscanf(buffer.data(), "%f %f %f", &lin_x, &lin_y, &ang_z) == 3) {
                lin_x_.store(lin_x, std::memory_order_relaxed);
                lin_y_.store(lin_y, std::memory_order_relaxed);
                ang_z_.store(ang_z, std::memory_order_relaxed);
                last_msg_ns_.store(steady_now_ns(), std::memory_order_release);
            }
        }
    });

    std::cout << "ROS2 cmd_vel bridge started: launched " << bridge_path
              << " subscribing to " << topic_name
              << " (isolated from Unitree SDK2 DDS in a child process)" << std::endl;
}

void CmdVelBridge::stop()
{
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return;
    }

    std::lock_guard<std::mutex> lock(ros_mutex_);

    if (impl_->recv_thread.joinable()) {
        impl_->recv_thread.join();
    }
    if (impl_->socket_fd >= 0) {
        close(impl_->socket_fd);
        impl_->socket_fd = -1;
    }
    if (impl_->child_pid > 0) {
        kill(impl_->child_pid, SIGTERM);
        waitpid(impl_->child_pid, nullptr, 0);
        impl_->child_pid = -1;
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
