#include <windows.h>
#include <dm/resource_session.hpp>
#include <dm/tls.hpp>
#include <charconv>
#include <iostream>
#include <map>
#include <syncstream>
#include <thread>
#include <string_view>

namespace {
std::atomic_bool stopped=false;
BOOL WINAPI control(DWORD event){if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT||event==CTRL_CLOSE_EVENT){stopped=true;return TRUE;}return FALSE;}
void report(const std::string& line){std::osyncstream(std::cout)<<line<<std::endl;}
unsigned number(const std::string& value,unsigned limit,bool zero=false){unsigned result=0;auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result);if(error!=std::errc{}||end!=value.data()+value.size()||result>limit||(!zero&&!result))throw std::invalid_argument("Invalid numeric option");return result;}
void ipv4(const std::string& value){
    std::size_t offset=0;unsigned first=0;
    for(unsigned group=0;group<4;++group){const auto end=value.find('.',offset);if((group<3)==(end==std::string::npos))throw std::invalid_argument("Expected an IPv4 literal");
        const auto piece=value.substr(offset,(end==std::string::npos?value.size():end)-offset);const auto octet=number(piece,255,true);if(group==0)first=octet;offset=end+1;}
    if(!first||first>=224)throw std::invalid_argument("Expected a unicast IPv4 literal");
}
std::wstring wide(const std::string& value){
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!size)throw std::invalid_argument("Camera identifier is not valid UTF-8");std::wstring result(size,L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size))throw std::invalid_argument("Camera identifier conversion failed");return result;
}
std::string utf8(const std::wstring& value){const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(!size)return {};std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);for(auto& c:result)if(c=='\t'||c=='\r'||c=='\n')c=' ';return result;}
struct Threads{std::thread parent,status;~Threads(){stopped=true;if(parent.joinable())parent.join();if(status.joinable())status.join();}};
}
int main(int argc,char** argv){try{
    SetConsoleCtrlHandler(control,TRUE);
    if(argc<2||std::string_view(argv[1])=="--help"){
        std::cout<<"ExtraPane camera resource helper (development)\n"
            "  ExtraPane.Resources.exe serve-camera --bind <local-ipv4|auto> --port N [--require-code] [--parent-pid N]\n"
            "  ExtraPane.Resources.exe send-camera --bind <local-ipv4|auto> --peer <server-ipv4> --port N --device-stdin [--access-stdin] [--width 1280 --height 720 --fps 30]\n"
            "  ExtraPane.Resources.exe list-cameras\n"
            "Host stdin: G generates a new one-time code; Q stops. Client stdin: device ID then optional code, followed by Q.\n"
            "Camera-only listener; do not run the legacy display listener on the same interface/port.\n";return 0;
    }
    const std::string command=argv[1];
    if(command=="list-cameras"){
        if(argc!=2)throw std::invalid_argument("list-cameras takes no options");
        const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized))throw std::runtime_error("Camera enumeration COM failed");
        struct Cleanup{~Cleanup(){CoUninitialize();}} cleanup;
        unsigned index=0;for(const auto& device:dm::camera::enumerate())report("CAMERA_DEVICE\t"+std::to_string(index++)+"\t"+utf8(device.name)+"\t"+utf8(device.id));
        report("CAMERA_LIST_DONE");return 0;
    }
    const bool server=command=="serve-camera";
    if(!server&&command!="send-camera")throw std::invalid_argument("Unknown resource helper command");
    bool requireCode=false,accessStdin=false,deviceStdin=false,checkConfig=false;std::map<std::string,std::string> values;
    for(int i=2;i<argc;++i){const std::string key=argv[i];
        if(key=="--require-code"){if(requireCode)throw std::invalid_argument("Duplicate code option");requireCode=true;continue;}
        if(key=="--access-stdin"){if(accessStdin)throw std::invalid_argument("Duplicate code input option");accessStdin=true;continue;}
        if(key=="--device-stdin"){if(deviceStdin)throw std::invalid_argument("Duplicate device input option");deviceStdin=true;continue;}
        if(key=="--check-config"){if(checkConfig)throw std::invalid_argument("Duplicate configuration check");checkConfig=true;continue;}
        if(key!="--bind"&&key!="--peer"&&key!="--port"&&key!="--width"&&key!="--height"&&key!="--fps"&&key!="--device-index"&&key!="--parent-pid")throw std::invalid_argument("Unknown resource helper option");
        if(++i>=argc||!values.emplace(key,argv[i]).second)throw std::invalid_argument("Missing or duplicate resource helper option");
    }
    if(!values.contains("--bind")||!values.contains("--port"))throw std::invalid_argument("Select a local interface and port");
    if(server&&(accessStdin||deviceStdin||values.contains("--device-index")||values.contains("--peer")||values.contains("--width")||values.contains("--height")||values.contains("--fps")))throw std::invalid_argument("Client options supplied to server");
    if(!server&&(requireCode||!values.contains("--peer")||(deviceStdin==values.contains("--device-index"))))throw std::invalid_argument("Client needs server address and exactly one local camera selector");
    dm::net::Options options;options.bind_address=values.at("--bind");options.port=static_cast<std::uint16_t>(number(values.at("--port"),65535));options.timeout=std::chrono::seconds(5);
    if(!server)options.peer=values.at("--peer");
    if(options.bind_address!="auto")ipv4(options.bind_address);if(!server)ipv4(options.peer);
    dm::camera::Format format{values.contains("--width")?number(values.at("--width"),3840):1280,values.contains("--height")?number(values.at("--height"),2160):720,values.contains("--fps")?number(values.at("--fps"),60):30};dm::camera::validate_format(format);
    if(values.contains("--device-index"))number(values.at("--device-index"),65535,true);
    if(values.contains("--parent-pid"))number(values.at("--parent-pid"),0xffffffffu);
    if(checkConfig){report(std::string("CONFIG_OK mode=")+(server?"camera-server":"camera-client"));return 0;}
    Threads threads;
    if(values.contains("--parent-pid")){
        const auto parent=OpenProcess(SYNCHRONIZE,FALSE,number(values.at("--parent-pid"),0xffffffffu));if(!parent)throw std::runtime_error("Launcher unavailable");
        threads.parent=std::thread([parent]{while(!stopped)if(WaitForSingleObject(parent,250)==WAIT_OBJECT_0){stopped=true;break;}CloseHandle(parent);});
    }
    std::shared_ptr<dm::access::Gate> gate;
    if(requireCode){gate=dm::security::enable_access_host();report("ACCESS_CODE "+gate->generate());
        threads.status=std::thread([gate]{auto previous=gate->status();while(!stopped){Sleep(200);const auto current=gate->status();if(current!=previous){previous=current;if(current!=dm::access::Gate::Status::active)report(std::string("ACCESS_STATUS ")+(current==dm::access::Gate::Status::used?"used":current==dm::access::Gate::Status::expired?"expired":"exhausted"));}}});
    }
    std::wstring device;
    if(!server){
        if(deviceStdin){std::string line;if(!std::getline(std::cin,line)||line.empty()||line.size()>4096)throw std::invalid_argument("Missing local camera identifier");device=wide(line);}
        else {const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized))throw std::runtime_error("Camera enumeration COM failed");struct Cleanup{~Cleanup(){CoUninitialize();}} cleanup;const auto devices=dm::camera::enumerate();const auto index=number(values.at("--device-index"),65535,true);if(index>=devices.size())throw std::invalid_argument("Camera selection is no longer available");device=devices[index].id;}
        if(accessStdin){std::string code;if(!std::getline(std::cin,code))throw std::invalid_argument("Missing access code");dm::security::set_access_client(code);SecureZeroMemory(code.data(),code.size());}
    }
    // stdin is launcher-owned; EOF revokes every resource. This detached reader
    // only captures process-lifetime stop state and a shared admission gate.
    std::thread([gate]{std::string line;while(std::getline(std::cin,line)){if(line=="Q"){stopped=true;return;}if(line=="G"&&gate){try{report("ACCESS_CODE "+gate->generate());}catch(...){report("error: Access code generation failed");}}}stopped=true;}).detach();
    if(server)dm::resources::serve_camera(options,stopped,report);
    else dm::resources::send_camera(options,device,format,stopped,report);
    report("STOPPED");return 0;
}catch(const std::exception& error){report(std::string("error: ")+error.what());return 1;}}
