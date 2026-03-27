#pragma once

#include <mutex>

#include "Types.h"
#include "param.h"
#include "FSM/BaseState.h"
#include "isaaclab/devices/keyboard/keyboard.h"
#include "isaaclab/devices/gamepad/gamepad.h"
#include "unitree_joystick_dsl.hpp"
#include <spdlog/spdlog.h>

class FSMState : public BaseState
{
public:
    FSMState(int state, std::string state_string) 
    : BaseState(state, state_string) 
    {
        spdlog::info("Initializing State_{} ...", state_string);

        auto transitions = param::config["FSM"][state_string]["transitions"];

        if(transitions)
        {
            auto transition_map = transitions.as<std::map<std::string, std::string>>();

            for(auto it = transition_map.begin(); it != transition_map.end(); ++it)
            {
                std::string target_fsm = it->first;
                if(!FSMStringMap.right.count(target_fsm))
                {
                    spdlog::warn("FSM State_'{}' not found in FSMStringMap!", target_fsm);
                    continue;
                }

                int fsm_id = FSMStringMap.right.at(target_fsm);

                std::string condition = it->second;
                unitree::common::dsl::Parser p(condition);
                auto ast = p.Parse();
                auto func = unitree::common::dsl::Compile(*ast);
                auto wrapped = [func, condition, fsm_id, state_string]()->bool{
                    bool res = func(FSMState::active_joystick());
                    if (res) {
                        spdlog::info("FSM: Condition '{}' triggered in State_{} -> State_{}",
                                     condition,
                                     state_string,
                                     FSMStringMap.left.at(fsm_id));
                    }
                    return res;
                };
                registered_checks.emplace_back(
                    std::make_pair(
                        wrapped,
                        fsm_id
                    )
                );
            }
        }

        // register for all states
        registered_checks.emplace_back(
            std::make_pair(
                []()->bool{ return lowstate->isTimeout(); },
                FSMStringMap.right.at("Passive")
            )
        );
    }

    static const unitree::common::UnitreeJoystick & active_joystick()
    {
        if (gamepad && gamepad->connected()) {
            return gamepad->joystick();
        }
        return lowstate->joystick;
    }

    void pre_run()
    {
        lowstate->update();
        if (gamepad) {
            gamepad->update();
            if (gamepad->connected()) {
                std::lock_guard<std::mutex> lock(lowstate->mutex_);
                gamepad->apply_to(lowstate->joystick);
            }
        }
        if(keyboard) keyboard->update();

    }

    void post_run()
    {
        lowcmd->unlockAndPublish();
    }

    static std::unique_ptr<LowCmd_t> lowcmd;
    static std::shared_ptr<LowState_t> lowstate;
    static std::shared_ptr<Keyboard> keyboard;
    static std::shared_ptr<isaaclab::Gamepad> gamepad;
};
