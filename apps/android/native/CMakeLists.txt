cmake_minimum_required(VERSION 3.22)
project(extrapane_android LANGUAGES CXX)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
add_library(extrapane SHARED protocol_jni.cpp
    "${ROOT}/core/protocol/video_protocol.cpp"
    "${ROOT}/core/protocol/input_protocol.cpp")
target_include_directories(extrapane PRIVATE "${ROOT}/core/protocol/include")
target_compile_features(extrapane PRIVATE cxx_std_20)
target_compile_options(extrapane PRIVATE -Wall -Wextra -Wpedantic)
