#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <dm/text_focus.hpp>
#include <dm/text_focus_policy.hpp>
#include <dm/display.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <iostream>
#include <syncstream>
namespace dm::input {
namespace {
bool normal_desktop(){
    const auto desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);if(!desktop)return false;
    wchar_t name[128]{};DWORD size=0;const bool ok=GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&size)&&_wcsicmp(name,L"Default")==0;CloseDesktop(desktop);return ok;
}
}
struct TextFocusProbe::State {
    std::wstring monitor;std::mutex mutex;Event touch;std::uint64_t generation=0,expires=0;std::atomic_uint32_t confirmed=0;std::jthread worker;
    explicit State(std::wstring source):monitor(std::move(source)),worker([this](std::stop_token stop){run(stop);}){}
    void run(std::stop_token stop){
        const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized))return;
        struct Com{~Com(){CoUninitialize();}}com;
        Microsoft::WRL::ComPtr<IUIAutomation2> automation;
        if(FAILED(CoCreateInstance(__uuidof(CUIAutomation8),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))||FAILED(automation->put_ConnectionTimeout(150))||FAILED(automation->put_TransactionTimeout(150))){std::osyncstream(std::cout)<<"AUTO_KEYBOARD_UNAVAILABLE UI Automation probe unavailable"<<std::endl;return;}
        std::uint64_t handled=0;
        while(!stop.stop_requested()){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));Event event;std::uint64_t request=0,until=0;
            {std::lock_guard lock(mutex);request=generation;event=touch;until=expires;}
            if(!request||request==handled||GetTickCount64()>until||!normal_desktop())continue;
            try{
                std::optional<display::Monitor> target;for(const auto& candidate:display::active_monitors())if(candidate.source==monitor)target=candidate;
                if(!target)continue;
                const long x=target->left+static_cast<long>((std::uint64_t(event.x)*(target->right-target->left-1)+32767)/65535);
                const long y=target->top+static_cast<long>((std::uint64_t(event.y)*(target->bottom-target->top-1)+32767)/65535);
                // A local foreground window outside this display never qualifies.
                const auto foreground=GetForegroundWindow();RECT foreground_rect{};if(!foreground||!GetWindowRect(foreground,&foreground_rect)||!PtInRect(&foreground_rect,POINT{x,y}))continue;
                Microsoft::WRL::ComPtr<IUIAutomationElement> element;if(FAILED(automation->GetFocusedElement(&element))||!element)continue;
                BOOL enabled=FALSE,focused=FALSE;RECT rect{};if(FAILED(element->get_CurrentIsEnabled(&enabled))||FAILED(element->get_CurrentHasKeyboardFocus(&focused))||FAILED(element->get_CurrentBoundingRectangle(&rect)))continue;
                Microsoft::WRL::ComPtr<IUIAutomationValuePattern> value;BOOL read_only=TRUE;
                // ValuePattern.IsReadOnly is metadata. Never call CurrentValue,
                // CurrentName or TextPattern.GetText, including password fields.
                const bool editable=SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&value)))&&value&&SUCCEEDED(value->get_CurrentIsReadOnly(&read_only))&&!read_only;
                if(GetForegroundWindow()!=foreground||!normal_desktop())continue;
                if(!editable_at_touch({target->left,target->top,target->right,target->bottom},{rect.left,rect.top,rect.right,rect.bottom},x,y,enabled!=FALSE,focused!=FALSE,editable))continue;
                std::lock_guard lock(mutex);if(generation==request&&GetTickCount64()<=expires){confirmed=touch_token(event.micros);handled=request;}
            }catch(...){/* Unsupported providers or display changes never imply editable focus. */}
        }
    }
};
TextFocusProbe::TextFocusProbe(std::wstring monitor):state_(std::make_unique<State>(std::move(monitor))){}
TextFocusProbe::~TextFocusProbe()=default;
void TextFocusProbe::observe(std::span<const Event> events){
    std::lock_guard lock(state_->mutex);
    for(const auto& event:events){
        if(event.kind==Kind::reset||event.kind==Kind::key||event.kind==Kind::pen||(event.kind==Kind::mouse&&event.action==Action::down)||(event.kind==Kind::touch&&event.action==Action::cancel)){++state_->generation;state_->expires=0;state_->confirmed=0;}
        if(event.kind==Kind::touch&&event.action==Action::down){state_->touch=event;++state_->generation;state_->expires=GetTickCount64()+2000;state_->confirmed=0;}
    }
}
std::uint32_t TextFocusProbe::confirmed_touch()const noexcept{return state_->confirmed.load();}
}
