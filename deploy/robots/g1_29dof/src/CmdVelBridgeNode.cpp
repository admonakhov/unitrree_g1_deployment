#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>

int main(int argc, char** argv)
{
    const std::string topic_name = argc > 1 ? argv[1] : "/cmd_vel";
    const int udp_port = argc > 2 ? std::atoi(argv[2]) : 0;
    if (udp_port <= 0) {
        std::cerr << "g1_cmd_vel_bridge_node: missing UDP port argument" << std::endl;
        return 2;
    }

    const int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        std::cerr << "g1_cmd_vel_bridge_node: failed to create UDP socket: "
                  << std::strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(udp_port));

    if (connect(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "g1_cmd_vel_bridge_node: failed to connect UDP socket: "
                  << std::strerror(errno) << std::endl;
        close(socket_fd);
        return 1;
    }

    try {
        rclcpp::init(argc, argv);
        auto node = std::make_shared<rclcpp::Node>("g1_29dof_cmd_vel_bridge");
        auto sub = node->create_subscription<geometry_msgs::msg::Twist>(
            topic_name,
            rclcpp::SensorDataQoS(),
            [socket_fd](const geometry_msgs::msg::Twist::SharedPtr msg) {
                char buffer[128];
                const int n = std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "%.9g %.9g %.9g",
                    static_cast<float>(msg->linear.x),
                    static_cast<float>(msg->linear.y),
                    static_cast<float>(msg->angular.z));
                if (n > 0) {
                    send(socket_fd, buffer, static_cast<size_t>(n), 0);
                }
            });

        std::cout << "g1_cmd_vel_bridge_node: subscribing to " << topic_name
                  << " and forwarding commands to g1_ctrl" << std::endl;
        rclcpp::spin(node);
        rclcpp::shutdown();
    } catch (const std::exception& e) {
        std::cerr << "g1_cmd_vel_bridge_node: ROS2 bridge failed: " << e.what() << std::endl;
        close(socket_fd);
        return 1;
    }

    close(socket_fd);
    return 0;
}
