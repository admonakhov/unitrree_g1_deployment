cmake_minimum_required(VERSION 3.12)
project(g1_29dof_controller)

set(CMAKE_CXX_STANDARD 17)

option(USE_ONNXRUNTIME_AARCH64 "Link against the aarch64 ONNX Runtime package" OFF)
set(UNITREE_SDK_PREFIX "/usr/local" CACHE PATH "Unitree SDK2 install prefix")

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
  ${UNITREE_SDK_PREFIX}/include # unitree_sdk2 + bundled ddsc headers
  ${UNITREE_SDK_PREFIX}/include/ddscxx # bundled CycloneDDS C++ headers
  ${UNITREE_SDK_PREFIX}/include/iceoryx/v2.0.2 # bundled iceoryx headers
  include
  ${ONNXRUNTIME_ROOT}/include
  ${PROJECT_SOURCE_DIR}/../../include/
)

find_library(UNITREE_SDK2_LIB unitree_sdk2 PATHS ${UNITREE_SDK_PREFIX}/lib NO_DEFAULT_PATH REQUIRED)
find_library(UNITREE_DDSC_LIB ddsc PATHS ${UNITREE_SDK_PREFIX}/lib NO_DEFAULT_PATH REQUIRED)
find_library(UNITREE_DDSCXX_LIB ddscxx PATHS ${UNITREE_SDK_PREFIX}/lib NO_DEFAULT_PATH REQUIRED)

link_libraries(
  ${UNITREE_SDK2_LIB} ${UNITREE_DDSC_LIB} ${UNITREE_DDSCXX_LIB} rt pthread # dds
  libboost_program_options.a libyaml-cpp.a fmt
  ${ONNXRUNTIME_LIB}
)

file(GLOB_RECURSE ADD_SRC_LIST
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)
list(REMOVE_ITEM ADD_SRC_LIST ${PROJECT_SOURCE_DIR}/src/CmdVelBridge.cpp)

add_library(${PROJECT_NAME}_lib ${ADD_SRC_LIST})
target_include_directories(${PROJECT_NAME}_lib BEFORE PRIVATE
  ${UNITREE_SDK_PREFIX}/include
  ${UNITREE_SDK_PREFIX}/include/ddscxx
  ${UNITREE_SDK_PREFIX}/include/iceoryx/v2.0.2
)
target_compile_options(${PROJECT_NAME}_lib PRIVATE
  -I${UNITREE_SDK_PREFIX}/include
  -I${UNITREE_SDK_PREFIX}/include/ddscxx
  -I${UNITREE_SDK_PREFIX}/include/iceoryx/v2.0.2
)
add_library(${PROJECT_NAME}_cmd_vel_bridge src/CmdVelBridge.cpp)
target_include_directories(${PROJECT_NAME}_cmd_vel_bridge PRIVATE
  include
)
ament_target_dependencies(${PROJECT_NAME}_cmd_vel_bridge rclcpp geometry_msgs)

add_executable(g1_ctrl main.cpp)
target_include_directories(g1_ctrl BEFORE PRIVATE
  ${UNITREE_SDK_PREFIX}/include
  ${UNITREE_SDK_PREFIX}/include/ddscxx
  ${UNITREE_SDK_PREFIX}/include/iceoryx/v2.0.2
)
target_compile_options(g1_ctrl PRIVATE
  -I${UNITREE_SDK_PREFIX}/include
  -I${UNITREE_SDK_PREFIX}/include/ddscxx
  -I${UNITREE_SDK_PREFIX}/include/iceoryx/v2.0.2
)
target_link_libraries(g1_ctrl PRIVATE ${PROJECT_NAME}_lib ${PROJECT_NAME}_cmd_vel_bridge)
