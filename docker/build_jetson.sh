#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)
IMAGE=${IMAGE:-g1-cart-delivery:humble-aarch64}
DOCKER_PLATFORM=${DOCKER_PLATFORM:-linux/arm64}
BASE_IMAGE=${BASE_IMAGE:-ros:humble-ros-base-jammy}

cd "${REPO_DIR}"

docker build \
  --platform "${DOCKER_PLATFORM}" \
  --build-arg BASE_IMAGE="${BASE_IMAGE}" \
  -f docker/Dockerfile.jetson \
  -t "${IMAGE}" \
  .
