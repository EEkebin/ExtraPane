cmake_minimum_required(VERSION 3.22)
project(extrapane_android LANGUAGES C CXX)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
add_library(extrapane SHARED protocol_jni.cpp access_jni.cpp admission_jni.cpp camera_jni.cpp microphone_jni.cpp
    "${ROOT}/core/session/wire.cpp"
    "${ROOT}/core/session/channel.cpp"
    "${ROOT}/core/session/media.cpp"
    "${ROOT}/core/session/camera_protocol.cpp"
    "${ROOT}/core/session/microphone_protocol.cpp"
    "${ROOT}/core/protocol/video_protocol.cpp"
    "${ROOT}/core/protocol/input_protocol.cpp")
target_include_directories(extrapane PRIVATE "${ROOT}/core/protocol/include" "${ROOT}/core/session/include")
target_compile_features(extrapane PRIVATE cxx_std_20)
target_compile_options(extrapane PRIVATE -Wall -Wextra -Wpedantic)

include("${ROOT}/cmake/Access.cmake")
target_link_libraries(extrapane PRIVATE dm_access)
