#pragma once
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <string>

// A hidden top-level window receives notification-area and Explorer restart events.
class Tray {
public:
    static constexpr UINT show_message=WM_APP+17;
    static const wchar_t* class_name(bool server){return server?L"SideScreen.Server.Tray":L"SideScreen.Client.Tray";}
    HWND window=nullptr;
    bool ready=false,active=false;
    std::function<void()> show,toggle,quit;
    Tray(bool server,std::function<void()> show_action,std::function<void()> toggle_action,std::function<void()> quit_action)
        :show(std::move(show_action)),toggle(std::move(toggle_action)),quit(std::move(quit_action)),server_(server) {
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=procedure;cls.lpszClassName=class_name(server);
        RegisterClassW(&cls);
        window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,server?L"SideScreen Server tray":L"SideScreen Client tray",WS_POPUP,0,0,0,0,nullptr,nullptr,cls.hInstance,this);
        if(!window)throw std::runtime_error("Cannot create tray window");
        data_.cbSize=sizeof(data_);data_.hWnd=window;data_.uID=1;data_.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;
        data_.uCallbackMessage=WM_APP+18;data_.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
        update(false);add();
    }
    ~Tray(){remove();if(window)DestroyWindow(window);}
    void remove(){if(ready)Shell_NotifyIconW(NIM_DELETE,&data_);ready=false;if(window)RemovePropW(window,L"SideScreen.TrayReady");}
    void update(bool running){active=running;if(window)SetPropW(window,L"SideScreen.Running",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(running?1:0)));std::wstring label=server_?L"SideScreen Server":L"SideScreen Client";label+=server_?(running?L" - Driver running":L" - Driver stopped"):(running?L" - Session active":L" - Ready");wcsncpy_s(data_.szTip,label.c_str(),_TRUNCATE);if(ready)Shell_NotifyIconW(NIM_MODIFY,&data_);}
private:
    NOTIFYICONDATAW data_{};bool server_=false;
    UINT restart_=RegisterWindowMessageW(L"TaskbarCreated");
    void add(){ready=Shell_NotifyIconW(NIM_ADD,&data_)!=FALSE;if(ready){SetPropW(window,L"SideScreen.TrayReady",reinterpret_cast<HANDLE>(1));data_.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&data_);}}
    void menu(){
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Open SideScreen");
        AppendMenuW(menu,MF_STRING|(!server_&&!active?MF_GRAYED:0),2,server_?(active?L"Stop Driver":L"Start Driver"):L"Disconnect");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,3,L"Exit");
        POINT point{};GetCursorPos(&point);SetForegroundWindow(window);
        const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
        DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
        if(command==1)show();else if(command==2)toggle();else if(command==3)quit();
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
        if(message==WM_NCCREATE)SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
        auto* self=reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(self){
            if(message==self->restart_){self->ready=false;self->add();return 0;}
            if(message==show_message){self->show();return 0;}
            if(message==WM_APP+18){const auto action=LOWORD(lparam);if(action==NIN_SELECT||action==NIN_KEYSELECT)self->show();else if(action==WM_CONTEXTMENU)self->menu();return 0;}
            if(message==WM_COMMAND){if(wparam==1)self->show();else if(wparam==2)self->toggle();else if(wparam==3)self->quit();return 0;}
            if(message==WM_ENDSESSION&&wparam){self->quit();return 0;}
        }
        return DefWindowProcW(hwnd,message,wparam,lparam);
    }
};
