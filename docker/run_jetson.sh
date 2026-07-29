#!/usr/bin/env bash
set -euo pipefail

ROS_DISTRO=${ROS_DISTRO:-foxy}
IMAGE=${IMAGE:-g1-cart-delivery:${ROS_DISTRO}-aarch64}
NETWORK_IFACE=${NETWORK_IFACE:-eth0}
CONTAINER_NAME=${CONTAINER_NAME:-g1-cart-delivery}

# --network host is required for ROS2/DDS discovery and Unitree DDS traffic.
# --ipc host and rtprio/nice help keep controller timing stable on the Jetson.
docker run --rm -it \
  --name "${CONTAINER_NAME}" \
  --network host \
  --ipc host \
  --ulimit rtprio=99 \
  --ulimit memlock=-1 \
  --cap-add SYS_NICE \
  "${IMAGE}" \
  bash -lc "source /opt/ros/${ROS_DISTRO}/setup.bash && cd deploy/robots/g1_29dof/build && ./g1_ctrl -n ${NETWORK_IFACE}"
