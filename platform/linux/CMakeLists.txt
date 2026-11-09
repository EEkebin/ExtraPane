if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  message(FATAL_ERROR "The initial Linux target is x86_64.")
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 DBus Widgets)
find_package(OpenSSL 3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.24 gstreamer-app-1.0>=1.24 gstreamer-video-1.0>=1.24)
set(CMAKE_AUTOMOC ON)
add_executable(ExtraPane
  tls.cpp host.cpp host.hpp portal.cpp portal.hpp receiver.cpp receiver.hpp media.cpp media.hpp input.cpp input.hpp
  ../../apps/linux/peer_runtime.cpp ../../apps/linux/peer_runtime.hpp
  ../../apps/shared/peer_controller.cpp ../../apps/shared/peer_controller.hpp ../../apps/shared/device_directory.cpp ../../apps/shared/device_directory.hpp
  ../../apps/linux/main.cpp ../../apps/linux/controller.cpp ../../apps/linux/controller.hpp
  ../../apps/linux/connection_runtime.cpp ../../apps/linux/connection_runtime.hpp ../../apps/linux/admission.cpp ../../apps/linux/admission.hpp
  resource_source.cpp resource_source.hpp resource_receiver.cpp resource_receiver.hpp
  ../../apps/shared/connection_model.cpp ../../apps/shared/connection_model.hpp
  ../../apps/shared/resource_model.cpp ../../apps/shared/resource_model.hpp)
target_include_directories(ExtraPane PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/apps/linux" "${PROJECT_SOURCE_DIR}/apps/shared")
target_compile_features(ExtraPane PRIVATE cxx_std_20)
target_compile_options(ExtraPane PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ExtraPane PRIVATE extrapane_peer_service extrapane_session dm_probe_protocol OpenSSL::SSL OpenSSL::Crypto
  Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Widgets PkgConfig::GSTREAMER)
# Stable resource paths, independent of this subdirectory's relative location.
set_source_files_properties(../../apps/shared/ui/PeerWorkspace.qml PROPERTIES QT_RESOURCE_ALIAS PeerWorkspace.qml)
set_source_files_properties(../../apps/shared/ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(../../apps/shared/ui/ResourcePicker.qml PROPERTIES QT_RESOURCE_ALIAS ResourcePicker.qml)
set_source_files_properties(../../apps/shared/ui/ClientConnections.qml PROPERTIES QT_RESOURCE_ALIAS ClientConnections.qml)
set_source_files_properties(../../apps/shared/ui/AppIcon.qml PROPERTIES QT_RESOURCE_ALIAS AppIcon.qml)
set_source_files_properties(../../apps/shared/ui/IconButton.qml PROPERTIES QT_RESOURCE_ALIAS IconButton.qml)
set_source_files_properties(../../apps/linux/ReceiverWindow.qml PROPERTIES QT_RESOURCE_ALIAS ReceiverWindow.qml)
set_source_files_properties(../../apps/linux/Video.qml PROPERTIES QT_RESOURCE_ALIAS Video.qml)
set_source_files_properties(../../assets/branding/icon.png PROPERTIES QT_RESOURCE_ALIAS extrapane.png)
qt_add_resources(ExtraPane ui PREFIX / FILES
  ../../apps/shared/ui/PeerWorkspace.qml ../../apps/shared/ui/Main.qml ../../apps/shared/ui/ResourcePicker.qml ../../apps/shared/ui/ClientConnections.qml ../../apps/shared/ui/AppIcon.qml ../../apps/shared/ui/IconButton.qml ../../apps/linux/Video.qml ../../apps/linux/ReceiverWindow.qml ../../assets/branding/icon.png)
include(GNUInstallDirs)
install(TARGETS ExtraPane RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../../packaging/linux/io.github.EEkebin.ExtraPane.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ../../assets/branding/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME extrapane.png)
install(FILES ../../README.md ../../docs/LINUX.md ../../docs/LINUX-RESOURCES.md ../../docs/PLATFORM-PARITY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/extrapane)

option(EXTRAPANE_LINUX_TESTS "Build synthetic Linux input checks" OFF)
if(EXTRAPANE_LINUX_TESTS)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(extrapane-linux-input-check ../../tests/linux_input_check.cpp input.cpp input.hpp)
  target_include_directories(extrapane-linux-input-check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(extrapane-linux-input-check PRIVATE dm_probe_protocol Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  add_executable(extrapane-linux-output-slot-check ../../tests/linux_output_slot_check.cpp)
  target_compile_features(extrapane-linux-output-slot-check PRIVATE cxx_std_20)
  add_executable(extrapane-linux-resource-fanout-check ../../tests/linux_resource_fanout_check.cpp)
  target_compile_features(extrapane-linux-resource-fanout-check PRIVATE cxx_std_20)
  add_executable(extrapane-linux-resource-pcm-check ../../tests/linux_resource_pcm_check.cpp)
  target_link_libraries(extrapane-linux-resource-pcm-check PRIVATE extrapane_session)
  set(EXTRAPANE_ACCESS_LIBRARY dm_access)
  set(EXTRAPANE_CRYPTO_LIBRARY crypto)
  add_subdirectory(../../tests/linux_ui "${CMAKE_CURRENT_BINARY_DIR}/ui-checks")
endif()
