#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)
ROS_DISTRO=${ROS_DISTRO:-foxy}
IMAGE=${IMAGE:-g1-cart-delivery:${ROS_DISTRO}-aarch64}
DOCKER_PLATFORM=${DOCKER_PLATFORM:-linux/arm64}

case "${ROS_DISTRO}" in
  foxy)
    BASE_IMAGE=${BASE_IMAGE:-ros:foxy-ros-base-focal}
    ;;
  humble)
    BASE_IMAGE=${BASE_IMAGE:-ros:humble-ros-base-jammy}
    ;;
  *)
    BASE_IMAGE=${BASE_IMAGE:-ros:${ROS_DISTRO}-ros-base}
    ;;
esac

cd "${REPO_DIR}"

docker build \
  --platform "${DOCKER_PLATFORM}" \
  --build-arg ROS_DISTRO="${ROS_DISTRO}" \
  --build-arg BASE_IMAGE="${BASE_IMAGE}" \
  -f docker/Dockerfile.jetson \
  -t "${IMAGE}" \
  .
