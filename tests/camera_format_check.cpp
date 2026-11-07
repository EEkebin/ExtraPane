#include <dm/camera_resource.hpp>
#include <dm/camera_encode.hpp>
#include <mfapi.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::invalid_argument&){rejected=true;}if(!rejected)throw std::runtime_error("Invalid camera configuration accepted");}
int main(){try{
    using namespace dm::camera;
    // Horizontal mirroring preserves row order and the U/V order within each chroma pair.
    for(unsigned width:{4u,6u}){
        std::vector<std::uint8_t> pixels(width*3);for(std::size_t i=0;i<pixels.size();++i)pixels[i]=static_cast<std::uint8_t>(i+1);
        const auto original=pixels;mirror_nv12(pixels,{width,2,30});
        for(unsigned row=0;row<2;++row)for(unsigned x=0;x<width;++x)if(pixels[row*width+x]!=original[row*width+width-1-x])throw std::runtime_error("mirrored luma");
        for(unsigned x=0;x<width;x+=2)for(unsigned uv=0;uv<2;++uv)if(pixels[width*2+x+uv]!=original[width*2+width-2-x+uv])throw std::runtime_error("mirrored chroma");
        mirror_nv12(pixels,{width,2,30});if(pixels!=original)throw std::runtime_error("mirror toggle roundtrip");
    }
    rejects([]{std::vector<std::uint8_t> shortFrame(11);mirror_nv12(shortFrame,{4,2,30});});
    validate_format({1920,1080,30});validate_format({3840,2160,60});validate_format({640,480,15});
    for(auto format:{Format{0,1080,30},Format{1921,1080,30},Format{1920,1081,30},Format{4096,2160,30},Format{1920,1080,0},Format{1920,1080,61}})rejects([&]{validate_format(format);});
    rejects([]{SourceResource invalid(L"",{},[](Sample){});});
    rejects([]{SourceResource invalid(L"local-camera",{},{});});
    // Construction and repeated stop must be inert. No prepare/start, COM
    // initialization, inventory enumeration or physical capture occurs here.
    SourceResource inert(L"not-a-device",{},[](Sample){throw std::runtime_error("Unexpected capture");});
    inert.stop();inert.stop();
    if(inert.start().available)throw std::runtime_error("Unprepared camera started");
    // Synthetic CPU buffers exercise packing without enumerating cameras or
    // creating an encoder, GPU device or capture source.
    if(FAILED(MFStartup(MF_VERSION)))throw std::runtime_error("MFStartup failed");
    struct Shutdown{~Shutdown(){MFShutdown();}} shutdown;
    Sample sample;Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    if(FAILED(MFCreateSample(&sample.native))||FAILED(MFCreateMemoryBuffer(24,&buffer)))throw std::runtime_error("Synthetic allocation failed");
    BYTE* data=nullptr;DWORD capacity=0,length=0;
    if(FAILED(buffer->Lock(&data,&capacity,&length)))throw std::runtime_error("Synthetic lock failed");
    std::memset(data,73,24);buffer->Unlock();buffer->SetCurrentLength(24);sample.native->AddBuffer(buffer.Get());
    const auto pixels=nv12_pixels(sample,{4,4,30});
    if(pixels.size()!=24||pixels.front()!=73||pixels.back()!=73)throw std::runtime_error("Synthetic NV12 bytes changed");
    bool malformed=false;buffer->SetCurrentLength(23);
    try{nv12_pixels(sample,{4,4,30});}catch(const std::runtime_error&){malformed=true;}
    if(!malformed)throw std::runtime_error("Truncated NV12 was accepted");
    std::cout<<"Camera format and inert lifecycle checks passed (no device access)\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
