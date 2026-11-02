// Generates native OS input only inside the visible DM preview for integration QA.
#include <windows.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
namespace {
void need(bool ok) {if(!ok)throw std::runtime_error("Input exercise failed");}
void pause() {std::this_thread::sleep_for(std::chrono::milliseconds(140));}
void key(WORD scan,bool up) {INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wScan=scan;i.ki.dwFlags=KEYEVENTF_SCANCODE|(up?KEYEVENTF_KEYUP:0);need(SendInput(1,&i,sizeof(i))==1);pause();}
}
int main() {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HSYNTHETICPOINTERDEVICE touch=nullptr,pen=nullptr;
    try {
        HWND preview=nullptr;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(std::chrono::steady_clock::now()<until) {preview=FindWindowW(L"DMVideoFeasibilityPreview",nullptr);if(preview && reinterpret_cast<ULONG_PTR>(GetPropW(preview,L"DM.InputReady"))==15)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        need(preview && GetPropW(preview,L"DM.InputReady"));
        RECT client{};GetClientRect(preview,&client);POINT point{client.right/2,client.bottom/2};ClientToScreen(preview,&point);
        SetForegroundWindow(preview);SetCursorPos(point.x,point.y);pause();
        INPUT mouse{};mouse.type=INPUT_MOUSE;mouse.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;need(SendInput(1,&mouse,sizeof(mouse))==1);pause();SetCursorPos(point.x+30,point.y+20);pause();mouse.mi.dwFlags=MOUSEEVENTF_LEFTUP;need(SendInput(1,&mouse,sizeof(mouse))==1);pause();
        key(30,false);key(30,true); // physical scan A; no text payload in logs
        touch=CreateSyntheticPointerDevice(PT_TOUCH,2,POINTER_FEEDBACK_DEFAULT);pen=CreateSyntheticPointerDevice(PT_PEN,1,POINTER_FEEDBACK_DEFAULT);need(touch && pen);
        POINTER_TYPE_INFO touches[2]{};
        for(unsigned n=0;n<2;++n) {auto& t=touches[n];t.type=PT_TOUCH;t.touchInfo.pointerInfo.pointerType=PT_TOUCH;t.touchInfo.pointerInfo.pointerId=n+10;t.touchInfo.pointerInfo.ptPixelLocation={point.x+static_cast<LONG>(n)*60,point.y+40};t.touchInfo.pointerInfo.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT|(n?0:POINTER_FLAG_PRIMARY);}
        need(InjectSyntheticPointerInput(touch,touches,1)!=FALSE);pause();touches[0].touchInfo.pointerInfo.pointerFlags=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT|POINTER_FLAG_PRIMARY;need(InjectSyntheticPointerInput(touch,touches,2)!=FALSE);pause();
        for(auto& t:touches) {t.touchInfo.pointerInfo.pointerFlags=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;t.touchInfo.pointerInfo.ptPixelLocation.y+=25;}need(InjectSyntheticPointerInput(touch,touches,2)!=FALSE);pause();
        for(auto& t:touches)t.touchInfo.pointerInfo.pointerFlags=POINTER_FLAG_UP;need(InjectSyntheticPointerInput(touch,touches,2)!=FALSE);pause();
        POINTER_TYPE_INFO p{};p.type=PT_PEN;auto& info=p.penInfo;info.pointerInfo.pointerType=PT_PEN;info.pointerInfo.pointerId=1;info.pointerInfo.ptPixelLocation={point.x,point.y+70};info.pointerInfo.pointerFlags=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE;info.penMask=PEN_MASK_PRESSURE|PEN_MASK_TILT_X|PEN_MASK_TILT_Y;info.tiltX=-30;info.tiltY=40;
        need(InjectSyntheticPointerInput(pen,&p,1)!=FALSE);pause();info.pointerInfo.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;info.pressure=750;info.penFlags=PEN_FLAG_BARREL;need(InjectSyntheticPointerInput(pen,&p,1)!=FALSE);pause();info.pointerInfo.pointerFlags=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;for(unsigned n=0;n<50;++n){info.pointerInfo.ptPixelLocation.x+=1;info.pressure=750+n*3;need(InjectSyntheticPointerInput(pen,&p,1)!=FALSE);std::this_thread::sleep_for(std::chrono::milliseconds(2));}pause();info.pointerInfo.pointerFlags=POINTER_FLAG_UP|POINTER_FLAG_INRANGE;info.pressure=0;need(InjectSyntheticPointerInput(pen,&p,1)!=FALSE);pause();
        key(66,false);key(66,true); // F8 pause
        key(66,false);key(66,true); // F8 resume
        DestroySyntheticPointerDevice(pen);pen=nullptr;DestroySyntheticPointerDevice(touch);touch=nullptr;
        std::cout<<"RECEIVER_EXERCISE_SENT mouse-drag keyboard two-contact-touch pen-pressure-tilt-barrel F8\n";return 0;
    }catch(const std::exception& e){if(touch)DestroySyntheticPointerDevice(touch);if(pen)DestroySyntheticPointerDevice(pen);std::cerr<<e.what()<<'\n';return 1;}
}
