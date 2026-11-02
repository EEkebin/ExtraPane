#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
struct Search{DWORD pid;HWND window=nullptr,bar=nullptr,stats=nullptr;};
BOOL CALLBACK collect(HWND h,LPARAM p){auto& s=*reinterpret_cast<Search*>(p);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=s.pid)return TRUE;wchar_t cls[100]{};GetClassNameW(h,cls,100);if(wcscmp(cls,L"DMVideoFeasibilityPreview")==0)s.window=h;if(wcscmp(cls,L"DMDisplayToolbar")==0)s.bar=h;if(wcscmp(cls,L"SideScreenStatistics")==0)s.stats=h;return TRUE;}
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pause(){std::this_thread::sleep_for(std::chrono::milliseconds(400));}
int main(int argc,char** argv){try{
 SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 need(argc>=2,"Supply receiver PID");Search s{static_cast<DWORD>(std::stoul(argv[1]))};EnumWindows(collect,reinterpret_cast<LPARAM>(&s));need(s.window&&s.bar,"Preview/toolbar missing");
 if(argc>2&&std::string(argv[2])=="--next-75"){
  SetForegroundWindow(s.window);PostMessageW(s.window,WM_APP+42,5,0);pause();
  PostMessageW(s.window,WM_CHAR,'7',0);pause();
  std::cout<<"NEXT_SCALE selected 75% once\n";return 0;
 }
 if(argc>2&&std::string(argv[2])=="--disconnect"){
  SetForegroundWindow(s.window);pause();INPUT keys[6]{};const WORD codes[]={VK_CONTROL,VK_SHIFT,'Q','Q',VK_SHIFT,VK_CONTROL};
  for(int i=0;i<6;++i){keys[i].type=INPUT_KEYBOARD;keys[i].ki.wVk=codes[i];keys[i].ki.dwFlags=i>=3?KEYEVENTF_KEYUP:0;}
  need(SendInput(6,keys,sizeof(INPUT))==6,"Shortcut injection failed");
  for(int i=0;i<50&&IsWindow(s.window);++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
  need(!IsWindow(s.window),"Ctrl+Shift+Q did not disconnect");std::cout<<"DISCONNECT_SHORTCUT passed Ctrl+Shift+Q\n";return 0;
 }
 need(s.stats&&!IsWindowVisible(s.stats),"Statistics must start hidden");
 auto statistics_menu=[&]{
  SetForegroundWindow(s.window);PostMessageW(s.window,WM_APP+42,5,0);pause();
  // These key messages target the preview's native menu loop, not another app.
  PostMessageW(s.window,WM_KEYDOWN,VK_DOWN,0);PostMessageW(s.window,WM_KEYUP,VK_DOWN,0);pause();
  PostMessageW(s.window,WM_KEYDOWN,VK_RETURN,0);PostMessageW(s.window,WM_KEYUP,VK_RETURN,0);pause();
 };
 statistics_menu();need(IsWindowVisible(s.stats)!=FALSE,"Settings > Show statistics did not reveal overlay");
 need((GetWindowLongPtrW(s.stats,GWL_EXSTYLE)&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE),"Statistics must not take input");
 statistics_menu();need(!IsWindowVisible(s.stats),"Settings > Show statistics did not hide overlay");
 need((GetWindowLongPtrW(s.window,GWL_STYLE)&WS_THICKFRAME)!=0,"Preview must start resizable");
 SetWindowPos(s.window,nullptr,80,80,1100,720,SWP_NOZORDER|SWP_NOACTIVATE);pause();RECT before{};GetWindowRect(s.window,&before);
 SendMessageW(s.window,WM_KEYDOWN,VK_F11,0);SendMessageW(s.window,WM_KEYUP,VK_F11,0);pause();
 need((GetWindowLongPtrW(s.window,GWL_STYLE)&WS_THICKFRAME)==0,"F11 did not enter fullscreen");
 RECT full{};GetWindowRect(s.window,&full);MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(s.window,MONITOR_DEFAULTTONEAREST),&m);need(EqualRect(&full,&m.rcMonitor)!=FALSE,"Fullscreen bounds differ from monitor");
 SendMessageW(s.window,WM_APP+42,3,0);pause();RECT restored{};GetWindowRect(s.window,&restored);need(EqualRect(&before,&restored)!=FALSE,"Window placement was not restored");
 SendMessageW(s.window,WM_APP+42,2,0);pause();need(IsZoomed(s.window)!=FALSE,"Maximize failed");SendMessageW(s.window,WM_APP+42,2,0);pause();need(!IsZoomed(s.window),"Restore failed");
 SendMessageW(s.window,WM_APP+42,1,0);pause();need(IsIconic(s.window)!=FALSE,"Minimize failed");ShowWindow(s.window,SW_RESTORE);pause();
 // Native pointer checks are restricted to the owned preview.
 SendMessageW(s.window,WM_KEYDOWN,VK_F10,0);SendMessageW(s.window,WM_KEYUP,VK_F10,0);need(IsWindowVisible(s.bar)!=FALSE,"Toolbar keyboard reveal failed");
 POINT cursor{};GetCursorPos(&cursor);RECT bar{};GetWindowRect(s.bar,&bar);
 if(!PtInRect(&bar,cursor)){for(int i=0;i<25&&IsWindowVisible(s.bar);++i)std::this_thread::sleep_for(std::chrono::milliseconds(200));need(!IsWindowVisible(s.bar),"Toolbar did not auto-hide");}
 RECT client{};GetClientRect(s.window,&client);
 SendMessageW(s.window,WM_MOUSEMOVE,0,MAKELPARAM(client.right-10,1));pause();need(!IsWindowVisible(s.bar),"Top-right corner must not reveal toolbar");
 POINT top{client.right/2,1};ClientToScreen(s.window,&top);SetCursorPos(top.x,top.y);SendMessageW(s.window,WM_MOUSEMOVE,0,MAKELPARAM(client.right/2,1));
 std::this_thread::sleep_for(std::chrono::milliseconds(1000));need(IsWindowVisible(s.bar)!=FALSE,"Top-center hover reveal failed");SetCursorPos(cursor.x,cursor.y);
 ShowWindow(s.bar,SW_HIDE);
 auto touch=CreateSyntheticPointerDevice(PT_TOUCH,1,POINTER_FEEDBACK_NONE);need(touch!=nullptr,"Cannot create touch test device");
 auto contact=[&](int x,int y,DWORD flags){POINTER_TYPE_INFO p{};p.type=PT_TOUCH;p.touchInfo.pointerInfo.pointerType=PT_TOUCH;p.touchInfo.pointerInfo.pointerId=10;p.touchInfo.pointerInfo.ptPixelLocation={x,y};ClientToScreen(s.window,&p.touchInfo.pointerInfo.ptPixelLocation);p.touchInfo.pointerInfo.pointerFlags=flags;need(InjectSyntheticPointerInput(touch,&p,1)!=FALSE,"Touch injection failed");pause();};
 const auto down=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;
 SetForegroundWindow(s.window);contact(client.right-30,4,down);contact(client.right-30,4,POINTER_FLAG_UP);need(!IsWindowVisible(s.bar),"Corner tap was intercepted by overlay");
 contact(client.right/2,2,down);need(!IsWindowVisible(s.bar),"Center tap must wait for swipe");contact(client.right/2,80,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);contact(client.right/2,80,POINTER_FLAG_UP);need(IsWindowVisible(s.bar)!=FALSE,"Center swipe reveal failed");DestroySyntheticPointerDevice(touch);
 need(IsWindow(s.window)!=FALSE,"Preview closed during window changes");
 std::cout<<"WINDOW_CHECK passed statistics-hidden show-hide-menu click-through resize fullscreen bounds restore maximize minimize toolbar-hide edge-reveal keyboard-reveal\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
