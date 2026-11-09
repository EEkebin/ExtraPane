#include <windows.h>
#include <dm/microphone.hpp>
#include <dm/microphone_session.hpp>
#include <dm/tls.hpp>
#include <charconv>
#include <iostream>
#include <map>
#include <syncstream>
#include <thread>
#include <vector>
#include <algorithm>
namespace {
std::atomic_bool stopping=false,muted=false;
void report(const std::string& message){std::osyncstream(std::cout)<<message<<std::endl;}
BOOL WINAPI handler(DWORD event){if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT||event==CTRL_CLOSE_EVENT){stopping=true;return TRUE;}return FALSE;}
unsigned number(const std::string& value,unsigned limit,bool zero=false){unsigned n=0;const auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),n);if(ec!=std::errc{}||end!=value.data()+value.size()||n>limit||(!zero&&!n))throw std::invalid_argument("Invalid numeric option");return n;}
void ipv4(const std::string& value){if(_stricmp(value.c_str(),"localhost")==0)return;std::size_t start=0;unsigned first=0;for(unsigned i=0;i<4;++i){auto end=value.find('.',start);if((i<3)==(end==std::string::npos))throw std::invalid_argument("Expected IPv4 literal");auto n=number(value.substr(start,(end==std::string::npos?value.size():end)-start),255,true);if(!i)first=n;start=end+1;}if(!first||first>=224)throw std::invalid_argument("Expected unicast IPv4 literal");}
std::wstring wide(const std::string& value){const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(!n)throw std::invalid_argument("Invalid UTF-8 microphone identifier");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n);return out;}
std::string utf8(const std::wstring& value){const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(!n)throw std::runtime_error("Cannot encode microphone label");std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);return out;}
struct Threads{std::thread parent,status;~Threads(){stopping=true;if(parent.joinable())parent.join();if(status.joinable())status.join();}};
void broker_mode(int argc,char** argv){
    bool deviceStdin=false;unsigned parentPid=0;
    for(int i=2;i<argc;++i){const std::string arg=argv[i];if(arg=="--device-stdin"&&!deviceStdin){deviceStdin=true;continue;}
        if(arg=="--parent-pid"&&!parentPid&&++i<argc){parentPid=number(argv[i],0xffffffffu);continue;}throw std::invalid_argument("Invalid microphone broker option");}
    if(!deviceStdin)throw std::invalid_argument("Microphone broker requires --device-stdin");
    Threads threads;if(parentPid){HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,parentPid);if(!parent)throw std::runtime_error("Launcher unavailable");threads.parent=std::thread([parent]{while(!stopping){if(WaitForSingleObject(parent,100)==WAIT_OBJECT_0){stopping=true;break;}}CloseHandle(parent);});}
    std::string device;if(!std::getline(std::cin,device)||device.empty()||device.size()>4096||device.find('\0')!=std::string::npos)throw std::invalid_argument("Missing local microphone identifier");
    dm::microphone::SourceBroker broker(wide(device),stopping,report);report("BROKER_READY");
    std::string line;while(!stopping&&std::getline(std::cin,line)){
        struct Wipe{std::string& value;~Wipe(){if(!value.empty())SecureZeroMemory(value.data(),value.size());}}wipe{line};
        if(line=="Q"){stopping=true;break;}
        if(line.empty()||line.size()>8192||line.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid microphone broker command size");
        std::vector<std::string> fields;std::size_t start=0;
        do{const auto next=line.find('\t',start);fields.push_back(line.substr(start,next==std::string::npos?next:next-start));if(next==std::string::npos)break;start=next+1;}while(fields.size()<8);
        struct WipeFields{std::vector<std::string>& values;~WipeFields(){for(auto& value:values)if(!value.empty())SecureZeroMemory(value.data(),value.size());}}wipeFields{fields};
        std::string row=fields.size()>1?fields[1]:std::string{};
        const bool validRow=!row.empty()&&row.size()<=128&&std::all_of(row.begin(),row.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';});
        if(!validRow)throw std::invalid_argument("Invalid microphone broker row identity");
        try{
            if(fields[0]=="ADD"&&fields.size()==6){
                dm::net::Options options;options.bind_address=fields[2];options.peer=fields[3];options.port=static_cast<std::uint16_t>(number(fields[4],65535));
                if(options.bind_address!="auto")ipv4(options.bind_address);ipv4(options.peer);
                broker.add(row,options,fields[5]=="-"?std::string{}:fields[5]);
            }else if(fields[0]=="REMOVE"&&fields.size()==2)broker.remove(row);
            else if(fields[0]=="MUTE"&&fields.size()==3&&(fields[2]=="0"||fields[2]=="1"))broker.mute(row,fields[2]=="1");
            else throw std::invalid_argument("Unknown microphone broker command");
        }catch(const std::exception& e){std::string reason=e.what();for(auto& c:reason)if(static_cast<unsigned char>(c)<32)c=' ';report("RESOURCE_EVENT\t"+row+"\tERROR\t"+reason.substr(0,512));}
    }
    stopping=true;
}
}
int main(int argc,char** argv){try{
    SetConsoleCtrlHandler(handler,TRUE);
    if(argc<2||std::string_view(argv[1])=="--help"){
        std::cout<<"ExtraPane microphone helper (PCM development slice)\n"
            "  serve-microphone --bind <local-ipv4|auto> --port N [--require-code] [--parent-pid N] [--check-config]\n"
            "  send-microphone --bind <local-ipv4|auto> --peer <server-ipv4> --port N --device-stdin [--access-stdin] [--parent-pid N] [--check-config]\n"
            "  list-microphones\nServer stdin: G new code, Q stop. Sender stdin: device ID, optional code, then M mute, U unmute, Q stop.\n"
            "  broker-microphone --device-stdin [--parent-pid N] (one source, dynamic ADD/REMOVE/MUTE recipients on stdin)\n"
            "Dedicated microphone-only port; source currently requires a 48 kHz mix format.\n";return 0;
    }
    const std::string mode=argv[1];
    if(mode=="broker-microphone"){broker_mode(argc,argv);report("STOPPED");return 0;}
    if(mode=="list"||mode=="list-microphones"){
        if(argc!=2)throw std::invalid_argument("list takes no options");unsigned index=0;
        for(const auto& device:dm::microphone::enumerate()){
            if(index>=128)throw std::runtime_error("Too many microphones");auto name=utf8(device.name),id=utf8(device.id);
            if(name.size()>2048||id.size()>4096||id.find_first_of("\r\n")!=std::string::npos||id.find('\0')!=std::string::npos)throw std::runtime_error("Unsupported microphone label");
            for(auto& ch:name)if(static_cast<unsigned char>(ch)<32)ch=' ';
            std::string hex;for(unsigned char ch:id){hex+="0123456789abcdef"[ch>>4];hex+="0123456789abcdef"[ch&15];}
            report("MICROPHONE_DEVICE_V1\t"+std::to_string(index++)+"\t"+name+"\t"+hex);
        }report("MICROPHONE_LIST_DONE");return 0;
    }
    const bool server=mode=="serve"||mode=="serve-microphone";if(!server&&mode!="send"&&mode!="send-microphone")throw std::invalid_argument("Unknown microphone command");
    bool requireCode=false,access=false,device=false,check=false;std::map<std::string,std::string> values;
    for(int i=2;i<argc;++i){std::string key=argv[i];
        auto flag=[&](bool& flag){if(flag)throw std::invalid_argument("Duplicate flag");flag=true;};
        if(key=="--require-code"){flag(requireCode);continue;}if(key=="--access-stdin"){flag(access);continue;}if(key=="--device-stdin"){flag(device);continue;}if(key=="--check-config"){flag(check);continue;}
        if(key!="--bind"&&key!="--peer"&&key!="--port"&&key!="--parent-pid")throw std::invalid_argument("Unknown microphone option");
        if(++i>=argc||!values.emplace(key,argv[i]).second)throw std::invalid_argument("Missing or duplicate microphone option");
    }
    if(!values.contains("--bind")||!values.contains("--port"))throw std::invalid_argument("Select local interface and port");
    if(server&&(access||device||values.contains("--peer")))throw std::invalid_argument("Client options supplied to server");
    if(!server&&(requireCode||!device||!values.contains("--peer")))throw std::invalid_argument("Sender requires server address and local device stdin");
    dm::net::Options options;options.bind_address=values.at("--bind");options.port=static_cast<std::uint16_t>(number(values.at("--port"),65535));
    if(options.bind_address!="auto")ipv4(options.bind_address);if(!server){options.peer=values.at("--peer");ipv4(options.peer);}
    if(values.contains("--parent-pid"))number(values.at("--parent-pid"),0xffffffffu);
    if(check){report(std::string("CONFIG_OK mode=microphone-")+(server?"server":"sender"));return 0;}
    Threads threads;
    if(values.contains("--parent-pid")){
        HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,number(values.at("--parent-pid"),0xffffffffu));if(!parent)throw std::runtime_error("Launcher unavailable");
        threads.parent=std::thread([parent]{while(!stopping){if(WaitForSingleObject(parent,100)==WAIT_OBJECT_0){stopping=true;break;}}CloseHandle(parent);});
    }
    std::shared_ptr<dm::access::Gate> gate;
    if(requireCode){gate=dm::security::enable_access_host();report("ACCESS_CODE "+gate->generate());
        threads.status=std::thread([gate]{auto prior=gate->status();while(!stopping){Sleep(200);auto status=gate->status();if(prior!=status){prior=status;if(status!=dm::access::Gate::Status::active)report(std::string("ACCESS_STATUS ")+(status==dm::access::Gate::Status::used?"used":status==dm::access::Gate::Status::expired?"expired":"exhausted"));}}});
    }
    std::wstring selected;
    if(!server){std::string line;if(!std::getline(std::cin,line)||line.empty()||line.size()>4096||line.find('\0')!=std::string::npos)throw std::invalid_argument("Missing local microphone identifier");selected=wide(line);
        if(access){std::string code;if(!std::getline(std::cin,code))throw std::invalid_argument("Missing access code");dm::security::set_access_client(code);SecureZeroMemory(code.data(),code.size());}}
    // Only process-lifetime atomics/shared gate are captured. EOF revokes capture.
    std::thread([gate]{std::string line;while(std::getline(std::cin,line)){if(line=="Q"){stopping=true;return;}if(line=="M")muted=true;if(line=="U")muted=false;if(line=="G"&&gate){try{report("ACCESS_CODE "+gate->generate());}catch(...){report("error: Cannot generate access code");}}}stopping=true;}).detach();
    if(server)dm::microphone::serve(options,stopping,report);else dm::microphone::send(options,selected,stopping,muted,report);
    report("STOPPED");return 0;
}catch(const std::exception& error){report(std::string("error: ")+error.what());return 1;}}
