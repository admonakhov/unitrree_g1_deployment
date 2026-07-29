# Запуск политик движения робота Unitree G1

В этом репозитории поддерживается выбор пакета ONNX Runtime во время конфигурации CMake для запуска с внешнего или локального ПК.

## Поддерживаемый флаг

- `USE_ONNXRUNTIME_AARCH64`
  - По умолчанию `OFF`
  - Когда `OFF`, сборка использует `thirdparty/onnxruntime-linux-x64-1.22.0` для запуска на x64 архитектруе (внешние ПК) 
  - Когда `ON`, сборка использует `thirdparty/onnxruntime-linux-aarch64-1.23.2` для запуска на arm архитектруе (локальный ПК) 

## Примеры сборки

### Сборка unitree_sdk2 (если не установлено)

```bash

sudo apt install -y libyaml-cpp-dev libboost-all-dev libeigen3-dev libspdlog-dev libfmt-dev

cd deploy/thirdparty/unitree_sdk2
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=OFF # Install on the /usr/local directory
sudo make install
```

### Сборка x64

```bash
cd robots/g1_29dof/build
source /opt/ros/humble/setup.bash
cmake ..
make
```

### Сборка aarch64

```bash
cd robots/g1_29dof/build
source /opt/ros/humble/setup.bash
cmake .. -DUSE_ONNXRUNTIME_AARCH64=ON
make
```

Контроллер теперь линкуется с ROS2 `rclcpp` и `geometry_msgs`, поэтому перед `cmake` и перед запуском нужно source-ить ROS2 окружение.

## Использование для деплоя

Ниже приведён базовый порядок запуска контроллера и подачи команд скорости.

```bash
cd robots/g1_29dof/build
source /opt/ros/humble/setup.bash
./g1_ctrl
```

В состоянии `Velocity` команда скорости берётся из ROS2 топика `/cmd_vel` типа `geometry_msgs/msg/Twist`:

```bash
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist \
  "{linear: {x: 0.2, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}" -r 10
```

Используются поля `linear.x`, `linear.y`, `angular.z`. Они ограничиваются диапазонами из `deploy/robots/g1_29dof/config/policy/velocity/v0/params/deploy.yaml` (`lin_vel_x`, `lin_vel_y`, `ang_vel_z`). Если `/cmd_vel` не приходит дольше 0.5 секунды, команда автоматически становится `[0, 0, 0]`.

Чтобы деплой не закрывался при закрытии терминала используйте screen:

```bash
screen -S g1_ctrl
cd robots/g1_29dof/build
source /opt/ros/humble/setup.bash
./g1_ctrl
```

Для запуска с внешненго ПК необходимо указать используемый интерфейс:

```bash
cd robots/g1_29dof/build
source /opt/ros/humble/setup.bash
./g1_ctrl -n eth0
```

Переведите робота в рабочие состояния с пульта:
   - `L2 + Up` для перехода в `FixStand`
   - `R1 + X` для перехода в `Velocity`
   - `L2 + B` для перехода в `Passive`


