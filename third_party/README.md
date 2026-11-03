# Native TLS dependency

The secure prototypes statically link the official **Mbed TLS 4.1.1** release, which includes **TF-PSA-Crypto 1.1.1**. CMake downloads the complete upstream release tarball with SHA256 verification; see `cmake/Dependencies.cmake`. No upstream source is edited.

- Source: https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-4.1.1
- Archive SHA256: `3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c`
- Core projects are available under Apache-2.0 OR GPL-2.0-or-later; this project uses the Apache-2.0 option. Bundled third-party components retain their file-specific notices/licenses.

The verified downloaded source under the CMake dependency directory retains upstream license and copyright notices. Copies of the root license files and bundled-driver notices are kept in `licenses/` for development distribution. A shipping package must preserve all applicable notices for its actual linked configuration. No license for DM's original code is chosen by this dependency decision.

The separate indirect display driver derives from Microsoft Windows-driver-samples under the Microsoft Public License (MS-PL). See `drivers/idd/UPSTREAM.md` for the pinned commit and `drivers/idd/LICENSE` for its full license; driver source derivatives remain under that license. SDK/WDK NuGet packages are build tools restored under ignored `out/`, not included in runtime packages.

## Vulkan headers

The optional Vulkan renderer compiles against **Khronos Vulkan-Headers v1.3.290**, pinned by SHA256 `f38a653bf93cab7a2a229a53d2d53b1cba9a2819e4c0a7de13c54085bde9bcf5` in `cmake/Dependencies.cmake`. Only headers are consumed; the C++ module, dependency tests and installation targets are disabled. No Vulkan SDK, loader or graphics driver is bundled. The system Vulkan loader is loaded dynamically from System32, so missing Vulkan support does not prevent D3D11/D3D12 use.

Source: https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.3.290 . The consumed headers are Apache-2.0 OR MIT; this project uses the Apache-2.0 option. Packages preserve upstream `LICENSE.md` and both full license texts from `LICENSES/` under `Licenses/vulkan-headers/`.
