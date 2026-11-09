cmake_minimum_required(VERSION 3.22)
project(extrapane_android LANGUAGES C CXX)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
add_library(extrapane SHARED peer_service_jni.cpp protocol_jni.cpp access_jni.cpp admission_jni.cpp camera_jni.cpp microphone_jni.cpp
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

# The Android SDK pins CMake 3.22.1; compile the same portable peer sources without
# raising that toolchain requirement through the standalone desktop test project.
add_library(extrapane_peers STATIC
    "${ROOT}/core/peers/peers.cpp" "${ROOT}/core/peers/store.cpp"
    "${ROOT}/core/peers/rpc.cpp" "${ROOT}/core/peers/media_bridge.cpp")
target_include_directories(extrapane_peers PUBLIC "${ROOT}/core/peers/include")
target_compile_features(extrapane_peers PUBLIC cxx_std_20)
target_compile_definitions(extrapane_peers PRIVATE BORINGSSL_PREFIX=EXTRAPANE)
target_link_libraries(extrapane_peers PRIVATE crypto)
set_target_properties(extrapane_peers PROPERTIES POSITION_INDEPENDENT_CODE ON)
include("${ROOT}/cmake/Peers.cmake")
set_target_properties(extrapane_peer_transport extrapane_peer_session extrapane_peer_service PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(extrapane PRIVATE extrapane_peer_service)
