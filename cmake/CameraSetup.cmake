# Include after extrapane_camera_source. This builds only; it never registers it.
set(camera_payload_rc "${CMAKE_CURRENT_BINARY_DIR}/camera_setup_payload.rc")
add_custom_command(OUTPUT "${camera_payload_rc}"
  COMMAND "${CMAKE_COMMAND}" "-DPAYLOAD=$<TARGET_FILE:extrapane_camera_source>" "-DOUTPUT=${camera_payload_rc}"
    -P "${CMAKE_CURRENT_LIST_DIR}/WriteCameraPayload.cmake"
  DEPENDS extrapane_camera_source "${CMAKE_CURRENT_LIST_DIR}/WriteCameraPayload.cmake"
  VERBATIM)
add_executable(extrapane_camera_setup "${CMAKE_CURRENT_LIST_DIR}/../apps/camera-setup/main.cpp" "${camera_payload_rc}")
target_compile_features(extrapane_camera_setup PRIVATE cxx_std_20)
target_compile_definitions(extrapane_camera_setup PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(extrapane_camera_setup PRIVATE bcrypt advapi32 shell32 ole32)
set_target_properties(extrapane_camera_setup PROPERTIES OUTPUT_NAME "ExtraPane.CameraSetup")
if(EXTRAPANE_RESOURCE_SECURITY_TESTS)
  add_executable(extrapane-camera-setup-check "${CMAKE_CURRENT_LIST_DIR}/../tests/camera_setup_check.cpp")
  target_compile_features(extrapane-camera-setup-check PRIVATE cxx_std_20)
  add_test(NAME camera-setup-policy COMMAND extrapane-camera-setup-check)
  add_test(NAME camera-setup-payload COMMAND extrapane_camera_setup --check-package)
endif()
