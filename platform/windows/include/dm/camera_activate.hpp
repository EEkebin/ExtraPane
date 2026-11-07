#pragma once
#include <mfidl.h>
namespace dm::camera {
// Construction creates an attribute store only. No camera/pipe is activated.
HRESULT create_camera_activation(IMFActivate** out) noexcept;
}
