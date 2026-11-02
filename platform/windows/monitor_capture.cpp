#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <dm/monitor_capture.hpp>
#include <dm/video_gpu.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace dm::video {
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
struct MonitorCapture::State {
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    event_token closed_token{};
    bool registered=false;
    winrt::Windows::Graphics::SizeInt32 initial_size{};
    std::shared_ptr<std::atomic_bool> closed=std::make_shared<std::atomic_bool>(false);
    Frame last;
    unsigned max_width=1280,max_height=720;
    ~State() {
        try { if(registered) item.Closed(closed_token); } catch(const hresult_error&) {}
        try { if(session) session.Close(); } catch(const hresult_error&) {}
        try { if(pool) pool.Close(); } catch(const hresult_error&) {}
    }
};
MonitorCapture::MonitorCapture(ID3D11Device* device,const std::wstring& source,unsigned width,unsigned height) try :state_(std::make_unique<State>()) {
    auto& s=*state_;
    s.max_width=width;s.max_height=height;
    if(!GraphicsCaptureSession::IsSupported()) throw std::runtime_error("Windows Graphics Capture is unavailable");
    struct Lookup { const std::wstring& source; HMONITOR monitor=nullptr; } lookup{source};
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM param)->BOOL {
        auto& lookup=*reinterpret_cast<Lookup*>(param);
        MONITORINFOEXW info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoW(monitor,&info) && lookup.source==info.szDevice) lookup.monitor=monitor;
        return TRUE;
    },reinterpret_cast<LPARAM>(&lookup));
    if(!lookup.monitor) throw std::runtime_error("Selected monitor is not active; capture will not fall back to another screen");
    s.device.copy_from(device);device->GetImmediateContext(s.context.put());
    auto dxgi=s.device.as<IDXGIDevice>();
    com_ptr<IInspectable> inspectable;
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),inspectable.put()));
    const auto direct_device=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    auto interop=get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
    // An indirect monitor can be enumerated before WGC accepts its HMONITOR.
    // Retry this specific startup race, re-resolving the same named monitor.
    const auto capture_ready=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    for(;;) {
        const auto result=interop->CreateForMonitor(lookup.monitor,guid_of<GraphicsCaptureItem>(),put_abi(s.item));
        if(SUCCEEDED(result))break;
        if(result!=E_INVALIDARG || std::chrono::steady_clock::now()>=capture_ready)check_hresult(result);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        lookup.monitor=nullptr;
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM param)->BOOL {
            auto& current=*reinterpret_cast<Lookup*>(param);MONITORINFOEXW info{};info.cbSize=sizeof(info);
            if(GetMonitorInfoW(monitor,&info) && current.source==info.szDevice)current.monitor=monitor;return TRUE;
        },reinterpret_cast<LPARAM>(&lookup));
        if(!lookup.monitor)throw std::runtime_error("Selected monitor disappeared during capture startup");
    }
    s.closed_token=s.item.Closed([closed=s.closed](auto&&,auto&&) { closed->store(true); });s.registered=true;
    s.initial_size=s.item.Size();
    s.pool=Direct3D11CaptureFramePool::CreateFreeThreaded(direct_device,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,s.initial_size);
    s.session=s.pool.CreateCaptureSession(s.item);
    s.session.StartCapture();
} catch(const hresult_error& error) {
    throw std::runtime_error("Start selected-monitor capture: "+to_string(error.message())+" HRESULT="+std::to_string(static_cast<unsigned long>(error.code().value)));
}
MonitorCapture::~MonitorCapture()=default;
Frame MonitorCapture::next_gpu() try {
    auto& s=*state_;
    check_hresult(s.device->GetDeviceRemovedReason());
    if(s.closed->load()) throw std::runtime_error("Selected capture monitor was disconnected");
    auto captured=s.pool.TryGetNextFrame();
    if(!captured && s.last.gpu) return s.last;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!captured && std::chrono::steady_clock::now()<until) {
        if(s.closed->load()) throw std::runtime_error("Selected capture monitor was disconnected");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));captured=s.pool.TryGetNextFrame();
    }
    if(!captured) throw std::runtime_error("Timed out waiting for selected monitor's first GPU frame");
    // Drain at most the two-slot pool; never wait for future frames in this loop.
    for(unsigned i=0;i<2;++i) {
        auto newer=s.pool.TryGetNextFrame();
        if(!newer) break;
        captured.Close();captured=std::move(newer);
    }
    const auto size=captured.ContentSize();
    const auto initial=s.initial_size;
    if(size.Width!=initial.Width || size.Height!=initial.Height) throw std::runtime_error("Capture size changed; restart the session");
    auto access=captured.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    com_ptr<ID3D11Texture2D> source;check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),source.put_void()));
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    if(size.Width<=0 || size.Height<=0 || desc.Width!=static_cast<UINT>(size.Width) || desc.Height!=static_cast<UINT>(size.Height) || desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM || static_cast<std::uint64_t>(desc.Width)*desc.Height>33554432)
        throw std::runtime_error("Unsupported selected-monitor GPU texture");
    if(!s.last.gpu) {
        const double scale=std::min({1.0,static_cast<double>(s.max_width)/desc.Width,static_cast<double>(s.max_height)/desc.Height});
        s.last.width=std::max(2u,static_cast<UINT>(desc.Width*scale)&~1u);
        s.last.height=std::max(2u,static_cast<UINT>(desc.Height*scale)&~1u);
        s.last.gpu=std::make_shared<GpuFrame>();
        s.last.gpu->device=s.device.get();
        s.last.gpu->texture=make_video_texture(s.device.get(),desc.Width,desc.Height,desc.Format);
    }
    s.context->CopyResource(s.last.gpu->texture.Get(),source.get());
    // Submit the copy before returning the WGC pool surface. All subsequent
    // conversion commands use this same immediate context in submission order.
    s.context->Flush();
    captured.Close();
    ++s.last.capture_generation;
    return s.last;
} catch(const hresult_error& error) {
    throw std::runtime_error("Capture selected monitor: "+to_string(error.message())+" HRESULT="+std::to_string(static_cast<unsigned long>(error.code().value)));
}
}
