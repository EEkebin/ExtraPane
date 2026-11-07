if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  message(FATAL_ERROR "The initial Linux target is x86_64.")
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 DBus)
find_package(OpenSSL 3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.24 gstreamer-app-1.0>=1.24)
set(CMAKE_AUTOMOC ON)
add_executable(ExtraPane
  tls.cpp host.cpp host.hpp portal.cpp portal.hpp receiver.cpp receiver.hpp media.cpp media.hpp input.cpp input.hpp
  ../../apps/linux/main.cpp ../../apps/linux/controller.cpp ../../apps/linux/controller.hpp
  ../../apps/shared/resource_model.cpp ../../apps/shared/resource_model.hpp)
target_include_directories(ExtraPane PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/apps/linux" "${PROJECT_SOURCE_DIR}/apps/shared")
target_compile_features(ExtraPane PRIVATE cxx_std_20)
target_compile_options(ExtraPane PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ExtraPane PRIVATE extrapane_session dm_probe_protocol OpenSSL::SSL OpenSSL::Crypto
  Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus PkgConfig::GSTREAMER)
# Stable resource paths, independent of this subdirectory's relative location.
set_source_files_properties(../../apps/shared/ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(../../apps/shared/ui/ResourcePicker.qml PROPERTIES QT_RESOURCE_ALIAS ResourcePicker.qml)
set_source_files_properties(../../apps/linux/Video.qml PROPERTIES QT_RESOURCE_ALIAS Video.qml)
set_source_files_properties(../../assets/branding/icon.png PROPERTIES QT_RESOURCE_ALIAS extrapane.png)
qt_add_resources(ExtraPane ui PREFIX / FILES
  ../../apps/shared/ui/Main.qml ../../apps/shared/ui/ResourcePicker.qml ../../apps/linux/Video.qml ../../assets/branding/icon.png)
include(GNUInstallDirs)
install(TARGETS ExtraPane RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../../packaging/linux/io.github.EEkebin.ExtraPane.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ../../assets/branding/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME extrapane.png)
install(FILES ../../README.md ../../docs/LINUX.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/extrapane)

option(EXTRAPANE_LINUX_TESTS "Build synthetic Linux input checks" OFF)
if(EXTRAPANE_LINUX_TESTS)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(extrapane-linux-input-check ../../tests/linux_input_check.cpp input.cpp input.hpp)
  target_include_directories(extrapane-linux-input-check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(extrapane-linux-input-check PRIVATE dm_probe_protocol Qt6::Quick Qt6::Test)
endif()
