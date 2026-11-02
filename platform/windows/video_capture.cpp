#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mferror.h>
#include <dm/video.hpp>
#include <dm/video_gpu.hpp>
#include <dm/monitor_capture.hpp>
#include <d3d10.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace dm::video {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr, const char* stage) {
    if (FAILED(hr)) throw std::runtime_error(std::string(stage) + " failed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ")");
}
class Com {
public:
    Com() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize COM"); }
    ~Com() { CoUninitialize(); }
};
ComPtr<IWICImagingFactory> factory() {
    ComPtr<IWICImagingFactory> result;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&result)), "WIC factory");
    return result;
}
}
struct DesktopCapture::State {
    unsigned max_width,max_height;
    Com com;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging;
    D3D11_TEXTURE2D_DESC desc{};
    Frame last;
    Frame last_gpu;
    std::unique_ptr<MonitorCapture> monitor_capture;
    explicit State(const std::wstring& source,unsigned width,unsigned height):max_width(width),max_height(height) {
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context), "Create capture GPU device");
        ComPtr<ID3D10Multithread> threading;
        check(context.As(&threading),"Capture GPU multithread protection"); threading->SetMultithreadProtected(TRUE);
        if(!source.empty()) { monitor_capture=std::make_unique<MonitorCapture>(device.Get(),source,width,height);return; }
        ComPtr<IDXGIDevice> dxgi;
        check(device.As(&dxgi), "Capture DXGI device");
        ComPtr<IDXGIAdapter> adapter;
        check(dxgi->GetAdapter(&adapter), "Capture adapter");
        ComPtr<IDXGIOutput> selected;
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(index, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC output_desc{};
            check(output->GetDesc(&output_desc), "Read output description");
            if (!output_desc.AttachedToDesktop) continue;
            if (!selected) selected = output;
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(output_desc.Monitor, &monitor) && (monitor.dwFlags & MONITORINFOF_PRIMARY)) { selected = output; break; }
        }
        if (!selected) throw std::runtime_error("No active monitor on the capture adapter");
        DXGI_OUTPUT_DESC output_desc{};
        check(selected->GetDesc(&output_desc), "Read selected output");
        if (output_desc.Rotation != DXGI_MODE_ROTATION_IDENTITY) throw std::runtime_error("Rotated monitors are not supported by this first capture spike");
        ComPtr<IDXGIOutput1> output1;
        check(selected.As(&output1), "Desktop duplication interface");
        check(output1->DuplicateOutput(device.Get(), &duplication), "Duplicate desktop (requires an unlocked interactive session)");
    }
};
DesktopCapture::DesktopCapture(const std::wstring& source,unsigned width,unsigned height) : state_(std::make_unique<State>(source,width,height)) {}
DesktopCapture::~DesktopCapture() = default;
Frame DesktopCapture::next_gpu() {
    auto& s=*state_;
    if(s.monitor_capture) return s.monitor_capture->next_gpu();
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    const auto result=s.duplication->AcquireNextFrame(s.last_gpu.gpu ? 0 : 500,&info,&resource);
    if(result==DXGI_ERROR_WAIT_TIMEOUT && s.last_gpu.gpu) return s.last_gpu;
    check(result,"Acquire GPU desktop frame");
    struct Release{IDXGIOutputDuplication* duplication;~Release(){duplication->ReleaseFrame();}} release{s.duplication.Get()};
    ComPtr<ID3D11Texture2D> source; check(resource.As(&source),"GPU desktop texture");
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    if(desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM || static_cast<std::uint64_t>(desc.Width)*desc.Height>33554432) throw std::runtime_error("Unsupported GPU capture format/dimensions");
    D3D11_TEXTURE2D_DESC cached{};
    if(s.last_gpu.gpu) s.last_gpu.gpu->texture->GetDesc(&cached);
    if(!s.last_gpu.gpu || cached.Width!=desc.Width || cached.Height!=desc.Height) {
        const double scale=std::min({1.0,static_cast<double>(s.max_width)/desc.Width,static_cast<double>(s.max_height)/desc.Height});
        s.last_gpu.width=std::max(2u,static_cast<UINT>(desc.Width*scale)&~1u);
        s.last_gpu.height=std::max(2u,static_cast<UINT>(desc.Height*scale)&~1u);
        s.last_gpu.gpu=std::make_shared<GpuFrame>();
        s.last_gpu.gpu->device=s.device;
        s.last_gpu.gpu->texture=make_video_texture(s.device.Get(),desc.Width,desc.Height,DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    // GPU copy owns the duplicated frame after ReleaseFrame. Reused only by the
    // serial capture->convert->encode loop after its preceding output completes.
    s.context->CopyResource(s.last_gpu.gpu->texture.Get(),source.Get());
    ++s.last_gpu.capture_generation;
    return s.last_gpu;
}
Frame DesktopCapture::next() {
    auto& s = *state_;
    if(s.monitor_capture) throw std::runtime_error("Selected monitor capture requires H264 GPU mode");
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    const HRESULT result = s.duplication->AcquireNextFrame(s.last.bgra.empty() ? 500 : 0, &info, &resource);
    if (result == DXGI_ERROR_WAIT_TIMEOUT && !s.last.bgra.empty()) return s.last;
    check(result, "Acquire desktop frame");
    struct Release { IDXGIOutputDuplication* duplication; ~Release() { duplication->ReleaseFrame(); } } release{s.duplication.Get()};
    ComPtr<ID3D11Texture2D> texture;
    check(resource.As(&texture), "Desktop texture");
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || static_cast<std::uint64_t>(desc.Width) * desc.Height > 33554432) throw std::runtime_error("Unsupported desktop format or dimensions");
    if (!s.staging || desc.Width != s.desc.Width || desc.Height != s.desc.Height) {
        s.desc = desc;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(s.device->CreateTexture2D(&desc, nullptr, &s.staging), "Create staging texture");
    }
    s.context->CopyResource(s.staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(s.context->Map(s.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map capture frame");
    struct Unmap { ID3D11DeviceContext* context; ID3D11Texture2D* texture; ~Unmap() { context->Unmap(texture, 0); } } unmap{s.context.Get(), s.staging.Get()};
    // Keep this feasibility path bounded; production encoding will avoid this CPU readback.
    Frame raw{s.desc.Width, s.desc.Height, {}};
    raw.bgra.resize(static_cast<std::size_t>(raw.width) * raw.height * 4);
    for (UINT row = 0; row < raw.height; ++row) std::memcpy(raw.bgra.data() + static_cast<std::size_t>(row) * raw.width * 4, static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, static_cast<std::size_t>(raw.width) * 4);
    const auto wic = factory();
    ComPtr<IWICBitmap> bitmap;
    check(wic->CreateBitmapFromMemory(raw.width, raw.height, GUID_WICPixelFormat32bppBGRA, raw.width * 4, static_cast<UINT>(raw.bgra.size()), raw.bgra.data(), &bitmap), "Capture bitmap");
    const double scale = std::min({1.0, 1280.0 / raw.width, 720.0 / raw.height});
    const UINT width = std::max(2u, static_cast<UINT>(raw.width * scale) & ~1u);
    const UINT height = std::max(2u, static_cast<UINT>(raw.height * scale) & ~1u);
    ComPtr<IWICBitmapScaler> scaler;
    check(wic->CreateBitmapScaler(&scaler), "Create scaler");
    check(scaler->Initialize(bitmap.Get(), width, height, WICBitmapInterpolationModeLinear), "Scale capture");
    Frame frame{width, height, {}};
    frame.bgra.resize(static_cast<std::size_t>(width) * height * 4);
    check(scaler->CopyPixels(nullptr, width * 4, static_cast<UINT>(frame.bgra.size()), frame.bgra.data()), "Copy scaled frame");
    s.last = frame;
    return frame;
}

std::vector<std::uint8_t> encode_jpeg(const Frame& frame) {
    if (!frame.width || !frame.height || frame.width > 1280 || frame.height > 720 ||
        frame.bgra.size() != static_cast<std::size_t>(frame.width) * frame.height * 4) throw std::runtime_error("Invalid source frame dimensions or pixel data");
    Com com;
    const auto wic = factory();
    ComPtr<IWICBitmap> bitmap;
    check(wic->CreateBitmapFromMemory(frame.width, frame.height, GUID_WICPixelFormat32bppBGRA, frame.width * 4, static_cast<UINT>(frame.bgra.size()), const_cast<BYTE*>(frame.bgra.data()), &bitmap), "JPEG source");
    ComPtr<IWICFormatConverter> converter;
    check(wic->CreateFormatConverter(&converter), "JPEG converter");
    check(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert JPEG pixels");
    ComPtr<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "JPEG memory stream");
    ComPtr<IWICBitmapEncoder> encoder;
    check(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder), "JPEG encoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize JPEG encoder");
    ComPtr<IWICBitmapFrameEncode> encoded;
    ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(&encoded, &properties), "JPEG frame");
    PROPBAG2 property{};
    property.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT quality{};
    quality.vt = VT_R4;
    quality.fltVal = 0.65f;
    check(properties->Write(1, &property, &quality), "JPEG quality");
    check(encoded->Initialize(properties.Get()), "Initialize JPEG frame");
    check(encoded->SetSize(frame.width, frame.height), "JPEG size");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    check(encoded->SetPixelFormat(&format), "JPEG pixel format");
    check(encoded->WriteSource(converter.Get(), nullptr), "Encode JPEG");
    check(encoded->Commit(), "Commit JPEG frame");
    check(encoder->Commit(), "Commit JPEG");
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME), "JPEG size");
    if (stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > 2 * 1024 * 1024) throw std::runtime_error("Encoded frame exceeds prototype limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "JPEG seek");
    ULONG read = 0;
    check(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read), "JPEG read");
    if (read != bytes.size()) throw std::runtime_error("Truncated encoded JPEG");
    return bytes;
}

Frame decode_jpeg(const std::vector<std::uint8_t>& jpeg, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || width > 1280 || height > 720 || jpeg.empty() || jpeg.size() > 2 * 1024 * 1024) throw std::runtime_error("Video frame exceeds prototype limits");
    Com com;
    const auto wic = factory();
    ComPtr<IWICStream> stream;
    check(wic->CreateStream(&stream), "Decode stream");
    check(stream->InitializeFromMemory(const_cast<BYTE*>(jpeg.data()), static_cast<DWORD>(jpeg.size())), "Decode JPEG memory");
    ComPtr<IWICBitmapDecoder> decoder;
    check(wic->CreateDecoder(GUID_ContainerFormatJpeg, nullptr, &decoder), "Create JPEG decoder");
    check(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand), "Initialize JPEG decoder");
    ComPtr<IWICBitmapFrameDecode> source;
    check(decoder->GetFrame(0, &source), "Decode JPEG frame");
    UINT actual_width = 0, actual_height = 0;
    check(source->GetSize(&actual_width, &actual_height), "JPEG dimensions");
    if (width != actual_width || height != actual_height) throw std::runtime_error("JPEG dimensions differ from negotiated video dimensions");
    ComPtr<IWICFormatConverter> converter;
    check(wic->CreateFormatConverter(&converter), "Decode converter");
    check(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Decode BGRA");
    Frame result{width, height, {}};
    result.bgra.resize(static_cast<std::size_t>(width) * height * 4);
    check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(result.bgra.size()), result.bgra.data()), "Copy decoded pixels");
    return result;
}

void hardware_info() {
    Com com;
    ComPtr<ID3D11Device> device;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr), "Create GPU device");
    ComPtr<IDXGIDevice> dxgi;
    check(device.As(&dxgi), "DXGI device");
    ComPtr<IDXGIAdapter> adapter;
    check(dxgi->GetAdapter(&adapter), "GPU adapter");
    DXGI_ADAPTER_DESC desc{};
    check(adapter->GetDesc(&desc), "GPU description");
    std::wcout << L"GPU: " << desc.Description << L'\n';
    ComPtr<IDXGIFactory1> gpu_factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&gpu_factory)),"Enumerate GPU factory");
    for (UINT index=0;index<32;++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const auto result=gpu_factory->EnumAdapters1(index,&candidate);
        if (result==DXGI_ERROR_NOT_FOUND) break;
        check(result,"Enumerate GPU adapter");
        DXGI_ADAPTER_DESC1 gpu_desc{}; check(candidate->GetDesc1(&gpu_desc),"GPU inventory description");
        if (gpu_desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        unsigned outputs=0;
        for (UINT output_index=0;output_index<32;++output_index) {
            ComPtr<IDXGIOutput> output;
            const auto output_result=candidate->EnumOutputs(output_index,&output);
            if (output_result==DXGI_ERROR_NOT_FOUND) break;
            check(output_result,"Enumerate GPU outputs");
            DXGI_OUTPUT_DESC output_desc{}; check(output->GetDesc(&output_desc),"GPU output inventory");
            if (output_desc.AttachedToDesktop) ++outputs;
        }
        bool h264=false;
        ComPtr<ID3D11Device> video_device;
        if (SUCCEEDED(D3D11CreateDevice(candidate.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&video_device,nullptr,nullptr))) {
            ComPtr<ID3D11VideoDevice> capabilities;
            if (SUCCEEDED(video_device.As(&capabilities))) {
                D3D11_VIDEO_DECODER_DESC decode_desc{D3D11_DECODER_PROFILE_H264_VLD_NOFGT,1280,720,DXGI_FORMAT_NV12};
                UINT count=0;
                h264=SUCCEEDED(capabilities->GetVideoDecoderConfigCount(&decode_desc,&count)) && count>0;
            }
        }
        std::wcout << L"ADAPTER index=" << index << L" name=" << gpu_desc.Description
                   << L" dedicated_MiB=" << gpu_desc.DedicatedVideoMemory/(1024*1024)
                   << L" desktop_outputs=" << outputs << L" H264_NV12_720p_config=" << (h264 ? L"yes" : L"no") << L'\n';
    }
    check(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET), "Initialize Media Foundation");
    struct Shutdown { ~Shutdown() { MFShutdown(); } } shutdown;
    MFT_REGISTER_TYPE_INFO type{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** encoders = nullptr;
    UINT32 count = 0;
    check(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, nullptr, &type, &encoders, &count), "Enumerate hardware H264 encoders");
    struct Free { IMFActivate** entries; UINT32 count; ~Free() { for (UINT32 i = 0; i < count; ++i) entries[i]->Release(); CoTaskMemFree(entries); } } free{encoders, count};
    std::wcout << L"Hardware H264 encoder registrations: " << count << L'\n';
    for (UINT32 i = 0; i < count; ++i) {
        wchar_t* name = nullptr;
        UINT32 length = 0;
        if (SUCCEEDED(encoders[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &length))) {
            std::wcout << L"  " << name << L'\n';
            CoTaskMemFree(name);
        }
    }
    std::wcout << L"Registrations are not proof of encoding. Run codec-check for a hardware roundtrip.\n";
}
}
