#include "FSM/State_RLBase.h"
#include "CmdVelBridge.h"
#include "unitree_articulation.h"
#include "isaaclab/envs/mdp/observations/observations.h"
#include "isaaclab/envs/mdp/actions/joint_actions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace isaaclab
{
namespace
{
float clamp_range(float value, const YAML::Node& ranges, const char* key)
{
    if (!ranges || !ranges[key] || ranges[key].IsNull()) {
        return value;
    }
    const float min_value = ranges[key][0].as<float>();
    const float max_value = ranges[key][1].as<float>();
    return std::min(std::max(value, min_value), max_value);
}

float scale_axis(float value, const YAML::Node& ranges, const char* key)
{
    constexpr float deadzone = 0.08f;
    if (std::fabs(value) < deadzone) {
        return 0.0f;
    }
    if (!ranges || !ranges[key] || ranges[key].IsNull()) {
        return value;
    }
    const float min_value = ranges[key][0].as<float>();
    const float max_value = ranges[key][1].as<float>();
    return value > 0.0f ? value * max_value : value * std::fabs(min_value);
}

std::array<float, 3> gamepad_velocity_command(const YAML::Node& ranges)
{
    auto& joystick = FSMState::lowstate->joystick;
    return {
        scale_axis(joystick.ly(), ranges, "lin_vel_x"),
        scale_axis(joystick.lx(), ranges, "lin_vel_y"),
        scale_axis(-joystick.rx(), ranges, "ang_vel_z")
    };
}
}

// keyboard velocity commands example
// change "velocity_commands" observation name in policy deploy.yaml to "keyboard_velocity_commands"
REGISTER_OBSERVATION(keyboard_velocity_commands)
{
    std::string key = FSMState::keyboard->key();
    static auto cfg = env->cfg["commands"]["base_velocity"]["ranges"];

    static std::unordered_map<std::string, std::vector<float>> key_commands = {
        {"w", {1.0f, 0.0f, 0.0f}},
        {"s", {-1.0f, 0.0f, 0.0f}},
        {"a", {0.0f, 1.0f, 0.0f}},
        {"d", {0.0f, -1.0f, 0.0f}},
        {"q", {0.0f, 0.0f, 1.0f}},
        {"e", {0.0f, 0.0f, -1.0f}}
    };
    std::vector<float> cmd = {0.0f, 0.0f, 0.0f};
    if (key_commands.find(key) != key_commands.end())
    {
        // TODO: smooth and limit the velocity commands
        cmd = key_commands[key];
    }
    return cmd;
}

REGISTER_OBSERVATION(ros2_velocity_commands)
{
    static auto cfg = env->cfg["commands"]["base_velocity"]["ranges"];
    const auto cmd = CmdVelBridge::instance().command(cfg);
    return std::vector<float>{cmd[0], cmd[1], cmd[2]};
}

REGISTER_OBSERVATION(mixed_velocity_commands)
{
    static auto cfg = env->cfg["commands"]["base_velocity"]["ranges"];
    const auto ros_cmd = CmdVelBridge::instance().command(cfg);
    const auto gamepad_cmd = gamepad_velocity_command(cfg);
    return std::vector<float>{
        clamp_range(ros_cmd[0] + gamepad_cmd[0], cfg, "lin_vel_x"),
        clamp_range(ros_cmd[1] + gamepad_cmd[1], cfg, "lin_vel_y"),
        clamp_range(ros_cmd[2] + gamepad_cmd[2], cfg, "ang_vel_z")
    };
}

}

State_RLBase::State_RLBase(int state_mode, std::string state_string)
: FSMState(state_mode, state_string) 
{
    auto cfg = param::config["FSM"][state_string];
    auto policy_dir = param::parser_policy_dir(cfg["policy_dir"].as<std::string>());

    env = std::make_unique<isaaclab::ManagerBasedRLEnv>(
        YAML::LoadFile(policy_dir / "params" / "deploy.yaml"),
        std::make_shared<unitree::BaseArticulation<LowState_t::SharedPtr>>(FSMState::lowstate)
    );
    env->alg = std::make_unique<isaaclab::OrtRunner>(policy_dir / "exported" / "policy.onnx");

    this->registered_checks.emplace_back(
        std::make_pair(
            [&]()->bool{ return isaaclab::mdp::bad_orientation(env.get(), 1.0); },
            FSMStringMap.right.at("Passive")
        )
    );
}

void State_RLBase::run()
{
    auto action = env->action_manager->processed_actions();
    for(int i(0); i < env->robot->data.joint_ids_map.size(); i++) {
        lowcmd->msg_.motor_cmd()[env->robot->data.joint_ids_map[i]].q() = action[i];
    }
}