include(${CMAKE_CURRENT_LIST_DIR}/MbedTLS.cmake)

# Headers only. Runtime Vulkan is loaded from the installed graphics driver.
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(vulkan_headers
  URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.290.tar.gz
  URL_HASH SHA256=f38a653bf93cab7a2a229a53d2d53b1cba9a2819e4c0a7de13c54085bde9bcf5
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(vulkan_headers)
