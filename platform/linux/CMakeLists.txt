if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  message(FATAL_ERROR "The initial Linux target is x86_64.")
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
find_package(OpenSSL 3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.24 gstreamer-app-1.0>=1.24)
set(CMAKE_AUTOMOC ON)
add_executable(SideScreen
  tls.cpp receiver.cpp receiver.hpp media.cpp media.hpp input.cpp input.hpp
  ../../apps/linux/main.cpp ../../apps/linux/controller.cpp ../../apps/linux/controller.hpp)
target_include_directories(SideScreen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/apps/linux")
target_compile_features(SideScreen PRIVATE cxx_std_20)
target_compile_options(SideScreen PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(SideScreen PRIVATE dm_probe_protocol OpenSSL::SSL OpenSSL::Crypto
  Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 PkgConfig::GSTREAMER)
# Stable resource paths, independent of this subdirectory's relative location.
set_source_files_properties(../../apps/linux/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(../../apps/linux/Video.qml PROPERTIES QT_RESOURCE_ALIAS Video.qml)
set_source_files_properties(../../assets/branding/icon.png PROPERTIES QT_RESOURCE_ALIAS sidescreen.png)
qt_add_resources(SideScreen ui PREFIX / FILES
  ../../apps/linux/Main.qml ../../apps/linux/Video.qml ../../assets/branding/icon.png)
include(GNUInstallDirs)
install(TARGETS SideScreen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../../packaging/linux/io.github.EEkebin.SideScreen.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ../../assets/branding/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME sidescreen.png)
install(FILES ../../README.md ../../docs/LINUX.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/sidescreen)
