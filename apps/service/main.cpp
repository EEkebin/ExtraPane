#include "../winui/backend.hpp"
#include <dm/display.hpp>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    HANDLE pipe=INVALID_HANDLE_VALUE,parent=nullptr,instance=nullptr;bool rule=false;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Child engine;
    auto report=[&](const std::string& text){DWORD count=0;return WriteFile(pipe,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)&&count==text.size();};
    int result=0;
    try {
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::wstring name;DWORD pid=0;bool input=false,debug=false;
        for(int i=1;i<argc;++i){std::wstring arg=argv[i];if(arg==L"--pipe"&&i+1<argc)name=argv[++i];else if(arg==L"--parent-pid"&&i+1<argc)pid=std::stoul(argv[++i]);else if(arg==L"--input")input=true;else if(arg==L"--debug")debug=true;else throw std::runtime_error("Invalid service option");}LocalFree(argv);
        if(!pid||!name.starts_with(L"\\\\.\\pipe\\SideScreen-")||name.size()>100)throw std::runtime_error("Invalid service channel");
        parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!parent)throw std::runtime_error("Launcher is unavailable");
        wchar_t path[32768]{};DWORD length=32768;
        if(!QueryFullProcessImageNameW(parent,0,path,&length)||_wcsicmp(path,(directory()/L"SideScreen.exe").c_str())!=0)throw std::runtime_error("Unexpected launcher executable");
        pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);if(pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open launcher channel");
        ULONG server=0;if(!GetNamedPipeServerProcessId(pipe,&server)||server!=pid)throw std::runtime_error("Launcher channel identity mismatch");
        instance=CreateMutexW(nullptr,FALSE,L"Local\\SideScreen.DriverRuntime");if(!instance||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("SideScreen driver is already running");
        firewall(true);rule=true;
        std::vector<std::wstring> args{L"host",L"--lan",L"--bind",L"auto",L"--parent-pid",std::to_wstring(GetCurrentProcessId()),L"--allow-screen",L"--extend",L"--serve",L"--keep-layout",L"--client-mode",L"--frames",debug?L"3600":L"0",L"--timeout-ms",L"0"};if(input)args.push_back(L"--allow-input");
        engine.start((directory()/L"dm-view.exe").wstring(),args);
        while(WaitForSingleObject(parent,25)==WAIT_TIMEOUT){
            DWORD available=0;if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)||available)break;
            auto output=engine.drain();if(!output.empty()&&!report(output))break;
            if(WaitForSingleObject(engine.process,0)==WAIT_OBJECT_0){report(engine.drain());break;}
        }
    }catch(const std::exception& e){if(pipe!=INVALID_HANDLE_VALUE)report(std::string("error: ")+e.what()+"\n");result=1;}
    if(engine.process){try{for(const auto& monitor:dm::display::active_monitors())if(monitor.dm_virtual)dm::display::remember_last_mode(monitor.source);}catch(...){}}
    engine.stop();if(rule){try{firewall(false);}catch(...){result=1;}}
    if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);if(parent)CloseHandle(parent);if(instance)CloseHandle(instance);CoUninitialize();return result;
}
