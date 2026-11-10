#include <windows.h>
#include <mfapi.h>
#include <dm/camera_encode.hpp>
#include <cstring>
#include <algorithm>
#include <stdexcept>
namespace dm::camera {
void mirror_nv12(std::span<std::uint8_t> pixels, Format format) {
    validate_format(format);
    const std::size_t width = format.width, height = format.height;
    if (pixels.size() != width * height * 3 / 2) throw std::invalid_argument("Invalid mirror NV12 geometry");
    for (std::size_t y = 0; y < height; ++y) std::reverse(pixels.begin() + y * width, pixels.begin() + (y + 1) * width);
    // Interleaved UV pairs describe two horizontal pixels: reverse pairs, never U/V order.
    for (std::size_t y = 0; y < height / 2; ++y) {
        auto row = pixels.subspan(width * height + y * width, width);
        for (std::size_t x = 0; x < width / 2; x += 2) {
            std::swap(row[x], row[width - 2 - x]);
            std::swap(row[x + 1], row[width - 1 - x]);
        }
    }
}
std::vector<std::uint8_t> nv12_pixels(const Sample& sample, Format format) {
    validate_format(format);
    if (!sample.native) throw std::invalid_argument("Camera sample is empty");
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample.native->ConvertToContiguousBuffer(&buffer)))
        throw std::runtime_error("Camera buffer is unavailable");
    const auto size = static_cast<DWORD>(format.width * format.height * 3 / 2);
    std::vector<std::uint8_t> pixels(size);
    Microsoft::WRL::ComPtr<IMF2DBuffer> image;
    if (SUCCEEDED(buffer.As(&image))) {
        DWORD length = 0;
        if (FAILED(image->GetContiguousLength(&length)) || length != size)
            throw std::runtime_error("Camera NV12 geometry changed");
        if (FAILED(image->ContiguousCopyTo(pixels.data(), size))) throw std::runtime_error("Camera NV12 copy failed");
    } else {
        BYTE* data = nullptr;
        DWORD capacity = 0, length = 0;
        if (FAILED(buffer->Lock(&data, &capacity, &length))) throw std::runtime_error("Camera buffer lock failed");
        struct Unlock {
            IMFMediaBuffer* buffer;
            ~Unlock() { buffer->Unlock(); }
        } unlock{buffer.Get()};
        if (length != size || capacity < length) throw std::runtime_error("Camera NV12 buffer length changed");
        std::memcpy(pixels.data(), data, size);
    }
    return pixels;
}
}
