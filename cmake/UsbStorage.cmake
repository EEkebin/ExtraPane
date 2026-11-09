add_library(extrapane_usb_storage STATIC "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/usb_storage.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/usb_host_backend.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/usb_device.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/usb_descriptors.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/usb_device_inventory.cpp")
target_compile_features(extrapane_usb_storage PUBLIC cxx_std_20)
target_compile_definitions(extrapane_usb_storage PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(extrapane_usb_storage PUBLIC extrapane_usb_inventory PRIVATE extrapane_usb_recovery cfgmgr32 bcrypt setupapi)
if(EXTRAPANE_RESOURCE_SECURITY_TESTS)
  add_executable(extrapane-usb-descriptor-check "${CMAKE_CURRENT_LIST_DIR}/../tests/usb_descriptors_check.cpp")
  target_link_libraries(extrapane-usb-descriptor-check PRIVATE extrapane_usb_storage)
  add_test(NAME usb-descriptors COMMAND extrapane-usb-descriptor-check)
  add_executable(extrapane-usb-generic-check "${CMAKE_CURRENT_LIST_DIR}/../tests/usb_generic_check.cpp")
  target_link_libraries(extrapane-usb-generic-check PRIVATE extrapane_usb_storage)
  add_test(NAME usb-generic-lifecycle COMMAND extrapane-usb-generic-check)
  add_executable(extrapane-usb-storage-check "${CMAKE_CURRENT_LIST_DIR}/../tests/usb_storage_check.cpp")
  target_link_libraries(extrapane-usb-storage-check PRIVATE extrapane_usb_storage)
  add_test(NAME usb-storage-lifecycle COMMAND extrapane-usb-storage-check)
  add_executable(extrapane-usb-host-backend-check "${CMAKE_CURRENT_LIST_DIR}/../tests/usb_host_backend_check.cpp")
  target_link_libraries(extrapane-usb-host-backend-check PRIVATE extrapane_usb_storage)
  add_test(NAME usb-host-backend-lifecycle COMMAND extrapane-usb-host-backend-check)
endif()
