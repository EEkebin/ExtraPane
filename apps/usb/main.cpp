#include <windows.h>
#include <dm/usb_session.hpp>
#include <dm/tls.hpp>
#include <charconv>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
namespace {
std::atomic_bool stopped=false,eject_requested=false,active=false,quit_after_eject=false,clean_ejected=false;
std::mutex output_mutex;HANDLE channel=INVALID_HANDLE_VALUE,parent=nullptr;
BOOL WINAPI control(DWORD event){if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT||event==CTRL_CLOSE_EVENT){stopped=true;return TRUE;}return FALSE;}
void report(const std::string& line){if(line.starts_with("USB_STARTED"))active=true;if(line.starts_with("LISTENING")){active=false;clean_ejected=false;}if(line.starts_with("USB_EJECTED receiving"))clean_ejected=true;std::lock_guard lock(output_mutex);auto text=line+"\n";if(channel!=INVALID_HANDLE_VALUE){DWORD written=0;if(!WriteFile(channel,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)||written!=text.size())stopped=true;}else std::cout<<line<<std::endl;}
unsigned number(const std::wstring& text,unsigned maximum){if(text.empty()||!std::all_of(text.begin(),text.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))throw std::invalid_argument("Invalid numeric option");const auto value=std::stoull(text);if(!value||value>maximum)throw std::invalid_argument("Numeric option out of range");return static_cast<unsigned>(value);}
std::string ascii(const std::wstring& text){if(std::any_of(text.begin(),text.end(),[](wchar_t c){return c>127;}))throw std::invalid_argument("Expected an ASCII option");std::string result;result.reserve(text.size());for(auto c:text)result.push_back(static_cast<char>(c));return result;}
void ipv4(const std::string& text){std::size_t offset=0;for(unsigned n=0;n<4;++n){const auto at=text.find('.',offset);if((n<3)==(at==text.npos))throw std::invalid_argument("Expected an IPv4 literal");const auto part=text.substr(offset,(at==text.npos?text.size():at)-offset);unsigned value=0;const auto [end,error]=std::from_chars(part.data(),part.data()+part.size(),value);if(error!=std::errc{}||end!=part.data()+part.size()||part.empty()||value>255||(n==0&&(!value||value==127||value>=224)))throw std::invalid_argument("Expected a reachable unicast IPv4 literal");offset=at+1;}}
std::vector<unsigned char> sid(HANDLE process){HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))throw std::runtime_error("Cannot inspect launcher identity");DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<unsigned char> data(size);const auto ok=GetTokenInformation(token,TokenUser,data.data(),size,&size);CloseHandle(token);if(!ok)throw std::runtime_error("Cannot inspect launcher user");return data;}
void validate_channel(const std::wstring& name){
    const std::wstring prefix=L"\\\\.\\pipe\\ExtraPane-";if(!name.starts_with(prefix)||name.size()!=prefix.size()+32||!std::all_of(name.begin()+prefix.size(),name.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}))throw std::invalid_argument("Invalid local launcher channel");
}
void open_channel(const std::wstring& name,unsigned pid){
    validate_channel(name);
    parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!parent)throw std::runtime_error("Launcher unavailable");auto own=sid(GetCurrentProcess()),launcher=sid(parent);if(!EqualSid(reinterpret_cast<TOKEN_USER*>(own.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(launcher.data())->User.Sid))throw std::runtime_error("Launcher user mismatch");
    channel=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);if(channel==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open protected launcher channel");ULONG server=0;if(!GetNamedPipeServerProcessId(channel,&server)||server!=pid)throw std::runtime_error("Launcher pipe server mismatch");
}
std::string line(HANDLE input,std::size_t maximum,std::chrono::seconds timeout){std::string result;const auto deadline=std::chrono::steady_clock::now()+timeout;while(!stopped.load()){
    if(parent&&WaitForSingleObject(parent,0)!=WAIT_TIMEOUT){stopped=true;break;}DWORD available=0;if(PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)){if(!available){if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Launcher input timed out");Sleep(25);continue;}}
    else if(GetFileType(input)==FILE_TYPE_PIPE)throw std::runtime_error("Launcher channel closed");char byte=0;DWORD read=0;if(!ReadFile(input,&byte,1,&read,nullptr)||read!=1)throw std::runtime_error("Launcher input closed");if(byte=='\n'){if(!result.empty()&&result.back()=='\r')result.pop_back();return result;}if(!byte||result.size()>=maximum)throw std::invalid_argument("Launcher input exceeded limit");result+=byte;
}throw std::runtime_error("Launcher canceled");}
}
int wmain(int argc,wchar_t** argv){std::jthread controls,watcher;int result=0;try{
    SetConsoleCtrlHandler(control,TRUE);std::wstring command,pipe_name;std::map<std::wstring,std::wstring> values;bool require_code=false,access_stdin=false,device_stdin=false,check_config=false;
    for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--help"){std::cout<<"ExtraPane.Usb.exe list-storage | serve-usb --bind IPv4|auto --port N [--require-code] | send-usb --bind IPv4|auto --peer IPv4 --port N --device-stdin [--access-stdin]\nControls: receiver E safe eject, Q safe eject and stop; source Q stops into recovery. --check-config performs no device or network operations.\n";return 0;}
        if(arg==L"serve-usb"||arg==L"send-usb"||arg==L"list-storage"){if(!command.empty())throw std::invalid_argument("Duplicate USB command");command=arg;continue;}
        if(arg==L"--require-code"){if(require_code)throw std::invalid_argument("Duplicate option");require_code=true;continue;}if(arg==L"--access-stdin"){if(access_stdin)throw std::invalid_argument("Duplicate option");access_stdin=true;continue;}if(arg==L"--device-stdin"){if(device_stdin)throw std::invalid_argument("Duplicate option");device_stdin=true;continue;}if(arg==L"--check-config"){if(check_config)throw std::invalid_argument("Duplicate config option");check_config=true;continue;}
        if(arg!=L"--bind"&&arg!=L"--peer"&&arg!=L"--port"&&arg!=L"--pipe"&&arg!=L"--parent-pid")throw std::invalid_argument("Unknown USB option");if(++i>=argc||!values.emplace(arg,argv[i]).second)throw std::invalid_argument("Missing or duplicate USB option");
    }
    if(command==L"list-storage"){if(!values.empty()||require_code||access_stdin||device_stdin||check_config)throw std::invalid_argument("list-storage accepts no options");dm::usb::list_storage(report);return 0;}
    const bool receiving=command==L"serve-usb";if(!receiving&&command!=L"send-usb")throw std::invalid_argument("Select a USB command");
    if(!values.contains(L"--bind")||!values.contains(L"--port")||(receiving&&(values.contains(L"--peer")||device_stdin||access_stdin))||(!receiving&&(!values.contains(L"--peer")||!device_stdin||require_code)))throw std::invalid_argument("Invalid USB role options");
    dm::net::Options options;options.bind_address=ascii(values.at(L"--bind"));options.port=static_cast<std::uint16_t>(number(values.at(L"--port"),65535));options.timeout=std::chrono::milliseconds(250);if(options.bind_address!="auto")ipv4(options.bind_address);if(!receiving){options.peer=ascii(values.at(L"--peer"));ipv4(options.peer);options.timeout=std::chrono::seconds(5);}
    unsigned pid=0;if(values.contains(L"--parent-pid"))pid=number(values.at(L"--parent-pid"),0xffffffffu);if(values.contains(L"--pipe")&&!pid)throw std::invalid_argument("Local pipe requires parent identity");
    if(values.contains(L"--pipe"))validate_channel(values.at(L"--pipe"));
    if(check_config){report(receiving?"CONFIG_OK mode=usb-receiver":"CONFIG_OK mode=usb-source");return 0;}
    if(values.contains(L"--pipe"))open_channel(values.at(L"--pipe"),pid);else if(pid){parent=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!parent)throw std::runtime_error("Launcher unavailable");}
    const auto input=channel!=INVALID_HANDLE_VALUE?channel:GetStdHandle(STD_INPUT_HANDLE);std::string device;if(device_stdin)device=line(input,4096,std::chrono::seconds(30));if(access_stdin){auto code=line(input,6,std::chrono::seconds(30));dm::security::set_access_client(code);SecureZeroMemory(code.data(),code.size());}
    std::shared_ptr<dm::access::Gate> gate;if(require_code){gate=dm::security::enable_access_host();report("ACCESS_CODE "+gate->generate());}
    watcher=std::jthread([gate](std::stop_token token){auto previous=gate?gate->status():dm::access::Gate::Status::used;while(!token.stop_requested()&&!stopped.load()){if(parent&&WaitForSingleObject(parent,100)!=WAIT_TIMEOUT){stopped=true;return;}if(!parent)Sleep(100);if(gate){const auto state=gate->status();if(state!=previous){previous=state;if(state!=dm::access::Gate::Status::active)report(std::string("ACCESS_STATUS ")+(state==dm::access::Gate::Status::used?"used":state==dm::access::Gate::Status::expired?"expired":"exhausted"));}}}});
    controls=std::jthread([input,receiving,gate](std::stop_token token){while(!token.stop_requested()&&!stopped.load()){DWORD available=0;if(GetFileType(input)==FILE_TYPE_PIPE){if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)){stopped=true;return;}if(!available){Sleep(25);continue;}}
        try{const auto value=line(input,16,std::chrono::seconds(30));if(value=="G"&&gate)report("ACCESS_CODE "+gate->generate());else if(value=="Q"){if(receiving&&active.load()){quit_after_eject=true;eject_requested=true;}else stopped=true;}else if(value=="E"&&receiving&&active.load())eject_requested=true;else if(value!="E")throw std::invalid_argument("Unexpected USB control");}catch(...){stopped=true;return;}}});
    if(receiving){do{dm::usb::receive_storage(options,stopped,eject_requested,report);active=false;if(quit_after_eject.load()&&clean_ejected.load())stopped=true;}while(!stopped.load());}
    else dm::usb::send_storage(options,device,stopped,report);
    report("STOPPED");
}catch(const std::exception& e){report(std::string("error: ")+e.what());result=1;}
    stopped=true;controls.request_stop();watcher.request_stop();if(controls.joinable()){CancelSynchronousIo(controls.native_handle());controls.join();}if(watcher.joinable())watcher.join();if(channel!=INVALID_HANDLE_VALUE)CloseHandle(channel);if(parent)CloseHandle(parent);return result;
}
