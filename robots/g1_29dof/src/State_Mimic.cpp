#include "State_Mimic.h"
#include "unitree_articulation.h"
#include "isaaclab/envs/mdp/observations/observations.h"
#include "isaaclab/envs/mdp/actions/joint_actions.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <spdlog/spdlog.h>
#include <unitree/common/time/time_tool.hpp>
#include <unitree/robot/g1/audio/g1_audio_client.hpp>

static Eigen::Quaternionf init_quat;
std::shared_ptr<State_Mimic::MotionLoader_> State_Mimic::motion = nullptr;

namespace {

struct WavData {
    int32_t sample_rate = -1;
    int16_t num_channels = 0;
    int16_t bits_per_sample = 0;
    std::vector<uint8_t> pcm;
    bool ok = false;
};

static uint32_t read_u32_le(std::ifstream & in)
{
    uint8_t b[4]{};
    in.read(reinterpret_cast<char*>(b), sizeof(b));
    return static_cast<uint32_t>(b[0])
         | (static_cast<uint32_t>(b[1]) << 8)
         | (static_cast<uint32_t>(b[2]) << 16)
         | (static_cast<uint32_t>(b[3]) << 24);
}

static uint16_t read_u16_le(std::ifstream & in)
{
    uint8_t b[2]{};
    in.read(reinterpret_cast<char*>(b), sizeof(b));
    return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
}

static WavData read_wav_file(const std::filesystem::path & path)
{
    WavData out;
    spdlog::info("Loading audio file: {}", path.string());
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        spdlog::warn("Audio file not found: {}", path.string());
        return out;
    }

    char riff[4]{};
    in.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        spdlog::warn("Audio file is not RIFF WAV: {}", path.string());
        return out;
    }
    (void)read_u32_le(in); // file size
    char wave[4]{};
    in.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        spdlog::warn("Audio file is not WAVE: {}", path.string());
        return out;
    }

    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0;
    while (in && (!have_fmt || !have_data)) {
        char chunk_id[4]{};
        in.read(chunk_id, 4);
        if (!in) {
            break;
        }
        uint32_t chunk_size = read_u32_le(in);
        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            audio_format = read_u16_le(in);
            out.num_channels = static_cast<int16_t>(read_u16_le(in));
            out.sample_rate = static_cast<int32_t>(read_u32_le(in));
            (void)read_u32_le(in); // byte rate
            (void)read_u16_le(in); // block align
            out.bits_per_sample = static_cast<int16_t>(read_u16_le(in));
            if (chunk_size > 16) {
                in.seekg(chunk_size - 16, std::ios::cur);
            }
            have_fmt = true;
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            out.pcm.resize(chunk_size);
            in.read(reinterpret_cast<char*>(out.pcm.data()), chunk_size);
            have_data = true;
        } else {
            in.seekg(chunk_size, std::ios::cur);
        }
    }

    if (!have_fmt || !have_data) {
        spdlog::warn("Audio file missing fmt/data chunks: {}", path.string());
        return out;
    }
    if (audio_format != 1) {
        spdlog::warn("Unsupported WAV format (need PCM): {}", path.string());
        return out;
    }
    spdlog::info("Audio WAV: {} Hz, {} ch, {} bit, {} bytes",
                 out.sample_rate,
                 static_cast<int>(out.num_channels),
                 static_cast<int>(out.bits_per_sample),
                 out.pcm.size());
    out.ok = true;
    return out;
}

} // namespace

Eigen::Quaternionf torso_quat_w(isaaclab::ManagerBasedRLEnv* env) {
    using G1Type = unitree::BaseArticulation<LowState_t::SharedPtr>;
    G1Type* robot = dynamic_cast<G1Type*>(env->robot.get());

    auto root_quat = env->robot->data.root_quat_w;
    auto & motors = robot->lowstate->msg_.motor_state();

    Eigen::Quaternionf torso_quat = root_quat \
        * Eigen::AngleAxisf(motors[12].q(), Eigen::Vector3f::UnitZ()) \
        * Eigen::AngleAxisf(motors[13].q(), Eigen::Vector3f::UnitX()) \
        * Eigen::AngleAxisf(motors[14].q(), Eigen::Vector3f::UnitY()) \
    ;
    return torso_quat;
};

Eigen::Quaternionf anchor_quat_w(std::shared_ptr<State_Mimic::MotionLoader_> loader)
{
    const auto root_quat = loader->root_quaternion();
    const auto joint_pos = loader->joint_pos();
    Eigen::Quaternionf torso_quat = root_quat \
        * Eigen::AngleAxisf(joint_pos[12], Eigen::Vector3f::UnitZ()) \
        * Eigen::AngleAxisf(joint_pos[13], Eigen::Vector3f::UnitX()) \
        * Eigen::AngleAxisf(joint_pos[14], Eigen::Vector3f::UnitY()) \
    ;
    return torso_quat;
}


namespace isaaclab
{
namespace mdp
{

REGISTER_OBSERVATION(motion_joint_pos)
{
    auto & robot = env->robot;
    auto & loader = State_Mimic::motion;
    auto & ids = robot->data.joint_ids_map;

    auto data_dfs = loader->joint_pos();
    Eigen::VectorXf data_bfs = Eigen::VectorXf::Zero(data_dfs.size());
    for(int i = 0; i < data_dfs.size(); ++i) {
        data_bfs(i) = data_dfs[ids[i]];
    }
    return std::vector<float>(data_bfs.data(), data_bfs.data() + data_bfs.size());
}

REGISTER_OBSERVATION(motion_joint_vel)
{
    auto & robot = env->robot;
    auto & loader = State_Mimic::motion;
    auto & ids = robot->data.joint_ids_map;

    auto data_dfs = loader->joint_vel();
    Eigen::VectorXf data_bfs = Eigen::VectorXf::Zero(data_dfs.size());
    for(int i = 0; i < data_dfs.size(); ++i) {
        data_bfs(i) = data_dfs[ids[i]];
    }
    return std::vector<float>(data_bfs.data(), data_bfs.data() + data_bfs.size());
}

REGISTER_OBSERVATION(motion_command)
{
    auto & robot = env->robot;
    auto & loader = State_Mimic::motion;
    auto & ids = robot->data.joint_ids_map;

    auto pos_dfs = loader->joint_pos();
    Eigen::VectorXf pos_bfs = Eigen::VectorXf::Zero(pos_dfs.size());
    for(int i = 0; i < pos_dfs.size(); ++i) {
        pos_bfs(i) = pos_dfs[ids[i]];
    }
    auto vel_dfs = loader->joint_vel();
    Eigen::VectorXf vel_bfs = Eigen::VectorXf::Zero(vel_dfs.size());
    for(int i = 0; i < vel_dfs.size(); ++i) {
        vel_bfs(i) = vel_dfs[ids[i]];
    }
    std::vector<float> data;
    data.insert(data.end(), pos_bfs.data(), pos_bfs.data() + pos_bfs.size());
    data.insert(data.end(), vel_bfs.data(), vel_bfs.data() + vel_bfs.size());
    return data;
}

REGISTER_OBSERVATION(motion_anchor_ori_b)
{
    // auto & robot = env->robot;
    auto real_quat_w = torso_quat_w(env);
    auto ref_quat_w = anchor_quat_w(State_Mimic::motion);

    auto rot_ = (init_quat * ref_quat_w).conjugate() * real_quat_w;
    auto rot = rot_.toRotationMatrix().transpose();

    Eigen::Matrix<float, 6, 1> data;
    data << rot(0, 0), rot(0, 1), rot(1, 0), rot(1, 1), rot(2, 0), rot(2, 1);
    return std::vector<float>(data.data(), data.data() + data.size());
}

}
}


State_Mimic::State_Mimic(int state_mode, std::string state_string)
: FSMState(state_mode, state_string) 
{
    auto cfg = param::config["FSM"][state_string];
    auto policy_dir = param::parser_policy_dir(cfg["policy_dir"].as<std::string>());

    auto articulation = std::make_shared<unitree::BaseArticulation<LowState_t::SharedPtr>>(FSMState::lowstate);

    std::filesystem::path motion_file = cfg["motion_file"].as<std::string>();
    if(!motion_file.is_absolute()) {
        motion_file = param::proj_dir / motion_file;
    }

    // Motion
    motion_ = std::make_shared<MotionLoader_>(motion_file.string(), cfg["fps"].as<float>());
    spdlog::info("Loaded motion file '{}' with duration {:.2f}s", motion_file.stem().string(), motion_->duration);
    motion = motion_;
    if(cfg["time_start"]) {
        float time_start = cfg["time_start"].as<float>();
        time_range_[0] = std::clamp(time_start, 0.0f, motion_->duration);
    } else {
        time_range_[0] = 0.0f;
    }
    if(cfg["time_end"]) {
        float time_end = cfg["time_end"].as<float>();
        time_range_[1] = std::clamp(time_end, 0.0f, motion_->duration);
    } else {
        time_range_[1] = motion_->duration;
    }

    if (cfg["audio_file"]) {
        std::filesystem::path audio_path = cfg["audio_file"].as<std::string>();
        if (!audio_path.is_absolute()) {
            audio_path = param::proj_dir / audio_path;
        }
        audio_file_ = audio_path;
        spdlog::info("Mimic audio_file set: {}", audio_path.string());
    } else {
        spdlog::info("Mimic audio_file not set");
    }
    if (cfg["volume"]) {
        int volume = cfg["volume"].as<int>();
        volume = std::clamp(volume, 0, 100);
        audio_volume_ = static_cast<uint8_t>(volume);
    } else {
        audio_volume_ = 100;
    }

    env = std::make_unique<isaaclab::ManagerBasedRLEnv>(
        YAML::LoadFile(policy_dir / "params" / "deploy.yaml"),
        articulation
    );
    env->alg = std::make_unique<isaaclab::OrtRunner>(policy_dir / "exported" / "policy.onnx");

    const auto & joy = FSMState::lowstate->joystick;
    this->registered_checks.emplace_back(
        std::make_pair(
            [&]()->bool{ return (env->episode_length * env->step_dt) > time_range_[1]; }, // time out
            FSMStringMap.right.at("Velocity")
        )
    );
    this->registered_checks.emplace_back(
        std::make_pair(
            [&]()->bool{ return isaaclab::mdp::bad_orientation(env.get(), 1.0); }, // bad orientation
            FSMStringMap.right.at("Passive")
        )
    );
}

void State_Mimic::enter()
{
    spdlog::info("Enter State_Mimic");
    // set gain
    for (int i = 0; i < env->robot->data.joint_stiffness.size(); ++i)
    {
        lowcmd->msg_.motor_cmd()[i].kp() = env->robot->data.joint_stiffness[i];
        lowcmd->msg_.motor_cmd()[i].kd() = env->robot->data.joint_damping[i];
        lowcmd->msg_.motor_cmd()[i].dq() = 0;
        lowcmd->msg_.motor_cmd()[i].tau() = 0;
    }

    motion = motion_; // set for specific motion
    env->reset();
    start_audio_if_configured();
    // Start policy thread
    policy_thread_running = true;
    policy_thread = std::thread([this]{
        using clock = std::chrono::high_resolution_clock;
        const std::chrono::duration<double> desiredDuration(env->step_dt);
        const auto dt = std::chrono::duration_cast<clock::duration>(desiredDuration);

        // Initialize timing
        const auto start = clock::now();
        auto sleepTill = start + dt;

        auto ref_yaw = isaaclab::yawQuaternion(motion->root_quaternion()).toRotationMatrix();
        auto robot_yaw = isaaclab::yawQuaternion(torso_quat_w(env.get())).toRotationMatrix();
        init_quat = robot_yaw * ref_yaw.transpose();
        motion->reset(env->robot->data, time_range_[0]);
        env->reset();

        while (policy_thread_running)
        {
            env->robot->update();
            motion->update(env->episode_length * env->step_dt + time_range_[0]);
            env->step();

            // Sleep
            std::this_thread::sleep_until(sleepTill);
            sleepTill += dt;
        }
    });
}


void State_Mimic::run()
{
    auto action = env->action_manager->processed_actions();
    for(int i(0); i < env->robot->data.joint_ids_map.size(); i++) {
        lowcmd->msg_.motor_cmd()[env->robot->data.joint_ids_map[i]].q() = action[i];
    }
}

void State_Mimic::start_audio_if_configured()
{
    if (!audio_client_) {
        spdlog::info("Initializing AudioClient");
        audio_client_ = std::make_unique<unitree::robot::g1::AudioClient>();
        audio_client_->Init();
        audio_client_->SetTimeout(10.0f);
    }
    audio_client_->SetVolume(audio_volume_);

    if (!audio_file_) {
        spdlog::info("No audio_file configured for this Mimic action");
        return;
    }

    const auto wav = read_wav_file(*audio_file_);
    if (!wav.ok) {
        spdlog::warn("Audio load failed, continuing without audio");
        return;
    }
    if (wav.sample_rate != 16000 || wav.num_channels != 1 || wav.bits_per_sample != 16) {
        spdlog::warn("Audio file format error (need 16kHz/mono/16-bit): {}", audio_file_->string());
        return;
    }

    spdlog::info("Starting audio playback for Mimic action");
    audio_thread_running = true;
    const std::string stream_id = std::to_string(unitree::common::GetCurrentTimeMillisecond());
    audio_stream_id_ = stream_id;
    const auto pcm = std::make_shared<std::vector<uint8_t>>(wav.pcm);

    audio_thread_ = std::thread([this, pcm, stream_id, wav]{
        constexpr size_t kChunkSize = 96000; // ~3 seconds at 16kHz mono 16-bit
        const size_t bytes_per_sec =
            static_cast<size_t>(wav.sample_rate) *
            static_cast<size_t>(wav.num_channels) *
            static_cast<size_t>(wav.bits_per_sample / 8);
        size_t offset = 0;
        while (offset < pcm->size() && audio_thread_running.load()) {
            size_t remaining = pcm->size() - offset;
            size_t current_size = std::min(kChunkSize, remaining);
            std::vector<uint8_t> chunk(pcm->begin() + offset, pcm->begin() + offset + current_size);
            audio_client_->PlayStream("mimic", stream_id, chunk);
            if (bytes_per_sec > 0) {
                const float chunk_sec = static_cast<float>(current_size) / static_cast<float>(bytes_per_sec);
                unitree::common::Sleep(chunk_sec);
            } else {
                unitree::common::Sleep(1);
            }
            offset += current_size;
        }
        audio_client_->PlayStop(stream_id);
    });
}

void State_Mimic::stop_audio()
{
    audio_thread_running = false;
    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }
    if (audio_client_ && !audio_stream_id_.empty()) {
        audio_client_->PlayStop(audio_stream_id_);
        audio_stream_id_.clear();
    }
}
