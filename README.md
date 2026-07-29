# Запуск политик движения робота Unitree G1

В этом репозитории поддерживается выбор пакета ONNX Runtime во время конфигурации CMake для запуска с внешнего или локального ПК.

## Поддерживаемые ROS2 дистрибутивы

- Docker deploy на Jetson Orin/aarch64 по умолчанию использует ROS2 Foxy (`ros:foxy-ros-base-focal`). Это соответствует JetPack 5 / Ubuntu 20.04 сценариям.
- ROS2 Humble тоже поддерживается через переменную `ROS_DISTRO=humble` и base image `ros:humble-ros-base-jammy`.
- C++ bridge `/cmd_vel` использует только стабильные API `rclcpp` + `geometry_msgs/msg/Twist`, совместимые с Foxy и Humble.

## Поддерживаемый флаг ONNX Runtime

- `USE_ONNXRUNTIME_AARCH64`
  - По умолчанию `OFF`.
  - Когда `OFF`, сборка использует `thirdparty/onnxruntime-linux-x64-1.22.0` для запуска на x64 архитектуре.
  - Когда `ON`, сборка использует `thirdparty/onnxruntime-linux-aarch64-1.23.2` для запуска на ARM64/aarch64, например Jetson Orin.

## Docker deploy на Jetson Orin (aarch64, ROS2 Foxy по умолчанию)

Целевой способ деплоя: запускать контроллер из Docker-контейнера на Jetson Orin/aarch64. Dockerfile устанавливает зависимости из README, собирает `unitree_sdk2`, затем собирает `g1_ctrl` с `-DUSE_ONNXRUNTIME_AARCH64=ON`.

### Сборка ROS2 Foxy образа на Jetson

```bash
cd /path/to/g1-cart-delivery
docker/build_jetson.sh
```

По умолчанию используется:

- `ROS_DISTRO=foxy`
- image: `g1-cart-delivery:foxy-aarch64`
- platform: `linux/arm64`
- base image: `ros:foxy-ros-base-focal`

Если нужен свой L4T/Foxy base image:

```bash
BASE_IMAGE=<your-jetson-foxy-base> docker/build_jetson.sh
```

### Сборка ROS2 Humble образа, если нужен Humble

```bash
ROS_DISTRO=humble docker/build_jetson.sh
```

или явно:

```bash
ROS_DISTRO=humble BASE_IMAGE=ros:humble-ros-base-jammy docker/build_jetson.sh
```

### Запуск контроллера из контейнера

Для Foxy по умолчанию:

```bash
NETWORK_IFACE=eth0 docker/run_jetson.sh
```

Для Humble:

```bash
ROS_DISTRO=humble NETWORK_IFACE=eth0 docker/run_jetson.sh
```

Скрипт запускает контейнер с `--network host`, чтобы ROS2/DDS discovery и Unitree DDS работали через сетевой интерфейс Jetson. Внутри контейнера выполняется:

```bash
source /opt/ros/${ROS_DISTRO}/setup.bash
cd deploy/robots/g1_29dof/build
./g1_ctrl -n eth0
```

Для другого интерфейса:

```bash
NETWORK_IFACE=enP8p1s0 docker/run_jetson.sh
```

Публикация команды скорости, например из другого ROS2 Foxy/Humble терминала или контейнера в той же host network:

```bash
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist \
  "{linear: {x: 0.2, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}" -r 10
```

## Локальная сборка без Docker

### Сборка unitree_sdk2 (если не установлено)

```bash
sudo apt install -y libyaml-cpp-dev libboost-all-dev libeigen3-dev libspdlog-dev libfmt-dev

cd deploy/thirdparty/unitree_sdk2
mkdir -p build && cd build
cmake .. -DBUILD_EXAMPLES=OFF # Install on the /usr/local directory
sudo make install
```

### Сборка x64

```bash
export ROS_DISTRO=foxy  # или humble
cd deploy/robots/g1_29dof/build
source /opt/ros/${ROS_DISTRO}/setup.bash
cmake ..
make
```

### Сборка aarch64

```bash
export ROS_DISTRO=foxy  # или humble
cd deploy/robots/g1_29dof/build
source /opt/ros/${ROS_DISTRO}/setup.bash
cmake .. -DUSE_ONNXRUNTIME_AARCH64=ON
make
```

Контроллер линкуется с ROS2 `rclcpp` и `geometry_msgs`, поэтому перед `cmake` и перед запуском нужно source-ить ROS2 окружение.

Если при сборке в ROS2 Foxy появляется ошибка из `/usr/local/include/ddscxx/...` вида `DDS_DATA_REPRESENTATION_FLAG_XCDR1 was not declared`, `dds_create_topic_sertype was not declared` или `getSerType is not a member`, это конфликт заголовков CycloneDDS из Unitree SDK2 и ROS2. Пересоберите контроллер с чистым build-каталогом, чтобы CMake применил приоритет `/usr/local/include` для Unitree SDK2:

```bash
rm -rf deploy/robots/g1_29dof/build
mkdir -p deploy/robots/g1_29dof/build
cd deploy/robots/g1_29dof/build
source /opt/ros/${ROS_DISTRO}/setup.bash
cmake .. -DUNITREE_SDK_PREFIX=/usr/local -DUSE_ONNXRUNTIME_AARCH64=ON
make -j$(nproc)
```

## Использование для деплоя

Ниже приведён базовый порядок запуска контроллера и подачи команд скорости.

```bash
export ROS_DISTRO=foxy  # или humble
cd deploy/robots/g1_29dof/build
source /opt/ros/${ROS_DISTRO}/setup.bash
./g1_ctrl
```

В состоянии `Velocity` команда скорости берётся из ROS2 топика `/cmd_vel` типа `geometry_msgs/msg/Twist`.

Используются поля `linear.x`, `linear.y`, `angular.z`. Они ограничиваются диапазонами из `deploy/robots/g1_29dof/config/policy/velocity/v0/params/deploy.yaml` (`lin_vel_x`, `lin_vel_y`, `ang_vel_z`). Если `/cmd_vel` не приходит дольше 0.5 секунды, команда автоматически становится `[0, 0, 0]`.

Чтобы деплой не закрывался при закрытии терминала используйте screen:

```bash
screen -S g1_ctrl
export ROS_DISTRO=foxy  # или humble
cd deploy/robots/g1_29dof/build
source /opt/ros/${ROS_DISTRO}/setup.bash
./g1_ctrl
```

Для запуска с внешнего ПК необходимо указать используемый интерфейс:

```bash
export ROS_DISTRO=foxy  # или humble
cd deploy/robots/g1_29dof/build
source /opt/ros/${ROS_DISTRO}/setup.bash
./g1_ctrl -n eth0
```

Переведите робота в рабочие состояния с пульта:

- `L2 + Up` для перехода в `FixStand`
- `R1 + X` для перехода в `Velocity`
- `L2 + B` для перехода в `Passive`
