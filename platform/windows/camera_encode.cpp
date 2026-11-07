#include <windows.h>
#include <mfapi.h>
#include <dm/camera_encode.hpp>
#include <cstring>
#include <stdexcept>
namespace dm::camera {
std::vector<std::uint8_t> nv12_pixels(const Sample& sample,Format format){
    validate_format(format);
    if(!sample.native)throw std::invalid_argument("Camera sample is empty");
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    if(FAILED(sample.native->ConvertToContiguousBuffer(&buffer)))throw std::runtime_error("Camera buffer is unavailable");
    const auto size=static_cast<DWORD>(format.width*format.height*3/2);
    std::vector<std::uint8_t> pixels(size);
    Microsoft::WRL::ComPtr<IMF2DBuffer> image;
    if(SUCCEEDED(buffer.As(&image))){
        DWORD length=0;
        if(FAILED(image->GetContiguousLength(&length))||length!=size)throw std::runtime_error("Camera NV12 geometry changed");
        if(FAILED(image->ContiguousCopyTo(pixels.data(),size)))throw std::runtime_error("Camera NV12 copy failed");
    }else{
        BYTE* data=nullptr;DWORD capacity=0,length=0;
        if(FAILED(buffer->Lock(&data,&capacity,&length)))throw std::runtime_error("Camera buffer lock failed");
        struct Unlock{IMFMediaBuffer* buffer;~Unlock(){buffer->Unlock();}} unlock{buffer.Get()};
        if(length!=size||capacity<length)throw std::runtime_error("Camera NV12 buffer length changed");
        std::memcpy(pixels.data(),data,size);
    }
    return pixels;
}
}
