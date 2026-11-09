# Include after extrapane_usb_runner and extrapane_usb_recovery exist.
add_executable(extrapane_usb_recovery_tool "${CMAKE_CURRENT_LIST_DIR}/../apps/usb-recovery/main.cpp")
target_compile_features(extrapane_usb_recovery_tool PRIVATE cxx_std_20)
target_compile_definitions(extrapane_usb_recovery_tool PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(extrapane_usb_recovery_tool PRIVATE extrapane_usb_recovery extrapane_usb_runner)
set_target_properties(extrapane_usb_recovery_tool PROPERTIES OUTPUT_NAME "ExtraPane.UsbRecovery")
if(EXTRAPANE_RESOURCE_SECURITY_TESTS)
  # Argument/help output only: no registry, inventory, processes, or devices.
  add_test(NAME usb-recovery-cli-help COMMAND extrapane_usb_recovery_tool --help)
endif()
