#include <windows.h>
#include <windowsx.h>
#include <dm/display.hpp>
#include <dwmapi.h>
#pragma comment(lib,"dwmapi.lib")
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
#include <sstream>
#include <iomanip>

namespace dm::video {
namespace {
struct PreviewWindow {
    input::ReceiverInput* input=nullptr;HWND hwnd=nullptr,bar=nullptr,stats=nullptr;bool show_statistics=false;std::atomic<Quality> quality{Quality::native};std::atomic<bool> menu_open{false},minimized{false};bool fullscreen=false,edge_gesture=false;
    UINT32 gesture_id=0;POINT gesture_start{};ULONGLONG edge_hover=0;
    WINDOWPLACEMENT saved{sizeof(saved)};ULONGLONG last_reveal=0;
    int scale(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(hwnd)),96);}
    bool reveal_area(POINT p)const{RECT r{};GetClientRect(hwnd,&r);return p.y>=0&&p.y<scale(8)&&abs(p.x-r.right/2)<scale(48);}
    void release_input(){input->message(hwnd,WM_KILLFOCUS,0,0);input->message(hwnd,WM_SETFOCUS,0,0);}
    void position_bar(){if(!bar)return;RECT r{};GetClientRect(hwnd,&r);POINT p{(r.right-scale(450))/2,scale(8)};ClientToScreen(hwnd,&p);SetWindowPos(bar,HWND_TOP,p.x,p.y,scale(450),scale(42),SWP_NOACTIVATE);}
    void settings(){
        menu_open.store(true);auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(show_statistics?MF_CHECKED:0),10,L"Show statistics");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING|MF_DISABLED,0,L"Stream resolution - next connection only (not saved)");
        const wchar_t* modes[]={L"Native resolution (default)",L"75% resolution - lower bandwidth",L"50% resolution - lowest bandwidth"};
        for(unsigned i=0;i<3;++i)AppendMenuW(menu,MF_STRING|(static_cast<unsigned>(quality.load())==i?MF_CHECKED:0),20+i,modes[i]);
        RECT r{};GetWindowRect(GetDlgItem(bar,5),&r);SetForegroundWindow(hwnd);const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,r.right,r.bottom,0,hwnd,nullptr);DestroyMenu(menu);menu_open.store(false);PostMessageW(hwnd,WM_NULL,0,0);
        if(command==10){show_statistics=!show_statistics;ShowWindow(stats,show_statistics?SW_SHOWNOACTIVATE:SW_HIDE);}
        if(command>=20&&command<=22){quality.store(static_cast<Quality>(command-20));std::cout<<"NEXT_STREAM_SCALE "<<(command-20)<<std::endl;}
        reveal();
    }
    void reveal(){last_reveal=GetTickCount64();position_bar();ShowWindow(bar,SW_SHOWNOACTIVATE);}
    void toggle(){release_input();if(!fullscreen){GetWindowPlacement(hwnd,&saved);MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&m);SetWindowLongPtrW(hwnd,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(hwnd,nullptr,m.rcMonitor.left,m.rcMonitor.top,m.rcMonitor.right-m.rcMonitor.left,m.rcMonitor.bottom-m.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOZORDER);fullscreen=true;}else{SetWindowLongPtrW(hwnd,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);SetWindowPlacement(hwnd,&saved);SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER);fullscreen=false;}SetWindowTextW(GetDlgItem(bar,3),fullscreen?L"Windowed":L"Fullscreen");reveal();}
};
constexpr UINT toolbar_command=WM_APP+42;
LRESULT CALLBACK bar_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
    if(message==WM_COMMAND){SendMessageW(GetWindow(hwnd,GW_OWNER),toolbar_command,LOWORD(wparam),0);return 0;}
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_DRAWITEM){auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);const auto brush=CreateSolidBrush(item->itemState&ODS_SELECTED?RGB(65,65,70):RGB(32,32,36));FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(245,245,245));wchar_t label[32]{};GetWindowTextW(item->hwndItem,label,32);auto font=reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0));auto old=SelectObject(item->hDC,font);DrawTextW(item->hDC,label,-1,&item->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item->hDC,old);return TRUE;}
    if(message==WM_ERASEBKGND){RECT r{};GetClientRect(hwnd,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,GetSysColorBrush(COLOR_BTNFACE));return 1;}
    return DefWindowProcW(hwnd,message,wparam,lparam);
}
LRESULT CALLBACK stats_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(hwnd,&paint);RECT r{};GetClientRect(hwnd,&r);auto brush=CreateSolidBrush(RGB(24,24,28));FillRect(dc,&r,brush);DeleteObject(brush);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(245,245,245));auto font=CreateFontW(-MulDiv(13,static_cast<int>(GetDpiForWindow(hwnd)),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto old=SelectObject(dc,font);wchar_t text[1024]{};GetWindowTextW(hwnd,text,1024);DrawTextW(dc,text,-1,&r,DT_LEFT|DT_TOP|DT_NOPREFIX|DT_WORDBREAK);SelectObject(dc,old);DeleteObject(font);EndPaint(hwnd,&paint);return 0;}
    auto result=DefWindowProcW(hwnd,message,wparam,lparam);if(message==WM_SETTEXT)InvalidateRect(hwnd,nullptr,FALSE);return result;
}
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if(message==WM_NCCREATE){auto* state=static_cast<PreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);state->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
    auto* state=reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(state){
        if((message==WM_HOTKEY&&wparam==1)||(message==WM_KEYDOWN&&wparam=='Q'&&(GetKeyState(VK_CONTROL)&0x8000)&&(GetKeyState(VK_SHIFT)&0x8000))){state->release_input();DestroyWindow(hwnd);return 0;}
        if(message==WM_KEYDOWN && (wparam==VK_F9||wparam==VK_F10||wparam==VK_F11)){
            if(!(lparam&(1LL<<30))){if(wparam==VK_F9)DestroyWindow(hwnd);else if(wparam==VK_F11)state->toggle();else state->reveal();}return 0;
        }
        if(message==WM_KEYUP && (wparam==VK_F9||wparam==VK_F10||wparam==VK_F11))return 0;
        if(message==toolbar_command){state->release_input();if(wparam==1)ShowWindow(hwnd,SW_MINIMIZE);if(wparam==2){if(state->fullscreen)state->toggle();ShowWindow(hwnd,IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);}if(wparam==3)state->toggle();if(wparam==4)DestroyWindow(hwnd);if(wparam==5)state->settings();return 0;}
        if(message==WM_MOVE||message==WM_SIZE) {state->position_bar();state->minimized.store(IsIconic(hwnd)!=FALSE);if(IsIconic(hwnd)){ShowWindow(state->bar,SW_HIDE);state->release_input();}}
        if(message==WM_TIMER){POINT p{};GetCursorPos(&p);RECT r{};GetWindowRect(state->bar,&r);if(IsIconic(hwnd))ShowWindow(state->bar,SW_HIDE);else if(GetTickCount64()-state->last_reveal>2500&&!PtInRect(&r,p))ShowWindow(state->bar,SW_HIDE);POINT local=p;ScreenToClient(hwnd,&local);if(state->edge_hover&&state->reveal_area(local)&&GetTickCount64()-state->edge_hover>=600){state->reveal();state->edge_hover=0;}return 0;}
        if(message==WM_MOUSEMOVE){
            const bool promoted=(GetMessageExtraInfo()&0xFFFFFF00)==0xFF515700;
            POINT p{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
            if(!promoted&&state->reveal_area(p)&&!(wparam&(MK_LBUTTON|MK_RBUTTON|MK_MBUTTON))){if(!state->edge_hover)state->edge_hover=GetTickCount64();}else state->edge_hover=0;
        }
        if(message==WM_POINTERDOWN){POINTER_INFO p{};if(GetPointerInfo(GET_POINTERID_WPARAM(wparam),&p)&&p.pointerType==PT_TOUCH&&!state->gesture_id){ScreenToClient(hwnd,&p.ptPixelLocation);if(state->reveal_area(p.ptPixelLocation)){state->gesture_id=p.pointerId;state->gesture_start=p.ptPixelLocation;return 0;}}}
        if(state->gesture_id==GET_POINTERID_WPARAM(wparam)&&(message==WM_POINTERUPDATE||message==WM_POINTERUP||message==WM_POINTERCAPTURECHANGED)){
            POINTER_INFO p{};if(message==WM_POINTERUPDATE&&GetPointerInfo(state->gesture_id,&p)){ScreenToClient(hwnd,&p.ptPixelLocation);if(p.ptPixelLocation.y-state->gesture_start.y>=state->scale(24)){if(!state->edge_gesture)state->release_input();state->edge_gesture=true;state->reveal();}}
            if(message!=WM_POINTERUPDATE){state->gesture_id=0;state->edge_gesture=false;}return 0;
        }
        if(message==WM_DPICHANGED){auto* r=reinterpret_cast<RECT*>(lparam);if(!state->fullscreen)SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);state->position_bar();return 0;}
        if(state->input&&state->input->message(hwnd,message,wparam,lparam))return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
void check(HRESULT hr, const char* message) { if (FAILED(hr)) throw std::runtime_error(message); }
struct DestroyWindowOnExit { HWND hwnd; ~DestroyWindowOnExit() { if (IsWindow(hwnd)) DestroyWindow(hwnd); } };
}
void preview_video(const net::Options& options, const std::string& pair,std::optional<unsigned> decoder_adapter,bool control,bool fullscreen,Quality quality) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    input::ReceiverInput input_capture(control);
    PreviewWindow state;state.input=&input_capture;state.quality.store(quality);
    WNDCLASSW cls{};
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"DMVideoFeasibilityPreview";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Register preview window failed");
    const HWND hwnd = CreateWindowExW(0, cls.lpszClassName, control?L"DM Display ? F8 pauses input ? F11 fullscreen":L"DM Display ? View only ? F11 fullscreen", WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT, CW_USEDEFAULT, 960, 600, nullptr, nullptr, cls.hInstance, &state);
    if (!hwnd) throw std::runtime_error("Create preview window failed");
    DestroyWindowOnExit window{hwnd};
    if(!RegisterHotKey(hwnd,1,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'Q'))std::cout<<"DISCONNECT_SHORTCUT focused-window-only"<<std::endl;
    WNDCLASSW toolbar{};toolbar.lpfnWndProc=bar_proc;toolbar.hInstance=cls.hInstance;toolbar.lpszClassName=L"DMDisplayToolbar";toolbar.hCursor=cls.hCursor;
    RegisterClassW(&toolbar);
    state.bar=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,toolbar.lpszClassName,L"Display controls",WS_POPUP,0,0,state.scale(450),state.scale(42),hwnd,nullptr,cls.hInstance,nullptr);
    const DWORD corners=2;DwmSetWindowAttribute(state.bar,33,&corners,sizeof(corners));
    const wchar_t* labels[]={L"Minimize",L"Maximize",L"Fullscreen",L"Close",L"Settings"};
    for(int i=0;i<5;++i){auto button=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,state.scale(i*90),0,state.scale(90),state.scale(40),state.bar,reinterpret_cast<HMENU>(static_cast<INT_PTR>(i+1)),cls.hInstance,nullptr);SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);}
    WNDCLASSW statistics_class{};statistics_class.lpfnWndProc=stats_proc;statistics_class.hInstance=cls.hInstance;statistics_class.lpszClassName=L"SideScreenStatistics";RegisterClassW(&statistics_class);
    state.stats=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,statistics_class.lpszClassName,L"Waiting for stream statistics...",WS_POPUP,0,0,state.scale(440),state.scale(150),hwnd,nullptr,cls.hInstance,nullptr);
    SetLayeredWindowAttributes(state.stats,0,235,LWA_ALPHA);SendMessageW(state.stats,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    SetTimer(hwnd,1,250,nullptr);
    if(fullscreen)state.toggle();
    MONITORINFOEXW panel{};panel.cbSize=sizeof(panel);GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&panel);
    auto native=display::current_mode(panel.szDevice);
    // EnumDisplaySettings reports 59 for common 59.94 Hz panels; target 60 frames.
    if(native.hz==59)native.hz=60;
    const wire::ClientDisplay preference{native.width,native.height,native.hz};
    std::cout<<"CLIENT_DISPLAY "<<native.width<<'x'<<native.height<<'@'<<native.hz<<std::endl;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    // Test launcher hides its PowerShell parent; explicitly show the native preview.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    state.reveal();
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    std::unique_ptr<GpuProcessor> gpu_processor;
    std::mutex mutex;
    std::optional<Frame> latest;
    StreamStats stream_stats;std::uint64_t dropped_total=0;
    auto stats_time=std::chrono::steady_clock::now();std::uint64_t interval_presented=0;double render_total=0;
    std::exception_ptr error;
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::jthread worker([&] {
        try {
            receive_video(options, pair, [&](Frame frame) {
                if (cancel.load()) throw std::runtime_error("Video preview closed by user");
                std::lock_guard guard(mutex);
                if(latest){++dropped_total;}latest = std::move(frame); // A single latest frame, never an unbounded UI queue.
            },decoder_adapter,control?std::function<std::vector<input::Event>()>([&]{return input_capture.drain();}):std::function<std::vector<input::Event>()>{},[&](unsigned caps){if(!caps){std::lock_guard guard(mutex);latest.reset();}input_capture.grant(caps);std::cout<<"INPUT_CAPTURE caps="<<caps<<" digitizer="<<GetSystemMetrics(SM_DIGITIZER)<<" pen_axes=per-sample-validity"<<std::endl;},preference,quality,[&](StreamStats stats){std::lock_guard guard(mutex);stream_stats=stats;});
        } catch (...) { error = std::current_exception(); }
        done.store(true);
    });
    unsigned surface_width=0,surface_height=0,buffer_width=0,buffer_height=0;
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
                const auto render_start=std::chrono::steady_clock::now();
                input_capture.dimensions(frame->width,frame->height);
                RECT client{};GetClientRect(hwnd,&client);
                if(client.right<2 || client.bottom<2)continue;
                const auto target_width=frame->gpu?static_cast<unsigned>(client.right):frame->width;
                const auto target_height=frame->gpu?static_cast<unsigned>(client.bottom):frame->height;
                if(swap && (buffer_width!=target_width || buffer_height!=target_height || surface_width!=frame->width || surface_height!=frame->height || (frame->gpu && frame->gpu->device.Get()!=device.Get()))) {
                    backbuffer.Reset();gpu_processor.reset();context->ClearState();context->Flush();swap.Reset();context.Reset();device.Reset();
                }
                if (!swap) {
                    surface_width=frame->width;surface_height=frame->height;buffer_width=target_width;buffer_height=target_height;
                    DXGI_SWAP_CHAIN_DESC desc{};
                    desc.BufferDesc.Width = target_width; desc.BufferDesc.Height = target_height;
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
                ++presented;++interval_presented;
                const auto cost=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-render_start).count();render_total+=cost;
            }
            const auto now=std::chrono::steady_clock::now();const auto elapsed=std::chrono::duration<double>(now-stats_time).count();
            if(elapsed>=1){
                StreamStats stats;std::uint64_t skipped;{std::lock_guard guard(mutex);stats=stream_stats;skipped=dropped_total;}
                std::wostringstream text;text<<std::fixed<<std::setprecision(1)<<L"  "<<quality_name(quality)<<L" | "<<stats.width<<L" x "<<stats.height<<L" | target "<<stats.target_fps<<L" FPS\n  Received "<<stats.received_fps<<L" FPS | presented "<<interval_presented/elapsed<<L" FPS\n  Video "<<stats.mbps<<L" Mbps | skipped in preview "<<skipped<<L"\n  Decode "<<stats.decode_ms<<L" ms | render/present "<<(interval_presented?render_total/interval_presented:0)<<L" ms\n  Local timings; not end-to-end latency.";
                SetWindowTextW(state.stats,text.str().c_str());stats_time=now;interval_presented=0;render_total=0;
                std::cout<<"QUALITY_STATS received_fps="<<stats.received_fps<<" mbps="<<stats.mbps<<" decode_ms="<<stats.decode_ms<<" skipped="<<skipped<<" stream="<<stats.width<<'x'<<stats.height<<std::endl;
            }
            if(state.show_statistics&&!IsIconic(hwnd)){RECT r{};GetClientRect(hwnd,&r);POINT p{state.scale(12),std::max(state.scale(8),static_cast<int>(r.bottom)-state.scale(150))};ClientToScreen(hwnd,&p);SetWindowPos(state.stats,nullptr,p.x,p.y,state.scale(440),state.scale(140),SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);}else ShowWindow(state.stats,SW_HIDE);
            if (done.load()) break;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, frame ? 0 : 1, QS_ALLINPUT);
        }
    } catch (...) {
        cancel.store(true);
        worker.join();
        throw;
    }
    worker.join();
    if (error && !cancel.load()) std::rethrow_exception(error);
    std::cout << "PREVIEW_PRESENTED frames=" << presented << " renderer=D3D11-test gpu_converted_frames=" << gpu_presented << " cpu_pixel_uploads=" << cpu_uploads << std::endl;
}
}
