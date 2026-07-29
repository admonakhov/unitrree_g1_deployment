#!/usr/bin/env bash
set -e

source /opt/ros/humble/setup.bash

export G1_PROJECT_DIR=${G1_PROJECT_DIR:-/workspace/g1-cart-delivery}
export LD_LIBRARY_PATH=/usr/local/lib:${G1_PROJECT_DIR}/deploy/thirdparty/onnxruntime-linux-aarch64-1.23.2/lib:${LD_LIBRARY_PATH:-}

cd "${G1_PROJECT_DIR}"
exec "$@"
