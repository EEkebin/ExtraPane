#include <dm/input.hpp>
#include <dm/display.hpp>
#include <windowsx.h>
#include <iostream>
#include <chrono>
#include <thread>
using namespace dm::input;
namespace {
struct Observed {unsigned mouse_down=0,mouse_up=0,key_down=0,key_up=0,touch_down=0,touch_up=0,pen_down=0,pen_up=0,max_touch=0;UINT32 pressure=0,pen_flags=0,rotation=0;INT32 tilt_x=0,tilt_y=0;};
Observed seen;
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_LBUTTONDOWN){++seen.mouse_down;SetCapture(window);}
    if(message==WM_LBUTTONUP){++seen.mouse_up;ReleaseCapture();}
    if(message==WM_KEYDOWN && wparam=='A')++seen.key_down;
    if(message==WM_KEYUP && wparam=='A')++seen.key_up;
    if(message==WM_POINTERDOWN || message==WM_POINTERUPDATE || message==WM_POINTERUP) {
        const auto id=GET_POINTERID_WPARAM(wparam);POINTER_INPUT_TYPE type{};
        if(GetPointerType(id,&type) && type==PT_PEN) {
            POINTER_PEN_INFO p{};if(GetPointerPenInfo(id,&p)) {if(message==WM_POINTERDOWN)++seen.pen_down;if(message==WM_POINTERUP)++seen.pen_up;seen.pressure=std::max(seen.pressure,p.pressure);if(p.tiltX)seen.tilt_x=p.tiltX;if(p.tiltY)seen.tilt_y=p.tiltY;seen.rotation=p.rotation;seen.pen_flags|=p.penFlags;}return 0;
        }
        if(type==PT_TOUCH) {if(message==WM_POINTERDOWN)++seen.touch_down;if(message==WM_POINTERUP)++seen.touch_up;UINT32 count=0;GetPointerFrameTouchInfo(id,&count,nullptr);seen.max_touch=std::max(seen.max_touch,count);return 0;}
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
void pump(unsigned ms) { const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);while(std::chrono::steady_clock::now()<until) {MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}std::this_thread::sleep_for(std::chrono::milliseconds(2));} }
void need(bool ok,const char* text) {if(!ok)throw std::runtime_error(text);}
}
int main(int argc,char** argv) {
    if(argc==4 && std::string_view(argv[1])=="input-broker") {const std::string source=argv[3];return broker_main(std::wstring(source.begin(),source.end()));}
    if(argc==4 && std::string_view(argv[1])=="--hold") {
        const std::string text=argv[3];Broker broker(std::wstring(text.begin(),text.end()));
        Event key;key.kind=Kind::key;key.action=Action::down;key.id='A';key.code=30;
        Event mouse;mouse.kind=Kind::mouse;mouse.action=Action::down;mouse.code=1;mouse.x=25000;mouse.y=25000;
        broker.send(std::array{key,mouse});for(unsigned i=0;i<100;++i){broker.send({});std::this_thread::sleep_for(std::chrono::milliseconds(100));}return 0;
    }
    try {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        dm::display::VirtualMonitor monitor;const auto source=monitor.wait_for_source();if(argc==2 && std::string_view(argv[1])=="--portrait")dm::display::configure_virtual(source,{1080,1920,60,1,-1080,0});dm::display::Monitor target;for(const auto& m:dm::display::active_monitors())if(m.source==source)target=m;
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"DMInputHardwareCheck";RegisterClassW(&cls);
        HWND window=CreateWindowW(cls.lpszClassName,L"DM native input delivery check",WS_OVERLAPPEDWINDOW|WS_VISIBLE,target.left+20,target.top+20,1000,600,nullptr,nullptr,cls.hInstance,nullptr);
        need(window!=nullptr,"Diagnostic window creation failed");ShowWindow(window,SW_MAXIMIZE);SetForegroundWindow(window);SetFocus(window);pump(100);
        Broker broker(source);need(broker.capabilities()==all_caps,"Native pen/touch devices unavailable");
        auto send=[&](std::initializer_list<Event> events) {broker.send(events);pump(70);};
        Event mouse;mouse.kind=Kind::mouse;mouse.x=25000;mouse.y=25000;mouse.action=Action::down;mouse.code=1;send({mouse});mouse.action=Action::up;send({mouse});
        need(seen.mouse_down && seen.mouse_up,"Mouse did not reach diagnostic app");
        Event key;key.kind=Kind::key;key.id='A';key.code=30;key.action=Action::down;send({key});key.action=Action::up;send({key});need(seen.key_down && seen.key_up,"Keyboard did not reach diagnostic app");
        Event touch;touch.kind=Kind::touch;touch.id=10;touch.x=25000;touch.y=25000;touch.action=Action::down;touch.flags=3;touch.mask=TOUCH_MASK_PRESSURE;touch.pressure=600;send({touch});touch.id=11;touch.x=35000;send({touch});touch.action=Action::up;touch.flags=0;touch.pressure=0;send({touch});touch.id=10;touch.x=25000;send({touch});need(seen.touch_down==2 && seen.touch_up==2 && seen.max_touch>=2,"Native two-contact touch did not reach diagnostic app");
        Event tip;tip.kind=Kind::pen;tip.id=1;tip.x=25000;tip.y=25000;send({tip});std::cout<<"TIP_IDLE_LEAVE passed"<<std::endl;tip.action=Action::down;tip.flags=3;tip.mask=PEN_MASK_PRESSURE;tip.pressure=128;send({tip});tip.action=Action::up;tip.flags=1;tip.pressure=0;send({tip});send({Event{}});
        std::cout<<"TIP_FIRST_CONTACT passed"<<std::endl;
        tip.action=Action::down;tip.flags=3;tip.pressure=128;send({tip});tip.action=Action::up;tip.flags=1;tip.pressure=0;tip.x+=100;send({tip});send({Event{}});std::cout<<"TIP_RELEASE_DRIFT passed"<<std::endl;
        tip.action=Action::down;tip.flags=3;tip.pressure=128;send({tip});tip.action=Action::cancel;tip.flags=0;send({tip});send({Event{}});std::cout<<"TIP_CANCEL_PRESSURE passed"<<std::endl;
        tip.action=Action::down;tip.flags=3;tip.pressure=128;send({tip});tip.action=Action::move;tip.flags=1;tip.pressure=0;send({tip});send({Event{}});std::cout<<"TIP_CONTACT_TO_HOVER passed"<<std::endl;
        std::vector<Event> burst;tip.action=Action::down;tip.flags=3;tip.pressure=128;burst.push_back(tip);for(unsigned n=0;n<60;++n){tip.action=Action::move;tip.x+=5;tip.pressure=200+n;burst.push_back(tip);}tip.action=Action::up;tip.flags=1;tip.pressure=0;burst.push_back(tip);broker.send(burst);pump(150);send({Event{}});std::cout<<"TIP_COALESCED_BURST passed"<<std::endl;
        burst.clear();touch.id=13;touch.action=Action::down;touch.flags=3;touch.pressure=600;burst.push_back(touch);for(unsigned n=0;n<60;++n){touch.action=Action::move;touch.x+=5;burst.push_back(touch);}touch.action=Action::up;touch.flags=0;touch.pressure=0;burst.push_back(touch);broker.send(burst);pump(150);send({Event{}});std::cout<<"TOUCH_COALESCED_BURST passed"<<std::endl;
        Event pen;pen.kind=Kind::pen;pen.id=1;pen.x=25000;pen.y=25000;pen.action=Action::down;pen.flags=3;pen.mask=PEN_MASK_PRESSURE|PEN_MASK_TILT_X|PEN_MASK_TILT_Y;pen.pressure=800;pen.tilt_x=-35;pen.tilt_y=40;pen.code=PEN_FLAG_BARREL;send({pen});pen.action=Action::up;pen.flags=1;pen.pressure=0;send({pen});need(seen.pen_down && seen.pen_up && seen.pressure>=750 && seen.tilt_x==-35 && seen.tilt_y==40 && (seen.pen_flags&PEN_FLAG_BARREL),"Native pen axes/buttons did not reach diagnostic app");
        key.action=Action::down;mouse.action=Action::down;send({key,mouse});const auto before_key=seen.key_up,before_mouse=seen.mouse_up;send({Event{}});need(seen.key_up>before_key && seen.mouse_up>before_mouse,"Reset did not release owned state");
        pen.code=PEN_FLAG_INVERTED;pen.action=Action::move;pen.flags=1;pen.rotation=270;pen.mask|=PEN_MASK_ROTATION;send({pen});pen.action=Action::down;pen.flags=3;pen.code=PEN_FLAG_ERASER|PEN_FLAG_INVERTED;pen.pressure=800;send({pen});pen.action=Action::up;pen.flags=1;pen.pressure=0;send({pen});std::cout<<"PEN_OBSERVED flags="<<seen.pen_flags<<" rotation="<<seen.rotation<<std::endl;need(seen.pen_down>=2 && seen.pen_up>=2 && (seen.pen_flags&(PEN_FLAG_ERASER|PEN_FLAG_INVERTED))==(PEN_FLAG_ERASER|PEN_FLAG_INVERTED) && seen.rotation==270,"Pen eraser/inversion/rotation flags were lost");
        touch.id=12;touch.action=Action::down;touch.flags=3;touch.pressure=600;send({touch});
        const auto before_hold_up=seen.touch_up;
        for(unsigned i=0;i<14;++i){broker.send({});pump(100);}
        need(seen.touch_up==before_hold_up,"Held touch was canceled before release");touch.action=Action::up;touch.flags=0;touch.pressure=0;send({touch});need(seen.touch_up>before_hold_up,"Held touch did not release after refreshed hold");
        key.action=Action::down;mouse.action=Action::down;send({key,mouse});const auto before_timeout_key=seen.key_up,before_timeout_mouse=seen.mouse_up;pump(1900);need(seen.key_up>before_timeout_key && seen.mouse_up>before_timeout_mouse,"Watchdog timeout did not release owned state");
        wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring command=L"\""+std::wstring(path)+L"\" --hold --display \""+source+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        need(CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Cannot launch crash test parent");CloseHandle(process.hThread);
        struct Process {HANDLE handle;~Process(){TerminateProcess(handle,1);CloseHandle(handle);}} cleanup{process.hProcess};
        const auto old_down=seen.key_down;for(unsigned i=0;i<40 && seen.key_down==old_down;++i)pump(50);need(seen.key_down>old_down,"Crash-test input did not arrive");
        const auto old_up=seen.key_up,old_mouse_up=seen.mouse_up;TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,2000);pump(500);need(seen.key_up>old_up && seen.mouse_up>old_mouse_up,"Parent crash did not release remote keys/buttons");
        {
            dm::input::Broker moving(source);
            key.action=Action::down;mouse.action=Action::down;
            SetForegroundWindow(window);SetFocus(window);
            const auto before_move_down_key=seen.key_down,before_move_down_mouse=seen.mouse_down;
            moving.send(std::array{key,mouse});pump(100);
            need(seen.key_down>before_move_down_key && seen.mouse_down>before_move_down_mouse,"Geometry test did not establish held input");
            const auto before_move_key=seen.key_up;
            auto geometry=dm::display::current_mode(source);geometry.y+=100;
            dm::display::configure_virtual(source,geometry);
            need(dm::display::current_mode(source)==geometry,"Windows did not apply the geometry test position");
            moving.send({});pump(200);
            need(seen.key_up>before_move_key && !(GetAsyncKeyState(VK_LBUTTON)&0x8000),"Display move did not release held input");
            moving.send({});pump(50);
            std::cout<<"GEOMETRY_CHANGE_RELEASED_INPUT passed"<<std::endl;
        }
        DestroyWindow(window);std::cout<<"NATIVE_INPUT_DELIVERED mouse keyboard two-contact-touch held-touch pen pressure=800 tilt=-35,40 rotation=270 barrel eraser inversion reset watchdog parent-crash\n";return 0;
    }catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
}
