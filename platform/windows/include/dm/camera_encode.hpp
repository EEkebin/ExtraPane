#pragma once
#include <dm/camera.hpp>
namespace dm::camera {
// This initial path copies CPU NV12 samples to a contiguous native buffer.
// It is not a zero-copy camera pipeline and never passes pixels through QML.
std::vector<std::uint8_t> nv12_pixels(const Sample& sample,Format format);
}
