# Read-only implementation library; compiling never enumerates hardware.
add_library(extrapane_usb_inventory STATIC
  "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/usb_inventory.cpp")
target_compile_features(extrapane_usb_inventory PUBLIC cxx_std_20)
target_compile_definitions(extrapane_usb_inventory PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(extrapane_usb_inventory PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/include")
target_link_libraries(extrapane_usb_inventory PUBLIC extrapane_session PRIVATE setupapi cfgmgr32 psapi)
if(EXTRAPANE_RESOURCE_SECURITY_TESTS)
  add_executable(extrapane-usb-inventory-check "${CMAKE_CURRENT_LIST_DIR}/../tests/usb_inventory_check.cpp")
  target_compile_definitions(extrapane-usb-inventory-check PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(extrapane-usb-inventory-check PRIVATE extrapane_usb_inventory)
  add_test(NAME usb-inventory-parsers COMMAND extrapane-usb-inventory-check)
endif()
