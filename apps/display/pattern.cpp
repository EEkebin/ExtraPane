#include <windows.h>
#include <dm/display.hpp>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <windowsx.h>
#include <iostream>

namespace {
struct Segment {POINT from,to;int width;COLORREF color;};
struct Pattern { unsigned tick=0,seconds=0,animation=0; std::wstring source; std::wstring typed,status;std::vector<Segment> strokes;std::map<UINT32,POINT> contacts;POINT mouse{};bool dragging=false;unsigned mouse_down=0,touch_down=0,pen_down=0,chars=0,pressure_max=0; };
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto* pattern=reinterpret_cast<Pattern*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        pattern=static_cast<Pattern*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(pattern));
    }
    if(pattern && (message==WM_POINTERDOWN || message==WM_POINTERUPDATE || message==WM_POINTERUP)) {
        const auto id=GET_POINTERID_WPARAM(wparam);POINTER_INFO pointer{};
        if(GetPointerInfo(id,&pointer)) {
            POINT point=pointer.ptPixelLocation;ScreenToClient(window,&point);
            int width=5;COLORREF color=RGB(255,255,255);
            if(pointer.pointerType==PT_PEN) {
                if(message==WM_POINTERDOWN)++pattern->pen_down;
                POINTER_PEN_INFO pen{};if(GetPointerPenInfo(id,&pen)) {
                    width=2+static_cast<int>(pen.pressure*18/1024);color=(pen.penFlags&PEN_FLAG_ERASER)?RGB(0,0,0):RGB(255,255,255);
                    pattern->pressure_max=std::max(pattern->pressure_max,pen.pressure);
                    pattern->status=L"PEN pressure="+std::to_wstring(pen.pressure)+L" mask="+std::to_wstring(pen.penMask)+L" tilt="+std::to_wstring(pen.tiltX)+L","+std::to_wstring(pen.tiltY)+L" flags="+std::to_wstring(pen.penFlags);
                }
            } else if(pointer.pointerType==PT_TOUCH) {if(message==WM_POINTERDOWN)++pattern->touch_down;UINT32 count=0;GetPointerFrameTouchInfo(id,&count,nullptr);pattern->status=L"NATIVE TOUCH contacts="+std::to_wstring(count)+L" id="+std::to_wstring(id);color=RGB(255,140,255);}
            if(pointer.pointerFlags&POINTER_FLAG_INCONTACT) { if(pattern->contacts.contains(id) && pattern->strokes.size()<8192)pattern->strokes.push_back({pattern->contacts[id],point,width,color});pattern->contacts[id]=point; }
            else pattern->contacts.erase(id);
            InvalidateRect(window,nullptr,FALSE);
        }return 0;
    }
    if(pattern && (message==WM_LBUTTONDOWN || message==WM_LBUTTONUP || message==WM_MOUSEMOVE)) {
        if((static_cast<ULONG_PTR>(GetMessageExtraInfo())&0xffffff00)==0xff515700)return 0;
        POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        if(message==WM_LBUTTONDOWN){++pattern->mouse_down;pattern->dragging=true;SetCapture(window);SetFocus(window);}
        if(pattern->dragging && message==WM_MOUSEMOVE && pattern->strokes.size()<8192)pattern->strokes.push_back({pattern->mouse,point,3,RGB(0,0,0)});
        pattern->mouse=point;if(message==WM_LBUTTONUP){pattern->dragging=false;ReleaseCapture();}
        InvalidateRect(window,nullptr,FALSE);return 0;
    }
    if(pattern && message==WM_CHAR) {++pattern->chars; if(wparam==8){if(!pattern->typed.empty())pattern->typed.pop_back();}else if(wparam>=32 && pattern->typed.size()<120)pattern->typed.push_back(static_cast<wchar_t>(wparam));InvalidateRect(window,nullptr,FALSE);return 0; }
    if(message==WM_TIMER && pattern && wparam==2) {++pattern->animation;InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_TIMER && pattern) {
        bool present=false;
        try { for(const auto& m:dm::display::active_monitors()) if(m.dm_virtual && m.source==pattern->source) present=true; }
        catch(const std::exception&) {}
        if(!present || ++pattern->tick>=pattern->seconds) DestroyWindow(window);
        else InvalidateRect(window,nullptr,FALSE);
        return 0;
    }
    if(message==WM_PAINT && pattern) {
        PAINTSTRUCT paint{}; const auto dc=BeginPaint(window,&paint);
        RECT bounds{};GetClientRect(window,&bounds);
        const COLORREF colors[]={RGB(220,40,40),RGB(40,190,65),RGB(40,80,220),RGB(225,180,30)};
        for(int i=0;i<4;++i) {
            RECT area{(i%2)*bounds.right/2,(i/2)*bounds.bottom/2,((i%2)+1)*bounds.right/2,((i/2)+1)*bounds.bottom/2};
            const auto brush=CreateSolidBrush(colors[i]);FillRect(dc,&area,brush);DeleteObject(brush);
        }
        const LONG bar_x=static_cast<LONG>((pattern->animation*9)%static_cast<unsigned>(std::max(1L,bounds.right)));
        RECT bar{bar_x,bounds.bottom-50,std::min(bounds.right,bar_x+80),bounds.bottom-10};
        FillRect(dc,&bar,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        for(const auto& segment:pattern->strokes) {auto pen=CreatePen(PS_SOLID,segment.width,segment.color);auto old=SelectObject(dc,pen);MoveToEx(dc,segment.from.x,segment.from.y,nullptr);LineTo(dc,segment.to.x,segment.to.y);SelectObject(dc,old);DeleteObject(pen);}
        const std::wstring text=L"ExtraPane independent monitor / input test\nDraw with mouse, fingers or pen. F8 on laptop pauses control.\nLive counter: "+std::to_wstring(pattern->tick)+L" seconds\n"+pattern->status+L"\nTyped: "+pattern->typed;
        SetBkColor(dc,RGB(0,0,0));SetTextColor(dc,RGB(255,255,255));
        RECT caption{24,24,bounds.right-24,bounds.bottom-24};DrawTextW(dc,text.c_str(),-1,&caption,DT_LEFT|DT_TOP);
        EndPaint(window,&paint);return 0;
    }
    if(message==WM_DESTROY) { PostQuitMessage(0);return 0; }
    return DefWindowProcW(window,message,wparam,lparam);
}
}
void show_display_pattern(unsigned seconds) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    dm::display::Monitor selected;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(35);
    while(selected.source.empty() && std::chrono::steady_clock::now()<until) {
        for(const auto& monitor:dm::display::active_monitors()) if(monitor.dm_virtual) selected=monitor;
        if(selected.source.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if(selected.source.empty()) throw std::runtime_error("No active ExtraPane monitor appeared for the diagnostic window");
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"DMThirdMonitorPattern";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&cls)) throw std::runtime_error("Cannot register diagnostic window");
    Pattern pattern;pattern.seconds=seconds;pattern.source=selected.source;
    const auto window=CreateWindowExW(0,cls.lpszClassName,L"ExtraPane monitor test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,selected.left+60,selected.top+60,900,540,nullptr,nullptr,instance,&pattern);
    if(!window) throw std::runtime_error("Cannot create diagnostic window on the virtual monitor");
    ShowWindow(window,SW_MAXIMIZE);
    SetTimer(window,1,1000,nullptr);
    SetTimer(window,2,16,nullptr);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message);DispatchMessageW(&message); }
    std::cout<<"INPUT_OBSERVED mouse="<<pattern.mouse_down<<" chars="<<pattern.chars<<" touch="<<pattern.touch_down<<" pen="<<pattern.pen_down<<" pressure_max="<<pattern.pressure_max<<std::endl;
}
