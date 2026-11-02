#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
struct Search{DWORD pid;HWND window=nullptr,bar=nullptr;};
BOOL CALLBACK collect(HWND h,LPARAM p){auto& s=*reinterpret_cast<Search*>(p);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=s.pid)return TRUE;wchar_t cls[100]{};GetClassNameW(h,cls,100);if(wcscmp(cls,L"DMVideoFeasibilityPreview")==0)s.window=h;if(wcscmp(cls,L"DMDisplayToolbar")==0)s.bar=h;return TRUE;}
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pause(){std::this_thread::sleep_for(std::chrono::milliseconds(400));}
int main(int argc,char** argv){try{
 need(argc==2,"Supply receiver PID");Search s{static_cast<DWORD>(std::stoul(argv[1]))};EnumWindows(collect,reinterpret_cast<LPARAM>(&s));need(s.window&&s.bar,"Preview/toolbar missing");
 need((GetWindowLongPtrW(s.window,GWL_STYLE)&WS_THICKFRAME)!=0,"Preview must start resizable");
 SetWindowPos(s.window,nullptr,80,80,1100,720,SWP_NOZORDER|SWP_NOACTIVATE);pause();RECT before{};GetWindowRect(s.window,&before);
 SendMessageW(s.window,WM_KEYDOWN,VK_F11,0);SendMessageW(s.window,WM_KEYUP,VK_F11,0);pause();
 need((GetWindowLongPtrW(s.window,GWL_STYLE)&WS_THICKFRAME)==0,"F11 did not enter fullscreen");
 RECT full{};GetWindowRect(s.window,&full);MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(s.window,MONITOR_DEFAULTTONEAREST),&m);need(EqualRect(&full,&m.rcMonitor)!=FALSE,"Fullscreen bounds differ from monitor");
 SendMessageW(s.window,WM_APP+42,3,0);pause();RECT restored{};GetWindowRect(s.window,&restored);need(EqualRect(&before,&restored)!=FALSE,"Window placement was not restored");
 SendMessageW(s.window,WM_APP+42,2,0);pause();need(IsZoomed(s.window)!=FALSE,"Maximize failed");SendMessageW(s.window,WM_APP+42,2,0);pause();need(!IsZoomed(s.window),"Restore failed");
 SendMessageW(s.window,WM_APP+42,1,0);pause();need(IsIconic(s.window)!=FALSE,"Minimize failed");ShowWindow(s.window,SW_RESTORE);pause();
 // No global mouse/key injection: only the owned preview receives test messages.
 SendMessageW(s.window,WM_KEYDOWN,VK_F10,0);SendMessageW(s.window,WM_KEYUP,VK_F10,0);need(IsWindowVisible(s.bar)!=FALSE,"Toolbar keyboard reveal failed");
 POINT cursor{};GetCursorPos(&cursor);RECT bar{};GetWindowRect(s.bar,&bar);
 if(!PtInRect(&bar,cursor)){std::this_thread::sleep_for(std::chrono::seconds(3));need(!IsWindowVisible(s.bar),"Toolbar did not auto-hide");}
 SendMessageW(s.window,WM_MOUSEMOVE,0,MAKELPARAM(100,1));pause();need(IsWindowVisible(s.bar)!=FALSE,"Top-edge reveal failed");
 need(IsWindow(s.window)!=FALSE,"Preview closed during window changes");
 std::cout<<"WINDOW_CHECK passed resize fullscreen bounds restore maximize minimize toolbar-hide edge-reveal keyboard-reveal\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
