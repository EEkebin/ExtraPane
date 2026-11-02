#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <dm/video.hpp>
#include <dm/video_gpu.hpp>
#include <dm/input.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <stdexcept>
#include <iostream>

namespace dm::video {
namespace {
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if(message==WM_NCCREATE)SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
    if(message==WM_KEYDOWN && wparam==VK_F9) {DestroyWindow(hwnd);return 0;}
    auto* input=reinterpret_cast<input::ReceiverInput*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(input && input->message(hwnd,message,wparam,lparam))return 0;
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
void check(HRESULT hr, const char* message) { if (FAILED(hr)) throw std::runtime_error(message); }
struct DestroyWindowOnExit { HWND hwnd; ~DestroyWindowOnExit() { if (IsWindow(hwnd)) DestroyWindow(hwnd); } };
}
void preview_video(const net::Options& options, const std::string& pair,std::optional<unsigned> decoder_adapter,bool control,bool fullscreen) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    input::ReceiverInput input_capture(control);
    WNDCLASSW cls{};
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"DMVideoFeasibilityPreview";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Register preview window failed");
    const HWND hwnd = CreateWindowExW(0, cls.lpszClassName, control?L"DM control - click/tap to focus; F8 pauses":L"DM video test - encrypted, view only", WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT, CW_USEDEFAULT, 960, 600, nullptr, nullptr, cls.hInstance, &input_capture);
    if (!hwnd) throw std::runtime_error("Create preview window failed");
    DestroyWindowOnExit window{hwnd};
    if(fullscreen) {
        MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor))throw std::runtime_error("Cannot read fullscreen monitor bounds");
        SetWindowLongPtrW(hwnd,GWL_STYLE,WS_POPUP);
        SetWindowPos(hwnd,nullptr,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOZORDER);
    }
    ShowWindow(hwnd, SW_SHOWNORMAL);
    // Test launcher hides its PowerShell parent; explicitly show the native preview.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    std::unique_ptr<GpuProcessor> gpu_processor;
    std::mutex mutex;
    std::optional<Frame> latest;
    std::exception_ptr error;
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::jthread worker([&] {
        try {
            receive_video(options, pair, [&](Frame frame) {
                if (cancel.load()) throw std::runtime_error("Video preview closed by user");
                std::lock_guard guard(mutex);
                latest = std::move(frame); // A single latest frame, never an unbounded UI queue.
            },decoder_adapter,control?std::function<std::vector<input::Event>()>([&]{return input_capture.drain();}):std::function<std::vector<input::Event>()>{},[&](unsigned caps){input_capture.grant(caps);std::cout<<"INPUT_CAPTURE caps="<<caps<<" digitizer="<<GetSystemMetrics(SM_DIGITIZER)<<" pen_axes=per-sample-validity"<<std::endl;});
        } catch (...) { error = std::current_exception(); }
        done.store(true);
    });
    std::uint64_t presented = 0;
    std::uint64_t gpu_presented=0,cpu_uploads=0;
    try {
        for (;;) {
            input_capture.pump();
            MSG msg{};
            bool closing = false;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { closing = true; break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (closing) { cancel.store(true); break; }
            std::optional<Frame> frame;
            {
                std::lock_guard guard(mutex);
                frame.swap(latest);
            }
            if (frame) {
                input_capture.dimensions(frame->width,frame->height);
                if (!swap) {
                    DXGI_SWAP_CHAIN_DESC desc{};
                    desc.BufferDesc.Width = frame->width; desc.BufferDesc.Height = frame->height;
                    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    desc.SampleDesc.Count = 1;
                    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                    desc.BufferCount = 2; desc.OutputWindow = hwnd; desc.Windowed = TRUE;
                    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                    if(frame->gpu) {
                        device=frame->gpu->device;
                        device->GetImmediateContext(&context);
                        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
                        Microsoft::WRL::ComPtr<IDXGIAdapter> gpu_adapter;
                        Microsoft::WRL::ComPtr<IDXGIFactory> factory;
                        check(device.As(&dxgi_device),"Shared preview DXGI device");
                        check(dxgi_device->GetAdapter(&gpu_adapter),"Shared preview adapter");
                        check(gpu_adapter->GetParent(IID_PPV_ARGS(&factory)),"Shared preview DXGI factory");
                        check(factory->CreateSwapChain(device.Get(),&desc,&swap),"Create decoder-device swapchain");
                        gpu_processor=std::make_unique<GpuProcessor>(device.Get());
                    } else check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &desc, &swap, &device, nullptr, &context), "Create Direct3D preview failed");
                    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
                    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
                    check(device.As(&dxgi),"Preview DXGI device failed");
                    check(dxgi->GetAdapter(&adapter),"Preview GPU adapter failed");
                    DXGI_ADAPTER_DESC gpu{}; check(adapter->GetDesc(&gpu),"Preview GPU name failed");
                    std::string name;
                    for(const wchar_t c:gpu.Description){if(!c)break;name.push_back(c<128 ? static_cast<char>(c) : '?');}
                    std::cout << "PREVIEW_RENDERER adapter=" << name << std::endl;
                }
                check(swap->GetBuffer(0, IID_PPV_ARGS(&backbuffer)), "Get preview backbuffer failed");
                if(frame->gpu) {
                    if(!gpu_processor || !frame->bgra.empty()) throw std::runtime_error("Invalid GPU preview frame");
                    gpu_processor->blit(*frame->gpu,frame->width,frame->height,backbuffer.Get());
                    // Bound queued GPU work and keep this owned NV12 texture
                    // alive until the shader finishes reading it.
                    gpu_processor->wait_idle();
                    ++gpu_presented;
                } else {
                    context->UpdateSubresource(backbuffer.Get(), 0, nullptr, frame->bgra.data(), frame->width * 4, 0);
                    ++cpu_uploads;
                }
                check(swap->Present(1, 0), "Present preview failed");
                backbuffer.Reset();
                ++presented;
            }
            if (done.load()) break;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, frame ? 0 : 1, QS_ALLINPUT);
        }
    } catch (...) {
        cancel.store(true);
        worker.join();
        throw;
    }
    worker.join();
    if (error) std::rethrow_exception(error);
    std::cout << "PREVIEW_PRESENTED frames=" << presented << " renderer=D3D11-test gpu_converted_frames=" << gpu_presented << " cpu_pixel_uploads=" << cpu_uploads << std::endl;
}
}
