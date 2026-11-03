#include <dm/input.hpp>
#include <dm/display.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
namespace dm::input {
namespace {
using Clock=std::chrono::steady_clock;
void need(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void close(HANDLE& handle) { if(handle && handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);handle=nullptr; }
void write(HANDLE pipe,std::span<const std::uint8_t> bytes) { DWORD written=0;need(WriteFile(pipe,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size(),"Input watchdog pipe closed"); }
struct Injector {
    struct Timer {
        HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        ~Timer(){if(handle)CloseHandle(handle);}
    } pacing;
    struct InputLease {
        HANDLE handle=CreateMutexW(nullptr,FALSE,L"Local\\ExtraPane.RemoteInput");bool held=false;
        bool acquire(){if(held)return true;const auto result=WaitForSingleObject(handle,0);return held=(result==WAIT_OBJECT_0 || result==WAIT_ABANDONED);}
        void release(){if(held){ReleaseMutex(handle);held=false;}}
        ~InputLease(){release();if(handle)CloseHandle(handle);}
    } lease;
    Clock::time_point last_pointer=Clock::now()-std::chrono::seconds(1);
    display::Monitor target;
    HSYNTHETICPOINTERDEVICE touch=CreateSyntheticPointerDevice(PT_TOUCH,10,POINTER_FEEDBACK_DEFAULT);
    HSYNTHETICPOINTERDEVICE pen=CreateSyntheticPointerDevice(PT_PEN,1,POINTER_FEEDBACK_DEFAULT);
    std::map<unsigned,Event> contacts;
    unsigned touch_primary=0;
    std::optional<Event> pen_last;
    std::map<unsigned,Event> keys,buttons;
    std::array<bool,256> physical{};
    bool physical_initialized=false;
    HHOOK keyboard_hook=nullptr,mouse_hook=nullptr;
    static inline Injector* active=nullptr;
    static LRESULT CALLBACK keyboard(int code,WPARAM message,LPARAM param) {
        if(code==HC_ACTION && active) { const auto& k=*reinterpret_cast<KBDLLHOOKSTRUCT*>(param);if(!(k.flags&LLKHF_INJECTED) && k.vkCode<256) active->physical[k.vkCode]=message==WM_KEYDOWN || message==WM_SYSKEYDOWN; }
        return CallNextHookEx(nullptr,code,message,param);
    }
    static unsigned button_vk(unsigned button) { constexpr unsigned values[]={0,VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};return values[button]; }
    static LRESULT CALLBACK mouse(int code,WPARAM message,LPARAM param) {
        if(code==HC_ACTION && active) {
            const auto& m=*reinterpret_cast<MSLLHOOKSTRUCT*>(param);
            if(!(m.flags&LLMHF_INJECTED)) {
                unsigned vk=0;bool down=false;
                switch(message) {
                case WM_LBUTTONDOWN:down=true;[[fallthrough]];case WM_LBUTTONUP:vk=VK_LBUTTON;break;
                case WM_RBUTTONDOWN:down=true;[[fallthrough]];case WM_RBUTTONUP:vk=VK_RBUTTON;break;
                case WM_MBUTTONDOWN:down=true;[[fallthrough]];case WM_MBUTTONUP:vk=VK_MBUTTON;break;
                case WM_XBUTTONDOWN:down=true;[[fallthrough]];case WM_XBUTTONUP:vk=HIWORD(m.mouseData)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2;break;
                }
                if(vk)active->physical[vk]=down;
            }
        }return CallNextHookEx(nullptr,code,message,param);
    }
    explicit Injector(const std::wstring& source) {
        need(lease.handle!=nullptr,"Cannot coordinate remote input");
        need(pacing.handle!=nullptr,"Cannot create pointer pacing timer");
        for(const auto& monitor:display::active_monitors()) if(monitor.source==source) target=monitor;
        need(!target.source.empty(),"Input target is not an active monitor");
        active=this;
        keyboard_hook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard,GetModuleHandleW(nullptr),0);
        mouse_hook=SetWindowsHookExW(WH_MOUSE_LL,mouse,GetModuleHandleW(nullptr),0);
        if(!keyboard_hook || !mouse_hook) { if(keyboard_hook)UnhookWindowsHookEx(keyboard_hook);if(mouse_hook)UnhookWindowsHookEx(mouse_hook);if(touch)DestroySyntheticPointerDevice(touch);if(pen)DestroySyntheticPointerDevice(pen);active=nullptr;throw std::runtime_error("Cannot track local input ownership"); }
    }
    unsigned caps() const { return mouse_cap|keyboard_cap|(touch?touch_cap:0)|(pen?pen_cap:0); }
    POINT point(const Event& event) const {
        return {target.left+static_cast<LONG>((static_cast<std::uint64_t>(event.x)*(target.right-target.left-1)+32767)/65535),target.top+static_cast<LONG>((static_cast<std::uint64_t>(event.y)*(target.bottom-target.top-1)+32767)/65535)};
    }
    bool geometry() {
        try {
        for(const auto& monitor:display::active_monitors()) if(monitor.device_path==target.device_path && (!target.dm_virtual || monitor.dm_virtual)) {
            if(monitor.source==target.source && monitor.left==target.left && monitor.top==target.top && monitor.right==target.right && monitor.bottom==target.bottom)return true;
            reset();target=monitor;return false;
        }
        }catch(...){ /* Windows can expose an incomplete topology during Apply. */ }
        reset();return false;
    }
    static void send(INPUT value) { value.mi.dwExtraInfo=0x444D494E;need(SendInput(1,&value,sizeof(value))==1,"Windows rejected input injection"); }
    void key(const Event& e) {
        if(e.action==Action::down) {
            MONITORINFOEXW focused{};focused.cbSize=sizeof(focused);
            const auto monitor=MonitorFromWindow(GetForegroundWindow(),MONITOR_DEFAULTTONULL);
            if(!monitor || !GetMonitorInfoW(monitor,&focused) || target.source!=focused.szDevice)return;
            if(!keys.contains(e.id) && physical[e.id])return;keys[e.id]=e;
        }
        else { if(!keys.contains(e.id))return;keys.erase(e.id);if(physical[e.id])return; }
        INPUT value{};value.type=INPUT_KEYBOARD;value.ki.wScan=static_cast<WORD>(e.code);value.ki.dwFlags=KEYEVENTF_SCANCODE|(e.value?KEYEVENTF_EXTENDEDKEY:0)|(e.action==Action::up?KEYEVENTF_KEYUP:0);value.ki.dwExtraInfo=0x444D494E;
        need(SendInput(1,&value,sizeof(value))==1,"Windows rejected keyboard injection");
    }
    void mouse_event(const Event& e,bool reposition=true) {
        const auto p=point(e);const auto left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN),width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
        INPUT move{};move.type=INPUT_MOUSE;move.mi.dx=static_cast<LONG>((static_cast<std::int64_t>(p.x-left)*65535)/(std::max(2,width)-1));move.mi.dy=static_cast<LONG>((static_cast<std::int64_t>(p.y-top)*65535)/(std::max(2,height)-1));move.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
        if(e.action==Action::move) { send(move);return; }
        if(e.action==Action::wheel) { send(move);INPUT value{};value.type=INPUT_MOUSE;value.mi.dwFlags=e.code?MOUSEEVENTF_HWHEEL:MOUSEEVENTF_WHEEL;value.mi.mouseData=static_cast<DWORD>(e.value);send(value);return; }
        const auto vk=button_vk(e.code);
        if(e.action==Action::down) { if(buttons.contains(e.code) || physical[vk])return;buttons[e.code]=e; }
        else { if(!buttons.contains(e.code))return;buttons.erase(e.code);if(physical[vk])return; }
        if(reposition)send(move);
        constexpr DWORD downs[]={0,MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_RIGHTDOWN,MOUSEEVENTF_MIDDLEDOWN,MOUSEEVENTF_XDOWN,MOUSEEVENTF_XDOWN};
        constexpr DWORD ups[]={0,MOUSEEVENTF_LEFTUP,MOUSEEVENTF_RIGHTUP,MOUSEEVENTF_MIDDLEUP,MOUSEEVENTF_XUP,MOUSEEVENTF_XUP};
        INPUT value{};value.type=INPUT_MOUSE;value.mi.dwFlags=e.action==Action::down?downs[e.code]:ups[e.code];if(e.code>=4)value.mi.mouseData=e.code==4?XBUTTON1:XBUTTON2;send(value);
    }
    static POINTER_FLAGS flags(const Event& e) {
        POINTER_FLAGS f=0;if(e.flags&1)f|=POINTER_FLAG_INRANGE;if(e.flags&2)f|=POINTER_FLAG_INCONTACT;
        if(e.action==Action::down)f|=POINTER_FLAG_DOWN;
        else if(e.action==Action::up || e.action==Action::cancel)f|=POINTER_FLAG_UP;
        else f|=POINTER_FLAG_UPDATE;
        if(e.action==Action::cancel)f|=POINTER_FLAG_CANCELED;
        return f;
    }
    void inject(HSYNTHETICPOINTERDEVICE device,const POINTER_TYPE_INFO* frame,UINT32 count,const char* stage) {
        // A network batch can otherwise submit multiple history samples in one
        // Windows pointer timestamp quantum. Preserve samples, pace API calls.
        const auto until=last_pointer+std::chrono::microseconds(250);
        while(Clock::now()<until) {
            const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(until-Clock::now()).count();
            LARGE_INTEGER due{};due.QuadPart=-std::max<std::int64_t>(1,remaining/100);
            need(SetWaitableTimer(pacing.handle,&due,0,nullptr,nullptr,FALSE)!=FALSE && WaitForSingleObject(pacing.handle,100)==WAIT_OBJECT_0,"Pointer pacing wait failed");
        }
        const auto success=InjectSyntheticPointerInput(device,frame,count);const auto error=success?ERROR_SUCCESS:GetLastError();last_pointer=Clock::now();
        if(!success)throw std::runtime_error(std::string(stage)+" win32="+std::to_string(error));
    }
    POINTER_TYPE_INFO pointer(const Event& e) {
        POINTER_TYPE_INFO info{};info.type=e.kind==Kind::pen?PT_PEN:PT_TOUCH;
        auto& p=e.kind==Kind::pen?info.penInfo.pointerInfo:info.touchInfo.pointerInfo;
        p.pointerType=info.type;p.pointerId=e.kind==Kind::pen?1:e.id;p.ptPixelLocation=point(e);
        // InjectSyntheticPointerInput uses pixels relative to the virtual desktop
        // origin, including when a display sits left of or above the primary.
        p.ptPixelLocation.x-=GetSystemMetrics(SM_XVIRTUALSCREEN);
        p.ptPixelLocation.y-=GetSystemMetrics(SM_YVIRTUALSCREEN);p.pointerFlags=flags(e);
        if(e.kind==Kind::pen || e.id==touch_primary)p.pointerFlags|=POINTER_FLAG_PRIMARY;
        if(e.kind==Kind::pen) {
            auto& pen_info=info.penInfo;pen_info.penFlags=e.code;
            // Synthetic pen expects inversion on hover and eraser on contact.
            // Supplying both at once can turn hover/up into contact on Windows.
            if(e.flags&2) {if(e.code&PEN_FLAG_ERASER)pen_info.penFlags&=~PEN_FLAG_INVERTED;}
            else pen_info.penFlags&=~PEN_FLAG_ERASER;
            pen_info.penMask=e.mask;pen_info.pressure=e.pressure;pen_info.tiltX=e.tilt_x;pen_info.tiltY=e.tilt_y;pen_info.rotation=e.rotation;
        }
        else { auto& t=info.touchInfo;t.touchMask=e.mask;t.pressure=e.pressure;t.orientation=e.rotation;const LONG rx=std::max(1L,static_cast<LONG>(static_cast<std::uint64_t>(e.contact_x)*(target.right-target.left)/131070)),ry=std::max(1L,static_cast<LONG>(static_cast<std::uint64_t>(e.contact_y)*(target.bottom-target.top)/131070));t.rcContact={p.ptPixelLocation.x-rx,p.ptPixelLocation.y-ry,p.ptPixelLocation.x+rx,p.ptPixelLocation.y+ry}; }
        return info;
    }
    void pointer_event(const Event& e) {
        if(e.kind==Kind::pen) {
            need(pen!=nullptr,"Pen input unavailable");
            if(e.action==Action::down && pen_last && (pen_last->flags&2))throw std::runtime_error("Duplicate pen down");
            if(e.action==Action::move && (e.flags&2) && (!pen_last || !(pen_last->flags&2)))return;
            if((e.action==Action::up || e.action==Action::cancel) && (!pen_last || !(pen_last->flags&2)))return;
            if(e.action==Action::down && (e.code&PEN_FLAG_INVERTED) && (!pen_last || !(pen_last->code&PEN_FLAG_INVERTED))) {
                auto hover=e;hover.action=Action::move;hover.flags=1;hover.pressure=0;hover.code&=~PEN_FLAG_ERASER;
                auto setup=pointer(hover);inject(pen,&setup,1,"Windows rejected inverted pen setup");
            }
            auto info=pointer(e);
            inject(pen,&info,1,"Windows rejected pen frame");
            pen_last=e;if(e.action==Action::up || e.action==Action::cancel)pen_last->flags=static_cast<std::uint16_t>(e.flags&1);if(!pen_last->flags)pen_last.reset();return;
        }
        need(touch!=nullptr,"Touch input unavailable");
        if(e.action==Action::down)need(!contacts.contains(e.id) && contacts.size()<10,"Duplicate or excessive touch contacts");
        else if(!contacts.contains(e.id))return;
        if(contacts.empty())touch_primary=e.id;
        contacts[e.id]=e;
        std::vector<POINTER_TYPE_INFO> frame;
        for(const auto& [id,event]:contacts) { auto sample=event;if(id!=e.id)sample.action=Action::move;frame.push_back(pointer(sample)); }
        inject(touch,frame.data(),static_cast<UINT32>(frame.size()),"Windows rejected touch frame");
        if(e.action==Action::up || e.action==Action::cancel)contacts.erase(e.id);
    }
    void refresh() {
        if(!lease.acquire())return;
        // Windows can cancel synthetic pointers left unrefreshed while held.
        if(touch && !contacts.empty()) {
            std::vector<POINTER_TYPE_INFO> frame;
            for(const auto& [id,event]:contacts) {(void)id;auto e=event;e.action=Action::move;frame.push_back(pointer(e));}
            inject(touch,frame.data(),static_cast<UINT32>(frame.size()),"Touch hold refresh failed");
        }
        if(pen && pen_last) {auto e=*pen_last;e.action=Action::move;auto info=pointer(e);inject(pen,&info,1,"Pen hold refresh failed");}
        if(keys.empty()&&buttons.empty()&&contacts.empty()&&(!pen_last || !(pen_last->flags&2)))lease.release();
    }
    void reset() noexcept {
        while(!keys.empty()) { auto e=keys.begin()->second;e.action=Action::up;try { key(e); }catch(...) {keys.erase(e.id);} }
        while(!buttons.empty()) { auto e=buttons.begin()->second;e.action=Action::up;try { mouse_event(e,false); }catch(...) {buttons.erase(e.code);} }
        if(touch && !contacts.empty()) { std::vector<POINTER_TYPE_INFO> frame;for(const auto& [id,event]:contacts) { (void)id;auto e=event;e.action=Action::cancel;e.flags=0;frame.push_back(pointer(e)); }try{inject(touch,frame.data(),static_cast<UINT32>(frame.size()),"Touch reset failed");}catch(...){}contacts.clear(); }
        if(pen && pen_last) { auto e=*pen_last;e.action=(e.flags&2)?Action::cancel:Action::move;e.flags=0;auto info=pointer(e);try{inject(pen,&info,1,"Pen reset failed");}catch(...){}pen_last.reset(); }
        lease.release();
    }
    ~Injector() { reset();if(keyboard_hook)UnhookWindowsHookEx(keyboard_hook);if(mouse_hook)UnhookWindowsHookEx(mouse_hook);active=nullptr;if(touch)DestroySyntheticPointerDevice(touch);if(pen)DestroySyntheticPointerDevice(pen); }
    void apply(const Event& e) {
        validate(e);if(!lease.acquire())return;
        // Snapshot only after any other remote gesture ended, so its held
        // keys are not mistaken for permanently held local physical keys.
        if(!physical_initialized){for(unsigned vk=1;vk<256;++vk)physical[vk]=(GetAsyncKeyState(static_cast<int>(vk))&0x8000)!=0;physical_initialized=true;}
        if(e.kind==Kind::reset)reset();else if(e.kind==Kind::mouse)mouse_event(e);else if(e.kind==Kind::key)key(e);else pointer_event(e);
        if(keys.empty()&&buttons.empty()&&contacts.empty()&&(!pen_last || !(pen_last->flags&2)))lease.release();
    }
};
}
struct Broker::State {
    HANDLE input=nullptr,output=nullptr,process=nullptr;unsigned caps=0,sequence=0;
    ~State() { close(input);if(process)WaitForSingleObject(process,3000);close(output);close(process); }
};
Broker::Broker(const std::wstring& source):state_(std::make_unique<State>()) {
    bool found=false;for(const auto& monitor:display::active_monitors())if(monitor.source==source)found=true;
    need(found && source.find(L'"')==std::wstring::npos,"Invalid input monitor identity");
    auto& s=*state_;SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE child_in=nullptr,child_out=nullptr;
    struct Handles { HANDLE& a;HANDLE& b;~Handles(){close(a);close(b);} } cleanup{child_in,child_out};
    need(CreatePipe(&child_in,&s.input,&security,65536)!=FALSE,"Cannot create input watchdog pipe");
    need(CreatePipe(&s.output,&child_out,&security,4096)!=FALSE,"Cannot create watchdog status pipe");
    need(SetHandleInformation(s.input,HANDLE_FLAG_INHERIT,0) && SetHandleInformation(s.output,HANDLE_FLAG_INHERIT,0),"Cannot restrict pipe inheritance");
    wchar_t path[32768]{};need(GetModuleFileNameW(nullptr,path,32768)>0,"Cannot find input broker executable");
    std::wstring command=L"\""+std::wstring(path)+L"\" input-broker --display \""+source+L"\"";
    SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<std::uint8_t> attributes(size);
    auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());need(InitializeProcThreadAttributeList(list,1,0,&size)!=FALSE,"Cannot initialize broker handles");
    struct DeleteAttributes { LPPROC_THREAD_ATTRIBUTE_LIST value;~DeleteAttributes(){DeleteProcThreadAttributeList(value);} } delete_attributes{list};
    HANDLE inherited[]={child_in,child_out};need(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)!=FALSE,"Cannot restrict broker handles");
    STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);start.StartupInfo.dwFlags=STARTF_USESTDHANDLES;start.StartupInfo.hStdInput=child_in;start.StartupInfo.hStdOutput=child_out;start.StartupInfo.hStdError=child_out;start.lpAttributeList=list;
    PROCESS_INFORMATION process{};need(CreateProcessW(path,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&start.StartupInfo,&process)!=FALSE,"Cannot launch input watchdog");
    s.process=process.hProcess;CloseHandle(process.hThread);close(child_in);close(child_out);
    const auto until=Clock::now()+std::chrono::seconds(3);DWORD available=0;
    while(Clock::now()<until) { need(PeekNamedPipe(s.output,nullptr,0,nullptr,&available,nullptr)!=FALSE,"Input watchdog failed initialization");if(available>=header_size)break;std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    need(available>=header_size,"Input watchdog initialization timeout");HeaderBytes bytes{};DWORD read=0;need(ReadFile(s.output,bytes.data(),header_size,&read,nullptr) && read==header_size,"Input watchdog status failed");
    s.caps=decode_header(bytes).sequence;need(s.caps && !(s.caps&~all_caps),"Invalid input capabilities");
}
Broker::~Broker()=default;
unsigned Broker::capabilities() const { return state_->caps; }
void Broker::send(std::span<const Event> events) {
    if(WaitForSingleObject(state_->process,0)!=WAIT_TIMEOUT) {
        DWORD available=0,read=0;std::string message;
        if(PeekNamedPipe(state_->output,nullptr,0,nullptr,&available,nullptr) && available) {message.resize(std::min<DWORD>(available,1024));ReadFile(state_->output,message.data(),static_cast<DWORD>(message.size()),&read,nullptr);message.resize(read);}
        throw std::runtime_error("Input watchdog stopped: "+message);
    }
    write(state_->input,encode_header({1,state_->sequence++,static_cast<std::uint32_t>(events.size())}));write(state_->input,encode_events(events));
}
int broker_main(const std::wstring& source) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        Injector injector(source);write(GetStdHandle(STD_OUTPUT_HANDLE),encode_header({1,injector.caps(),0}));
        const auto pipe=GetStdHandle(STD_INPUT_HANDLE);auto heartbeat=Clock::now(),refresh=heartbeat;std::vector<std::uint8_t> pending;unsigned sequence=0;
        for(;;) {
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
            DWORD available=0;if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr))break;
            if(available) {
                need(pending.size()+available<=65536,"Input broker queue exceeds limit");std::vector<std::uint8_t> bytes(available);DWORD read=0;need(ReadFile(pipe,bytes.data(),available,&read,nullptr)!=FALSE,"Input broker read failed");pending.insert(pending.end(),bytes.begin(),bytes.begin()+read);
                while(pending.size()>=header_size) {
                    const auto header=decode_header(std::span(pending).first(header_size));const auto total=header_size+header.count*event_size;if(pending.size()<total)break;
                    need(header.epoch==1 && header.sequence==sequence++,"Input broker sequence mismatch");const bool geometry_ready=injector.geometry();
                    if(geometry_ready)for(const auto& event:decode_events(std::span(pending).subspan(header_size,header.count*event_size),header.count))injector.apply(event);
                    pending.erase(pending.begin(),pending.begin()+total);heartbeat=Clock::now();
                }
            }
            if(Clock::now()-heartbeat>(sequence?std::chrono::milliseconds(1500):std::chrono::milliseconds(10000)))break;
            if(sequence && Clock::now()-refresh>std::chrono::milliseconds(50)) {if(injector.geometry())injector.refresh();refresh=Clock::now();}
            MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
        }return 0;
    }catch(const std::exception& error) {const std::string message=std::string(error.what())+" win32="+std::to_string(GetLastError());DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message.data(),static_cast<DWORD>(message.size()),&written,nullptr);return 1;}
}
}
