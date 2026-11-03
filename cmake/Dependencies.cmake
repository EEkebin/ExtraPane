include(FetchContent)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(USE_SHARED_TF_PSA_CRYPTO_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_TF_PSA_CRYPTO_LIBRARY ON CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
  URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.1/mbedtls-4.1.1.tar.bz2
  URL_HASH SHA256=3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(mbedtls)

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
