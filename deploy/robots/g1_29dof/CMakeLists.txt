cmake_minimum_required(VERSION 3.12)
project(g1_29dof_controller)

set(CMAKE_CXX_STANDARD 17)

option(USE_ONNXRUNTIME_AARCH64 "Link against the aarch64 ONNX Runtime package" OFF)

if(USE_ONNXRUNTIME_AARCH64)
  set(ONNXRUNTIME_ROOT ${PROJECT_SOURCE_DIR}/../../thirdparty/onnxruntime-linux-aarch64-1.23.2)
  set(ONNXRUNTIME_LIB ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so.1.23.2)
else()
  set(ONNXRUNTIME_ROOT ${PROJECT_SOURCE_DIR}/../../thirdparty/onnxruntime-linux-x64-1.22.0)
  set(ONNXRUNTIME_LIB ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so.1.22.0)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(yaml-cpp REQUIRED)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)

include_directories(
  /usr/include/eigen3
  /usr/local/include/ddscxx # dds
  /usr/local/include/iceoryx/v2.0.2 # dds
  include
  ${ONNXRUNTIME_ROOT}/include
  ${PROJECT_SOURCE_DIR}/../../include/
)

link_libraries(
  unitree_sdk2 ddsc ddscxx rt pthread # dds
  libboost_program_options.a libyaml-cpp.a fmt
  ${ONNXRUNTIME_LIB}
)

file(GLOB_RECURSE ADD_SRC_LIST
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

add_library(${PROJECT_NAME}_lib ${ADD_SRC_LIST})
ament_target_dependencies(${PROJECT_NAME}_lib rclcpp geometry_msgs)
link_libraries(${PROJECT_NAME}_lib)

add_executable(g1_ctrl main.cpp)
ament_target_dependencies(g1_ctrl rclcpp geometry_msgs)
