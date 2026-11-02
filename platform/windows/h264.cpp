#include <windows.h>
#include <dm/video_wait.hpp>
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <dm/h264.hpp>
#include <dm/video_gpu.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace dm::video {
using Microsoft::WRL::ComPtr;
namespace {
using Clock = std::chrono::steady_clock;
void check(HRESULT hr, const char* stage) {
    if (FAILED(hr)) {
        std::ostringstream text;
        text << stage << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ')';
        throw std::runtime_error(text.str());
    }
}
struct Runtime {
    Runtime() {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize codec COM");
        const auto result = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
        if (FAILED(result)) { CoUninitialize(); check(result, "Initialize Media Foundation"); }
    }
    ~Runtime() { MFShutdown(); CoUninitialize(); }
};
void dimensions(unsigned width, unsigned height, unsigned fps) {
    if (width < 2 || height < 2 || width > 4096 || height > 4096 || static_cast<std::uint64_t>(width)*height > 8847360 || width % 2 || height % 2 || !fps || fps > 60) throw std::invalid_argument("H264 requires even dimensions within 4096 per axis, 8847360 pixels and 1-60 fps");
}
ComPtr<IMFMediaType> media_type(const GUID& subtype, unsigned width, unsigned height, unsigned fps) {
    ComPtr<IMFMediaType> type;
    check(MFCreateMediaType(&type), "Create codec media type");
    check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Media major type");
    check(type->SetGUID(MF_MT_SUBTYPE, subtype), "Media subtype");
    check(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height), "Media dimensions");
    check(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, fps, 1), "Media frame rate");
    check(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Media aspect ratio");
    check(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Media progressive mode");
    check(type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709), "Media color matrix");
    check(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235), "Media color range");
    return type;
}
void codec_uint(ICodecAPI* api, const GUID& id, ULONG value, bool required = false) {
    VARIANT setting{}; setting.vt = VT_UI4; setting.ulVal = value;
    const auto result = api->SetValue(&id, &setting);
    if (required) check(result, "Configure codec integer property");
}
void codec_bool(ICodecAPI* api, const GUID& id, bool value, bool required = false) {
    VARIANT setting{}; setting.vt = VT_BOOL; setting.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    const auto result = api->SetValue(&id, &setting);
    if (required) check(result, "Configure codec boolean property");
}
ComPtr<IMFSample> memory_sample(std::span<const std::uint8_t> bytes, LONGLONG time, LONGLONG duration) {
    ComPtr<IMFMediaBuffer> buffer;
    check(MFCreateMemoryBuffer(static_cast<DWORD>(bytes.size()), &buffer), "Allocate codec input");
    BYTE* data = nullptr;
    check(buffer->Lock(&data, nullptr, nullptr), "Lock codec input");
    std::memcpy(data, bytes.data(), bytes.size());
    check(buffer->Unlock(), "Unlock codec input");
    check(buffer->SetCurrentLength(static_cast<DWORD>(bytes.size())), "Set codec input size");
    ComPtr<IMFSample> sample;
    check(MFCreateSample(&sample), "Create codec sample");
    check(sample->AddBuffer(buffer.Get()), "Attach codec input");
    check(sample->SetSampleTime(time), "Sample timestamp");
    check(sample->SetSampleDuration(duration), "Sample duration");
    return sample;
}
struct Output {
    MFT_OUTPUT_DATA_BUFFER data{};
    ComPtr<IMFSample> allocated;
    explicit Output(IMFTransform* transform, DWORD fallback_size = 2 * 1024 * 1024) {
        MFT_OUTPUT_STREAM_INFO info{};
        check(transform->GetOutputStreamInfo(0, &info), "Codec output requirements");
        if (!(info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))) {
            check(MFCreateSample(&allocated), "Allocate output sample");
            ComPtr<IMFMediaBuffer> buffer;
            check(MFCreateMemoryBuffer(std::max(info.cbSize, fallback_size), &buffer), "Allocate output bytes");
            check(allocated->AddBuffer(buffer.Get()), "Attach output bytes");
            data.pSample = allocated.Get();
        }
    }
    ~Output() {
        if (data.pEvents) data.pEvents->Release();
        if (data.pSample && data.pSample != allocated.Get()) data.pSample->Release();
    }
};
std::vector<std::uint8_t> sample_bytes(IMFSample* sample) {
    if (!sample) throw std::runtime_error("Encoder returned no sample");
    ComPtr<IMFMediaBuffer> buffer;
    check(sample->ConvertToContiguousBuffer(&buffer), "Contiguous encoded data");
    BYTE* data = nullptr; DWORD size = 0;
    check(buffer->Lock(&data, nullptr, &size), "Read encoded bytes");
    struct Unlock { IMFMediaBuffer* buffer; ~Unlock() { buffer->Unlock(); } } unlock{buffer.Get()};
    if (!size || size > 2 * 1024 * 1024) throw std::runtime_error("Encoded access unit exceeds limit");
    return {data, data + size};
}
std::uint8_t clamp(int value) { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }
std::vector<std::uint8_t> to_nv12(const Frame& frame) {
    const auto width = frame.width, height = frame.height;
    if (frame.bgra.size() != static_cast<std::size_t>(width) * height * 4) throw std::runtime_error("Incorrect BGRA source buffer");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height * 3 / 2);
    const auto rgb = [&](unsigned x, unsigned y, unsigned channel) { return static_cast<int>(frame.bgra[(static_cast<std::size_t>(y) * width + x) * 4 + channel]); };
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        result[static_cast<std::size_t>(y) * width + x] = clamp(((47 * rgb(x,y,2) + 157 * rgb(x,y,1) + 16 * rgb(x,y,0) + 128) >> 8) + 16);
    }
    const auto uv = static_cast<std::size_t>(width) * height;
    for (unsigned y = 0; y < height; y += 2) for (unsigned x = 0; x < width; x += 2) {
        int r=0,g=0,b=0;
        for (unsigned dy=0;dy<2;++dy) for (unsigned dx=0;dx<2;++dx) { r+=rgb(x+dx,y+dy,2); g+=rgb(x+dx,y+dy,1); b+=rgb(x+dx,y+dy,0); }
        r/=4; g/=4; b/=4;
        const auto index = uv + static_cast<std::size_t>(y/2)*width+x;
        result[index] = clamp(((-26*r-87*g+112*b+128)>>8)+128);
        result[index+1] = clamp(((112*r-102*g-10*b+128)>>8)+128);
    }
    return result;
}
void start_transform(IMFTransform* transform) {
    check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0), "Begin codec streaming");
    check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), "Start codec stream");
}
void shutdown(IMFTransform* transform) noexcept {
    if (!transform) return;
    ComPtr<IMFShutdown> lifetime;
    if (SUCCEEDED(transform->QueryInterface(IID_PPV_ARGS(&lifetime)))) lifetime->Shutdown();
}
}

struct H264Encoder::State {
    Runtime runtime;
    ComPtr<ID3D11Device> device;
    ComPtr<IMFDXGIDeviceManager> manager;
    std::unique_ptr<GpuProcessor> converter;
    ComPtr<IMFTransform> transform;
    ComPtr<IMFMediaEventGenerator> events;
    unsigned width, height, fps;
    unsigned input_ready = 0, output_ready = 0;
    std::string name;
    State(unsigned w, unsigned h, unsigned f,ID3D11Device* gpu_device,unsigned requested_bitrate) :device(gpu_device), width(w), height(h), fps(f) {
        dimensions(w,h,f);
        if(device) {
            UINT token=0; check(MFCreateDXGIDeviceManager(&token,&manager),"Encoder GPU device manager");
            check(manager->ResetDevice(device.Get(),token),"Set encoder GPU device");
            converter=std::make_unique<GpuProcessor>(device.Get());
        }
        MFT_REGISTER_TYPE_INFO type{MFMediaType_Video, MFVideoFormat_H264};
        IMFActivate** entries = nullptr; UINT32 count=0;
        check(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, nullptr, &type, &entries, &count), "Enumerate hardware encoders");
        struct Free { IMFActivate** entries; UINT32 count; ~Free() { for (UINT32 i=0;i<count;++i) entries[i]->Release(); CoTaskMemFree(entries); } } free{entries,count};
        std::string failures;
        for (UINT32 i=0;i<count;++i) {
            try {
                check(entries[i]->ActivateObject(IID_PPV_ARGS(&transform)), "Activate hardware H264 encoder");
                wchar_t* friendly=nullptr; UINT32 length=0;
                if (SUCCEEDED(entries[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute,&friendly,&length))) {
                    name.clear(); for (UINT32 c=0;c<length;++c) name.push_back(friendly[c] < 128 ? static_cast<char>(friendly[c]) : '?');
                    CoTaskMemFree(friendly);
                }
                ComPtr<IMFAttributes> attributes;
                check(transform->GetAttributes(&attributes), "Encoder attributes");
                if (!MFGetAttributeUINT32(attributes.Get(), MF_TRANSFORM_ASYNC, FALSE)) throw std::runtime_error("Hardware encoder is not asynchronous");
                check(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE), "Unlock async encoder");
                if(device) {
                    if(!MFGetAttributeUINT32(attributes.Get(),MF_SA_D3D11_AWARE,FALSE)) throw std::runtime_error("Hardware encoder is not D3D11-aware");
                    check(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,reinterpret_cast<ULONG_PTR>(manager.Get())),"Set encoder D3D11 manager");
                }
                attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
                ComPtr<ICodecAPI> api;
                check(transform.As(&api), "Encoder codec properties");
                codec_bool(api.Get(), CODECAPI_AVLowLatencyMode, true, true);
                codec_uint(api.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0);
                codec_uint(api.Get(), CODECAPI_AVEncMPVGOPSize, fps);
                codec_uint(api.Get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
                const auto bitrate=requested_bitrate?requested_bitrate:static_cast<UINT32>(std::clamp<std::uint64_t>(static_cast<std::uint64_t>(width)*height*fps/8,4000000,60000000));
                codec_uint(api.Get(), CODECAPI_AVEncCommonMeanBitRate, bitrate);
                const auto output=media_type(MFVideoFormat_H264,width,height,fps);
                check(output->SetUINT32(MF_MT_AVG_BITRATE,bitrate), "H264 bitrate");
                check(output->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_Main), "H264 main profile");
                check(transform->SetOutputType(0,output.Get(),0), "Set encoder H264 output");
                const auto input=media_type(MFVideoFormat_NV12,width,height,fps);
                check(transform->SetInputType(0,input.Get(),0), "Set encoder NV12 input");
                check(transform.As(&events), "Encoder event generator");
                start_transform(transform.Get());
                return;
            } catch (const std::exception& error) {
                failures += std::string(error.what()) + "; ";
                events.Reset(); shutdown(transform.Get()); transform.Reset();
            }
        }
        throw std::runtime_error("No usable hardware H264 encoder (software fallback disabled): " + failures);
    }
    ~State() { shutdown(transform.Get()); }
    void wait_for(bool output) {
        const auto deadline=Clock::now()+std::chrono::seconds(5);
        while ((output ? output_ready : input_ready) == 0) {
            if (Clock::now() >= deadline) throw std::runtime_error(output ? "Hardware encoder output timed out" : "Hardware encoder input timed out");
            ComPtr<IMFMediaEvent> event;
            const auto result=events->GetEvent(MF_EVENT_FLAG_NO_WAIT,&event);
            if (result == MF_E_NO_EVENTS_AVAILABLE) { precise_wait(std::chrono::microseconds(250)); continue; }
            check(result,"Get encoder event");
            HRESULT status=S_OK; check(event->GetStatus(&status),"Encoder event status"); check(status,"Hardware encoder event");
            MediaEventType type=MEUnknown; check(event->GetType(&type),"Encoder event type");
            if (type == METransformNeedInput) ++input_ready;
            else if (type == METransformHaveOutput) ++output_ready;
        }
    }
};
H264Encoder::H264Encoder(unsigned w,unsigned h,unsigned fps,ID3D11Device* device,unsigned bitrate):state_(std::make_unique<State>(w,h,fps,device,bitrate)){}
H264Encoder::~H264Encoder()=default;
std::string H264Encoder::description() const { return state_->name + " hardware-MFT low-latency "+(state_->device ? "D3D11-NV12-surface-input" : "CPU-NV12-input"); }
std::vector<std::uint8_t> H264Encoder::encode(const Frame& frame,std::uint64_t sequence) {
    auto& s=*state_;
    if (frame.width!=s.width || frame.height!=s.height) throw std::runtime_error("Capture dimensions changed during H264 stream");
    ComPtr<IMFSample> sample;
    if(s.device) {
        if(!frame.gpu) throw std::runtime_error("GPU encoder requires GPU capture frame");
        D3D11_TEXTURE2D_DESC desc{}; frame.gpu->texture->GetDesc(&desc);
        auto nv12=make_video_texture(s.device.Get(),s.width,s.height,DXGI_FORMAT_NV12);
        s.converter->blit(*frame.gpu,desc.Width,desc.Height,nv12.Get());
        // Submit dependent GPU work before handing the surface to the async
        // encoder. A CPU completion wait here stalls this MFT/driver pipeline.
        s.converter->submit();
        ComPtr<IMFMediaBuffer> buffer;
        check(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),nv12.Get(),0,FALSE,&buffer),"Create encoder DXGI surface buffer");
        check(buffer->SetCurrentLength(s.width*s.height*3/2),"Set encoder GPU buffer length");
        check(MFCreateSample(&sample),"Create GPU encode sample");
        check(sample->AddBuffer(buffer.Get()),"Attach GPU encode surface");
        check(sample->SetSampleTime(static_cast<LONGLONG>(sequence)*10000000/s.fps),"GPU sample time");
        check(sample->SetSampleDuration(10000000/s.fps),"GPU sample duration");
    } else {
        if(frame.gpu) throw std::runtime_error("CPU-input encoder cannot silently read back GPU frames");
        const auto nv12=to_nv12(frame);
        sample=memory_sample(nv12,static_cast<LONGLONG>(sequence)*10000000/s.fps,10000000/s.fps);
    }
    s.wait_for(false);
    check(s.transform->ProcessInput(0,sample.Get(),0),"Submit hardware encode sample"); --s.input_ready;
    s.wait_for(true);
    Output output(s.transform.Get()); DWORD status=0;
    check(s.transform->ProcessOutput(0,1,&output.data,&status),"Read hardware H264 sample"); --s.output_ready;
    return sample_bytes(output.data.pSample);
}

struct H264Decoder::State {
    Runtime runtime;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IMFDXGIDeviceManager> manager;
    ComPtr<IMFTransform> transform;
    ComPtr<ID3D11Texture2D> staging;
    D3D11_TEXTURE2D_DESC staging_desc{};
    unsigned width,height,fps;std::uint64_t decoded=0;
    std::string adapter_name;
    bool gpu_output=false;
    std::unique_ptr<GpuProcessor> surface_barrier;
    bool drained=false;
    State(unsigned w,unsigned h,unsigned f,std::optional<unsigned> selected,bool output_gpu):width(w),height(h),fps(f),gpu_output(output_gpu) {
        dimensions(w,h,f);
        ComPtr<IDXGIAdapter1> selected_adapter;
        if (selected) {
            ComPtr<IDXGIFactory1> factory;
            check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Decoder GPU factory");
            check(factory->EnumAdapters1(*selected,&selected_adapter),"Selected decoder adapter is unavailable");
            DXGI_ADAPTER_DESC1 desc{}; check(selected_adapter->GetDesc1(&desc),"Selected decoder adapter description");
            if (desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) throw std::runtime_error("Software decoder adapter is not allowed");
        }
        check(D3D11CreateDevice(selected_adapter.Get(),selected ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Create decoder video device");
        ComPtr<IDXGIDevice> dxgi; check(device.As(&dxgi),"Decoder DXGI device");
        ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter),"Decoder adapter");
        DXGI_ADAPTER_DESC adapter_desc{}; check(adapter->GetDesc(&adapter_desc),"Decoder adapter name");
        for (const wchar_t c:adapter_desc.Description) { if (!c) break; adapter_name.push_back(c<128 ? static_cast<char>(c) : '?'); }
        ComPtr<ID3D10Multithread> threading;
        check(context.As(&threading),"Decoder multithread protection"); threading->SetMultithreadProtected(TRUE);
        UINT token=0;
        check(MFCreateDXGIDeviceManager(&token,&manager),"Create decoder device manager");
        check(manager->ResetDevice(device.Get(),token),"Assign decoder GPU");
        if(gpu_output) surface_barrier=std::make_unique<GpuProcessor>(device.Get());
        check(CoCreateInstance(CLSID_CMSH264DecoderMFT,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&transform)),"Create Microsoft H264 decoder");
        ComPtr<IMFAttributes> attributes;
        check(transform->GetAttributes(&attributes),"Decoder attributes");
        if (!MFGetAttributeUINT32(attributes.Get(),MF_SA_D3D11_AWARE,FALSE)) throw std::runtime_error("Decoder does not support D3D11");
        attributes->SetUINT32(MF_LOW_LATENCY,TRUE);
        ComPtr<ICodecAPI> api;
        check(transform.As(&api),"Decoder codec properties");
        codec_uint(api.Get(),CODECAPI_AVLowLatencyMode,1,true);
        // D3D11 acceleration is enabled by the device-manager message, not the
        // similarly named DirectShow-only AVDecVideoAcceleration_H264 property.
        codec_uint(api.Get(),CODECAPI_AVDecVideoMaxCodedWidth,width,true);
        codec_uint(api.Get(),CODECAPI_AVDecVideoMaxCodedHeight,height,true);
        check(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,reinterpret_cast<ULONG_PTR>(manager.Get())),"Set decoder D3D11 manager");
        if(gpu_output) {
            ComPtr<IMFAttributes> output_attributes;
            check(transform->GetOutputStreamAttributes(0,&output_attributes),"Decoder GPU output attributes");
            check(output_attributes->SetUINT32(MF_SA_D3D11_SHARED_WITHOUT_MUTEX,TRUE),"Decoder shared GPU output");
            check(output_attributes->SetUINT32(MF_SA_D3D11_BINDFLAGS,D3D11_BIND_SHADER_RESOURCE),"Decoder GPU output binding");
        }
        const auto input=media_type(MFVideoFormat_H264,width,height,fps);
        check(transform->SetInputType(0,input.Get(),0),"Set decoder H264 input");
        output_type();
        start_transform(transform.Get());
    }
    ~State(){shutdown(transform.Get());}
    void output_type() {
        for (DWORD i=0;i<64;++i) {
            ComPtr<IMFMediaType> type;
            const auto result=transform->GetOutputAvailableType(0,i,&type);
            if (result==MF_E_NO_MORE_TYPES) break;
            check(result,"Enumerate decoder output");
            GUID subtype{}; check(type->GetGUID(MF_MT_SUBTYPE,&subtype),"Decoder output subtype");
            if (subtype!=MFVideoFormat_NV12) continue;
            UINT32 w=0,h=0;
            check(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h),"Decoder output size");
            // Hardware surfaces may pad the coded height; cropping uses the negotiated dimensions.
            if (w<width || h<height || w>width+32 || h>height+32) throw std::runtime_error("Decoder output dimensions changed or exceed negotiated geometry");
            check(transform->SetOutputType(0,type.Get(),0),"Set decoder NV12 output");
            return;
        }
        throw std::runtime_error("Decoder has no NV12 output");
    }
    Frame pixels(IMFSample* sample) {
        if (!sample) throw std::runtime_error("Decoder returned no sample");
        ComPtr<IMFMediaBuffer> buffer;
        check(sample->GetBufferByIndex(0,&buffer),"Decoded media buffer");
        ComPtr<IMFDXGIBuffer> gpu;
        check(buffer.As(&gpu),"Hardware decode required: expected a D3D11 surface");
        ComPtr<ID3D11Texture2D> texture; UINT slice=0;
        check(gpu->GetResource(IID_PPV_ARGS(&texture)),"Decoded GPU texture");
        check(gpu->GetSubresourceIndex(&slice),"Decoded GPU slice");
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        if (desc.Format!=DXGI_FORMAT_NV12 || desc.Width<width || desc.Height<height || desc.Width>width+32 || desc.Height>height+32) throw std::runtime_error("Unsupported decoded GPU surface dimensions/format");
        if(gpu_output) {
            Frame frame{width,height,{}};
            frame.gpu=std::make_shared<GpuFrame>(); frame.gpu->device=device;
            // Detach the pooled array slice into owned NV12 memory on the GPU.
            // A latest-frame queue can then drop frames without retaining the
            // decoder's sample pool. Complete the copy before returning it.
            frame.gpu->texture=make_video_texture(device.Get(),desc.Width,desc.Height,DXGI_FORMAT_NV12);
            context->CopySubresourceRegion(frame.gpu->texture.Get(),0,0,0,0,texture.Get(),slice,nullptr);
            surface_barrier->wait_idle();
            ++decoded;
            return frame;
        }
        if (!staging || staging_desc.Width!=desc.Width || staging_desc.Height!=desc.Height) {
            desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            desc.BindFlags=0; desc.MiscFlags=0; desc.ArraySize=1; desc.MipLevels=1;
            check(device->CreateTexture2D(&desc,nullptr,&staging),"Create decoded staging surface");
            staging_desc=desc;
        }
        context->CopySubresourceRegion(staging.Get(),0,0,0,0,texture.Get(),slice,nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map decoded video surface");
        struct Unmap { ID3D11DeviceContext* context; ID3D11Texture2D* texture; ~Unmap(){context->Unmap(texture,0);} } unmap{context.Get(),staging.Get()};
        const auto* yplane=static_cast<const std::uint8_t*>(mapped.pData);
        const auto* uvplane=yplane+static_cast<std::size_t>(mapped.RowPitch)*staging_desc.Height;
        Frame frame{width,height,{}}; frame.bgra.resize(static_cast<std::size_t>(width)*height*4);
        for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
            const int c=static_cast<int>(yplane[static_cast<std::size_t>(y)*mapped.RowPitch+x])-16;
            const auto uv=static_cast<std::size_t>(y/2)*mapped.RowPitch+(x&~1u);
            const int d=static_cast<int>(uvplane[uv])-128, e=static_cast<int>(uvplane[uv+1])-128;
            const auto out=(static_cast<std::size_t>(y)*width+x)*4;
            frame.bgra[out]=clamp((298*c+541*d+128)>>8);
            frame.bgra[out+1]=clamp((298*c-55*d-136*e+128)>>8);
            frame.bgra[out+2]=clamp((298*c+459*e+128)>>8); frame.bgra[out+3]=255;
        }
        ++decoded;
        return frame;
    }
    std::vector<Frame> pull() {
        std::vector<Frame> frames;
        for (unsigned attempts=0;attempts<32;++attempts) {
            Output output(transform.Get(),width*height*3/2); DWORD status=0;
            const auto result=transform->ProcessOutput(0,1,&output.data,&status);
            if (result==MF_E_TRANSFORM_NEED_MORE_INPUT) return frames;
            if (result==MF_E_TRANSFORM_STREAM_CHANGE) { output_type(); continue; }
            check(result,"Decode H264 output");
            frames.push_back(pixels(output.data.pSample));
        }
        throw std::runtime_error("Decoder produced excessive output without more input");
    }
};
H264Decoder::H264Decoder(unsigned w,unsigned h,unsigned fps,std::optional<unsigned> adapter,bool gpu_output):state_(std::make_unique<State>(w,h,fps,adapter,gpu_output)){}
H264Decoder::~H264Decoder()=default;
std::vector<Frame> H264Decoder::decode(std::span<const std::uint8_t> bytes,std::uint64_t sequence) {
    auto& s=*state_;
    if (s.drained || bytes.empty() || bytes.size()>2*1024*1024) throw std::runtime_error("Invalid H264 access unit or decoder state");
    const auto sample=memory_sample(bytes,static_cast<LONGLONG>(sequence)*10000000/s.fps,10000000/s.fps);
    check(s.transform->ProcessInput(0,sample.Get(),0),"Submit H264 decode sample");
    return s.pull();
}
std::vector<Frame> H264Decoder::finish() {
    auto& s=*state_;
    if (s.drained) throw std::runtime_error("Decoder already drained");
    s.drained=true;
    check(s.transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0),"End H264 input");
    check(s.transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0),"Drain H264 decoder");
    return s.pull();
}
std::uint64_t H264Decoder::gpu_frames() const {return state_->decoded;}
std::string H264Decoder::description() const {return "Microsoft H264 Decoder MFT D3D11-manager NV12-DXGI-surfaces adapter="+state_->adapter_name;}

void h264_roundtrip_check() {
    H264Encoder encoder(640,360,30);
    H264Decoder decoder(640,360,30);
    std::cout << "ENCODER " << encoder.description() << std::endl;
    std::cout << "DECODER " << decoder.description() << std::endl;
    unsigned decoded=0; std::uint64_t bytes=0;
    const std::array<std::array<std::uint8_t,3>,4> colors{{{30,100,200},{180,80,40},{40,200,70},{150,40,170}}};
    auto validate=[&](const Frame& frame) {
        const auto i=(static_cast<std::size_t>(frame.height/2)*frame.width+frame.width/2)*4;
        const auto expected=colors[decoded%colors.size()];
        for (unsigned c=0;c<3;++c) if (std::abs(static_cast<int>(frame.bgra[i+c])-expected[c])>12) throw std::runtime_error("H264 roundtrip color/order check failed");
        ++decoded;
    };
    Frame source{640,360,{}}; source.bgra.resize(640*360*4);
    for (unsigned i=0;i<16;++i) {
        const auto color=colors[i%colors.size()];
        for (std::size_t pixel=0;pixel<source.bgra.size();pixel+=4){source.bgra[pixel]=color[0];source.bgra[pixel+1]=color[1];source.bgra[pixel+2]=color[2];source.bgra[pixel+3]=255;}
        const auto packet=encoder.encode(source,i); bytes+=packet.size();
        for (const auto& frame:decoder.decode(packet,i)) validate(frame);
    }
    for (const auto& frame:decoder.finish()) validate(frame);
    if (decoded!=16 || decoder.gpu_frames()!=16) throw std::runtime_error("H264 roundtrip frame count differs");
    std::cout << "H264_ROUNDTRIP_OK encoded_frames=16 decoded_frames=" << decoded << " gpu_surface_frames=" << decoder.gpu_frames() << " encoded_bytes=" << bytes << std::endl;
}

void gpu_h264_roundtrip_check() {
    Runtime runtime;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"GPU test device");
    ComPtr<ID3D10Multithread> threading; check(context.As(&threading),"GPU test multithread protection"); threading->SetMultithreadProtected(TRUE);
    H264Encoder encoder(640,360,30,device.Get());
    H264Decoder decoder(640,360,30,std::nullopt,true);
    Frame source{640,360,{}};
    source.gpu=std::make_shared<GpuFrame>(); source.gpu->device=device;
    source.gpu->texture=make_video_texture(device.Get(),1280,720,DXGI_FORMAT_B8G8R8A8_UNORM);
    const std::array<std::array<std::uint8_t,3>,4> colors{{{30,100,200},{180,80,40},{40,200,70},{150,40,170}}};
    std::vector<std::uint8_t> pixels(1280*720*4);
    unsigned decoded=0; std::uint64_t bytes=0;
    auto validate=[&](const Frame& gpu) {
        if(!gpu.bgra.empty() || !gpu.gpu) throw std::runtime_error("GPU decoder silently produced CPU pixels");
        const auto frame=readback_gpu_bgra(gpu);
        for(unsigned quadrant=0;quadrant<4;++quadrant) {
            const auto x=frame.width*(quadrant%2 ? 3 : 1)/4;
            const auto y=frame.height*(quadrant/2 ? 3 : 1)/4;
            const auto i=(static_cast<std::size_t>(y)*frame.width+x)*4;
            const auto expected=colors[(decoded+quadrant)%colors.size()];
            for(unsigned c=0;c<3;++c) if(std::abs(static_cast<int>(frame.bgra[i+c])-expected[c])>12) throw std::runtime_error("GPU scaling/color/orientation/order check failed");
        }
        ++decoded;
    };
    std::cout << "ENCODER " << encoder.description() << std::endl;
    std::cout << "DECODER " << decoder.description() << std::endl;
    for(unsigned i=0;i<16;++i) {
        for(unsigned y=0;y<720;++y) for(unsigned x=0;x<1280;++x) {
            const auto color=colors[(i+(x>=640 ? 1 : 0)+(y>=360 ? 2 : 0))%colors.size()];
            const auto j=(static_cast<std::size_t>(y)*1280+x)*4;
            pixels[j]=color[0];pixels[j+1]=color[1];pixels[j+2]=color[2];pixels[j+3]=255;
        }
        context->UpdateSubresource(source.gpu->texture.Get(),0,nullptr,pixels.data(),1280*4,0);
        const auto packet=encoder.encode(source,i); bytes+=packet.size();
        for(const auto& frame:decoder.decode(packet,i)) validate(frame);
    }
    for(const auto& frame:decoder.finish()) validate(frame);
    if(decoded!=16) throw std::runtime_error("GPU roundtrip lost frames");
    std::cout << "GPU_H264_ROUNDTRIP_OK frames=" << decoded << " encoded_bytes=" << bytes << " test_only_readbacks=16 production_raw_pixel_readbacks=0" << std::endl;
}
}
