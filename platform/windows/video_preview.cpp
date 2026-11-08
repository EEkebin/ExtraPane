#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")
#include <dm/display.hpp>
#include <dm/branding.hpp>
#include <dwmapi.h>
#pragma comment(lib,"dwmapi.lib")
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <dm/video.hpp>
#include <dm/video_gpu.hpp>
#include <dm/video_d3d12.hpp>
#include <dm/video_vulkan.hpp>
#include <dm/video_opengl.hpp>
#include <dm/input.hpp>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace dm::video {
namespace {
struct PreviewWindow {
    PreviewWindow(){DWORD value=1,size=sizeof(value);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\ExtraPane\\ExtraPane",L"AutomaticRemoteKeyboard",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS)auto_keyboard.store(value!=0);}
    Renderer requested=Renderer::d3d11;
    input::ReceiverInput* input=nullptr;HWND hwnd=nullptr,bar=nullptr,handle=nullptr,stats=nullptr;bool show_statistics=false;std::atomic_bool auto_keyboard=true;std::atomic<Quality> quality{Quality::native};std::atomic<bool> menu_open{false},minimized{false};bool fullscreen=false,edge_gesture=false;
    UINT32 gesture_id=0;POINT gesture_start{};ULONGLONG edge_hover=0;
    WINDOWPLACEMENT saved{sizeof(saved)};ULONGLONG last_reveal=0;
    int scale(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(hwnd)),96);}
    bool reveal_area(POINT p)const{RECT r{};GetClientRect(hwnd,&r);return p.y>=0&&p.y<scale(8)&&abs(p.x-r.right/2)<scale(48);}
    void release_input(){input->message(hwnd,WM_KILLFOCUS,0,0);input->message(hwnd,WM_SETFOCUS,0,0);}
    void position_bar(){
        if(!bar)return;RECT r{};GetClientRect(hwnd,&r);const int width=std::min(scale(540),static_cast<int>(r.right));
        POINT p{(r.right-width)/2,scale(8)};ClientToScreen(hwnd,&p);SetWindowPos(bar,HWND_TOP,p.x,p.y,width,scale(48),SWP_NOACTIVATE);
        for(int i=0;i<6;++i)SetWindowPos(GetDlgItem(bar,i+1),nullptr,i*width/6,0,(i+1)*width/6-i*width/6,scale(48),SWP_NOZORDER|SWP_NOACTIVATE);
        if(handle){POINT grip{(r.right-scale(126))/2,0};ClientToScreen(hwnd,&grip);SetWindowPos(handle,HWND_TOP,grip.x,grip.y,scale(126),scale(32),SWP_NOACTIVATE);SetWindowPos(GetDlgItem(handle,7),nullptr,0,0,scale(126),scale(32),SWP_NOZORDER|SWP_NOACTIVATE);}
    }
    void hide_bar(){ShowWindow(bar,SW_HIDE);if(handle&&!IsIconic(hwnd))ShowWindow(handle,SW_SHOWNOACTIVATE);}
    void keyboard(){
        // The streamed desktop is not a local editable control. Launch Windows'
        // explicit accessibility keyboard; do not fake TSF focus or synthesize a
        // system-wide shortcut that could instead be forwarded to the host.
        SetForegroundWindow(hwnd);SetFocus(hwnd);wchar_t system[MAX_PATH]{};
        const auto count=GetSystemDirectoryW(system,MAX_PATH);
        if(!count||count>=MAX_PATH){std::cout<<"KEYBOARD_UNAVAILABLE Cannot locate the Windows system directory."<<std::endl;return;}
        const std::wstring path=std::wstring(system)+L"\\osk.exe";
        const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
        if(result<=32){std::cout<<"KEYBOARD_UNAVAILABLE Windows could not open On-Screen Keyboard ("<<result<<")."<<std::endl;MessageBoxW(hwnd,L"Windows could not open On-Screen Keyboard. You can open it from Windows Settings > Accessibility > Keyboard.",L"ExtraPane keyboard",MB_OK|MB_ICONINFORMATION);}
        else std::cout<<"KEYBOARD_REQUESTED Windows On-Screen Keyboard"<<std::endl;
        reveal();
    }
    void settings(){
        menu_open.store(true);auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(show_statistics?MF_CHECKED:0),10,L"Show statistics");AppendMenuW(menu,MF_STRING|(auto_keyboard.load()?MF_CHECKED:0),11,L"Automatic server keyboard (next connection)");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING|MF_DISABLED,0,L"Stream resolution - next connection only (not saved)");
        const wchar_t* modes[]={L"Native resolution (default)",L"75% resolution - lower bandwidth",L"50% resolution - lowest bandwidth"};
        for(unsigned i=0;i<3;++i)AppendMenuW(menu,MF_STRING|(static_cast<unsigned>(quality.load())==i?MF_CHECKED:0),20+i,modes[i]);
        RECT r{};GetWindowRect(GetDlgItem(bar,5),&r);SetForegroundWindow(hwnd);const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,r.right,r.bottom,0,hwnd,nullptr);DestroyMenu(menu);menu_open.store(false);PostMessageW(hwnd,WM_NULL,0,0);
        if(command==11){
            const DWORD enabled=auto_keyboard.load()?0:1;HKEY key=nullptr;LSTATUS result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\ExtraPane\\ExtraPane",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
            if(result==ERROR_SUCCESS){result=RegSetValueExW(key,L"AutomaticRemoteKeyboard",0,REG_DWORD,reinterpret_cast<const BYTE*>(&enabled),sizeof(enabled));RegCloseKey(key);}
            if(result==ERROR_SUCCESS)auto_keyboard.store(enabled!=0);else std::cout<<"KEYBOARD_SETTING_FAILED "<<result<<std::endl;
        }
        if(command==10){show_statistics=!show_statistics;ShowWindow(stats,show_statistics?SW_SHOWNOACTIVATE:SW_HIDE);}
        if(command>=20&&command<=22){quality.store(static_cast<Quality>(command-20));std::cout<<"NEXT_STREAM_SCALE "<<(command-20)<<std::endl;}
        reveal();
    }
    void reveal(){last_reveal=GetTickCount64();position_bar();if(handle)ShowWindow(handle,SW_HIDE);ShowWindow(bar,SW_SHOWNOACTIVATE);}
    void toggle(){release_input();if(!fullscreen){GetWindowPlacement(hwnd,&saved);MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&m);SetWindowLongPtrW(hwnd,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(hwnd,nullptr,m.rcMonitor.left,m.rcMonitor.top,m.rcMonitor.right-m.rcMonitor.left,m.rcMonitor.bottom-m.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOZORDER);fullscreen=true;}else{SetWindowLongPtrW(hwnd,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);SetWindowPlacement(hwnd,&saved);SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER);fullscreen=false;}SetWindowTextW(GetDlgItem(bar,3),fullscreen?L"Windowed":L"Fullscreen");reveal();}
};
constexpr UINT toolbar_command=WM_APP+42;
LRESULT CALLBACK bar_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
    if(message==WM_COMMAND){SendMessageW(GetWindow(hwnd,GW_OWNER),toolbar_command,LOWORD(wparam),0);return 0;}
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_DRAWITEM){auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);const auto brush=CreateSolidBrush(item->CtlID==4?(item->itemState&ODS_SELECTED?RGB(143,29,42):RGB(179,38,53)):(item->itemState&ODS_SELECTED?RGB(65,65,70):RGB(32,32,36)));FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(245,245,245));wchar_t label[32]{};GetWindowTextW(item->hwndItem,label,32);auto font=reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0));auto old=SelectObject(item->hDC,font);DrawTextW(item->hDC,label,-1,&item->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item->hDC,old);return TRUE;}
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
        if(message==toolbar_command){state->release_input();if(wparam==1)ShowWindow(hwnd,SW_MINIMIZE);if(wparam==2){if(state->fullscreen)state->toggle();ShowWindow(hwnd,IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);}if(wparam==3)state->toggle();if(wparam==4)DestroyWindow(hwnd);if(wparam==5)state->settings();if(wparam==6)state->keyboard();if(wparam==7)state->reveal();return 0;}
        if(message==WM_MOVE||message==WM_SIZE) {state->position_bar();state->minimized.store(IsIconic(hwnd)!=FALSE);if(IsIconic(hwnd)){ShowWindow(state->bar,SW_HIDE);ShowWindow(state->handle,SW_HIDE);state->release_input();}}
        if(message==WM_TIMER){POINT p{};GetCursorPos(&p);RECT r{};GetWindowRect(state->bar,&r);if(IsIconic(hwnd)){ShowWindow(state->bar,SW_HIDE);ShowWindow(state->handle,SW_HIDE);}else if(!state->menu_open.load()&&GetTickCount64()-state->last_reveal>4000&&!PtInRect(&r,p))state->hide_bar();POINT local=p;ScreenToClient(hwnd,&local);if(state->edge_hover&&state->reveal_area(local)&&GetTickCount64()-state->edge_hover>=600){state->reveal();state->edge_hover=0;}return 0;}
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
void preview_video(const net::Options& options, const std::string& pair,std::optional<unsigned> decoder_adapter,bool control,bool fullscreen,Quality quality,Renderer renderer,PreviewOptions preview) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct MonitorChoice {std::wstring name;HMONITOR handle=nullptr;} choice{preview.monitor};
    if(!choice.name.empty()){
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM context)->BOOL{auto& choice=*reinterpret_cast<MonitorChoice*>(context);MONITORINFOEXW info{};info.cbSize=sizeof(info);if(GetMonitorInfoW(monitor,&info)&&_wcsicmp(info.szDevice,choice.name.c_str())==0){choice.handle=monitor;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&choice));
        if(!choice.handle)throw std::runtime_error("The selected local monitor is disconnected; select an attached monitor");
    }
    if((preview.width==0)!=(preview.height==0)||preview.width>4096||preview.height>4096||(preview.width&&(preview.width<2||preview.height<2||preview.width%2||preview.height%2))||preview.fps>360)throw std::invalid_argument("Unsupported custom display mode");
    input::ReceiverInput input_capture(control);
    PreviewWindow state;state.input=&input_capture;state.quality.store(quality);state.requested=renderer;
    WNDCLASSW cls{};
    cls.style=renderer==Renderer::opengl?CS_OWNDC:0;
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"DMVideoFeasibilityPreview";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Register preview window failed");
    const HWND hwnd = CreateWindowExW(0, cls.lpszClassName, control?branding::display_title:branding::display_view_title, WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT, CW_USEDEFAULT, 960, 600, nullptr, nullptr, cls.hInstance, &state);
    if (!hwnd) throw std::runtime_error("Create preview window failed");
    DestroyWindowOnExit window{hwnd};
    // Keep disconnect scoped to the focused receiver when several windows run.
    if(choice.handle){MONITORINFO panel{sizeof(panel)};if(!GetMonitorInfoW(choice.handle,&panel))throw std::runtime_error("Selected monitor information unavailable");const int width=std::min(960L,panel.rcWork.right-panel.rcWork.left),height=std::min(600L,panel.rcWork.bottom-panel.rcWork.top);SetWindowPos(hwnd,nullptr,panel.rcWork.left+(panel.rcWork.right-panel.rcWork.left-width)/2,panel.rcWork.top+(panel.rcWork.bottom-panel.rcWork.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);}
    WNDCLASSW toolbar{};toolbar.lpfnWndProc=bar_proc;toolbar.hInstance=cls.hInstance;toolbar.lpszClassName=L"DMDisplayToolbar";toolbar.hCursor=cls.hCursor;
    RegisterClassW(&toolbar);
    state.bar=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,toolbar.lpszClassName,L"Display controls",WS_POPUP,0,0,state.scale(540),state.scale(48),hwnd,nullptr,cls.hInstance,nullptr);
    const DWORD corners=2;DwmSetWindowAttribute(state.bar,33,&corners,sizeof(corners));
    const wchar_t* labels[]={L"Minimize",L"Maximize",L"Fullscreen",L"Close",L"Settings",L"Keyboard"};
    for(int i=0;i<6;++i){auto button=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,state.scale(i*90),0,state.scale(90),state.scale(48),state.bar,reinterpret_cast<HMENU>(static_cast<INT_PTR>(i+1)),cls.hInstance,nullptr);SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);}
    // A visible, touch-sized central handle replaces the hidden eight-pixel
    // gesture as the primary way back to controls. Remote top corners remain
    // completely available; keyboard shortcuts and the narrow gesture still work.
    state.handle=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,toolbar.lpszClassName,L"ExtraPane display controls handle",WS_POPUP,0,0,state.scale(126),state.scale(32),hwnd,nullptr,cls.hInstance,nullptr);
    auto grip=CreateWindowW(L"BUTTON",L"Controls  \x25BE",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,state.scale(126),state.scale(32),state.handle,reinterpret_cast<HMENU>(7),cls.hInstance,nullptr);
    SendMessageW(grip,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    DwmSetWindowAttribute(state.handle,33,&corners,sizeof(corners));
    WNDCLASSW statistics_class{};statistics_class.lpfnWndProc=stats_proc;statistics_class.hInstance=cls.hInstance;statistics_class.lpszClassName=L"ExtraPaneStatistics";RegisterClassW(&statistics_class);
    state.stats=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,statistics_class.lpszClassName,L"Waiting for stream statistics...",WS_POPUP,0,0,state.scale(440),state.scale(150),hwnd,nullptr,cls.hInstance,nullptr);
    SetLayeredWindowAttributes(state.stats,0,235,LWA_ALPHA);SendMessageW(state.stats,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    SetTimer(hwnd,1,250,nullptr);
    if(fullscreen)state.toggle();
    MONITORINFOEXW panel{};panel.cbSize=sizeof(panel);GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&panel);
    auto native=display::current_mode(panel.szDevice);
    // EnumDisplaySettings reports 59 for common 59.94 Hz panels; target 60 frames.
    if(native.hz==59)native.hz=60;
    const bool custom=preview.width||preview.fps;
    const wire::ClientDisplay preference{preview.width?preview.width:native.width,preview.height?preview.height:native.height,preview.fps?preview.fps:std::min(native.hz,60u),custom && preview.screen.mode!=wire::DisplayMode::existing_screen};
    std::cout<<"CLIENT_DISPLAY "<<preference.width<<'x'<<preference.height<<'@'<<preference.hz<<std::endl;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    // Test launcher hides its PowerShell parent; explicitly show the native preview.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    state.reveal();
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    std::unique_ptr<GpuProcessor> gpu_processor;
    std::unique_ptr<D3D12Renderer> d3d12;
    std::unique_ptr<VulkanRenderer> vulkan;
    std::unique_ptr<OpenGLRenderer> opengl;
    Microsoft::WRL::ComPtr<ID3D11Device> probed_device;
    Renderer active_renderer=Renderer::d3d11;
    auto reset_d3d11=[&]{backbuffer.Reset();gpu_processor.reset();if(context){context->ClearState();context->Flush();}swap.Reset();context.Reset();device.Reset();};
    auto unavailable=[&](const std::string& reason){std::cout<<"RENDERER_UNAVAILABLE "<<renderer_name(state.requested)<<' '<<reason<<std::endl;state.requested=Renderer::d3d11;};
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
            },decoder_adapter,control?std::function<std::vector<input::Event>()>([&]{return input_capture.drain();}):std::function<std::vector<input::Event>()>{},[&](unsigned caps){if(!caps){std::lock_guard guard(mutex);latest.reset();}input_capture.grant(caps);std::cout<<"INPUT_CAPTURE caps="<<caps<<" digitizer="<<GetSystemMetrics(SM_DIGITIZER)<<" pen_axes=per-sample-validity"<<std::endl;},preference,quality,[&](StreamStats stats){std::lock_guard guard(mutex);stream_stats=stats;},state.auto_keyboard.load()?std::function<void()>([]{std::cout<<"KEYBOARD_EDITABLE_TOUCH host keyboard requested"<<std::endl;}):std::function<void()>{},preview.screen);
        } catch (...) { error = std::current_exception(); }
        done.store(true);
    });
    unsigned surface_width=0,surface_height=0,buffer_width=0,buffer_height=0;
    std::uint64_t presented = 0;
    std::uint64_t gpu_presented=0,cpu_uploads=0,d3d12_presented=0,vulkan_presented=0,opengl_presented=0;
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
                if(state.requested!=Renderer::d3d11){
                    if(!frame->gpu)unavailable("This renderer requires GPU-decoded H.264; the stream uses CPU frames");
                    else if(probed_device.Get()!=frame->gpu->device.Get()){
                        d3d12.reset();vulkan.reset();opengl.reset();probed_device=frame->gpu->device;
                        try{D3D11_TEXTURE2D_DESC desc{};frame->gpu->texture->GetDesc(&desc);if(state.requested==Renderer::opengl)opengl=std::make_unique<OpenGLRenderer>(probed_device.Get(),frame->width,frame->height);else if(state.requested==Renderer::vulkan)vulkan=std::make_unique<VulkanRenderer>(probed_device.Get(),frame->width,frame->height);else d3d12=std::make_unique<D3D12Renderer>(probed_device.Get(),desc.Width,desc.Height);std::cout<<"RENDERER_AVAILABLE "<<renderer_name(state.requested)<<" adapter="<<(opengl?opengl->adapter_name():vulkan?vulkan->adapter_name():d3d12->adapter_name())<<std::endl;}
                        catch(const std::exception& e){unavailable(e.what());}
                    }
                }
                if(state.requested!=active_renderer){reset_d3d11();if(d3d12)d3d12->detach();active_renderer=state.requested;std::cout<<"RENDERER_CHANGED "<<renderer_name(active_renderer)<<std::endl;}
                SetPropW(hwnd,L"ExtraPane.Renderer",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(active_renderer)+1));
                if(active_renderer!=Renderer::d3d11){
                    try{if(opengl){if(!opengl->present(hwnd,*frame))continue;++opengl_presented;}else if(vulkan){if(!vulkan->present(hwnd,*frame))continue;++vulkan_presented;}else{d3d12->present(hwnd,*frame);++d3d12_presented;}++gpu_presented;}
                    catch(const std::exception& e){unavailable(e.what());d3d12.reset();vulkan.reset();opengl.reset();active_renderer=Renderer::d3d11;std::cout<<"RENDERER_FALLBACK D3D11"<<std::endl;continue;}
                }else{
                if(swap && (buffer_width!=target_width || buffer_height!=target_height || surface_width!=frame->width || surface_height!=frame->height || (frame->gpu && frame->gpu->device.Get()!=device.Get()))) {
                    reset_d3d11();
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
                    std::cout << "PREVIEW_RENDERER api=D3D11 adapter=" << name << std::endl;
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
                }
                ++presented;++interval_presented;
                const auto cost=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-render_start).count();render_total+=cost;
            }
            const auto now=std::chrono::steady_clock::now();const auto elapsed=std::chrono::duration<double>(now-stats_time).count();
            if(elapsed>=1){
                StreamStats stats;std::uint64_t skipped;{std::lock_guard guard(mutex);stats=stream_stats;skipped=dropped_total;}
                std::wostringstream text;text<<std::fixed<<std::setprecision(1)<<L"  "<<renderer_name(active_renderer)<<L" | "<<stats.width<<L" x "<<stats.height<<L" | target "<<stats.target_fps<<L" FPS\n  Received "<<stats.received_fps<<L" FPS | presented "<<interval_presented/elapsed<<L" FPS\n  Video "<<stats.mbps<<L" Mbps | skipped in preview "<<skipped<<L"\n  Decode "<<stats.decode_ms<<L" ms | render/present "<<(interval_presented?render_total/interval_presented:0)<<L" ms\n  Local timings; not end-to-end latency.";
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
    std::cout << "PREVIEW_PRESENTED frames=" << presented << " renderer="<<renderer_name(active_renderer)<<" d3d12_frames="<<d3d12_presented<<" opengl_frames="<<opengl_presented<<" vulkan_frames="<<vulkan_presented<<" gpu_converted_frames=" << gpu_presented << " cpu_pixel_uploads=" << cpu_uploads << std::endl;
}
}
