#include <windows.h>
#include <dm/speaker.hpp>
#include <dm/speaker_session.hpp>
#include <dm/tls.hpp>
#include <charconv>
#include <iostream>
#include <map>
#include <syncstream>
#include <thread>
namespace {
std::atomic_bool stopping=false,muted=false;std::atomic_uint volume=100;std::atomic_uint64_t muteEpoch=0;
void report(const std::string& text){std::osyncstream(std::cout)<<text<<std::endl;}
BOOL WINAPI handler(DWORD event){if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT||event==CTRL_CLOSE_EVENT){stopping=true;return TRUE;}return FALSE;}
unsigned number(std::string_view text,unsigned limit,bool zero=false){unsigned value=0;const auto [end,ec]=std::from_chars(text.data(),text.data()+text.size(),value);if(ec!=std::errc{}||end!=text.data()+text.size()||value>limit||(!zero&&!value))throw std::invalid_argument("Invalid numeric option");return value;}
void ipv4(const std::string& value){std::size_t start=0;unsigned first=0;for(unsigned i=0;i<4;++i){const auto end=value.find('.',start);if((i<3)==(end==std::string::npos))throw std::invalid_argument("Expected IPv4 literal");const auto n=number(value.substr(start,end==std::string::npos?end:end-start),255,true);if(!i)first=n;start=end+1;}if(!first||first>=224)throw std::invalid_argument("Expected unicast IPv4 literal");}
std::wstring wide(const std::string& value){const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(!n)throw std::invalid_argument("Invalid output identifier");std::wstring result(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),n);return result;}
std::string utf8(const std::wstring& value){const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(!n)throw std::runtime_error("Cannot encode speaker name");std::string result(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr);return result;}
bool readLine(std::string& line,std::size_t limit){line.clear();char c=0;while(std::cin.get(c)){if(c=='\n'){if(!line.empty()&&line.back()=='\r')line.pop_back();return true;}if(line.size()>=limit)throw std::invalid_argument("Speaker command exceeds size limit");line.push_back(c);}return false;}
struct Parent{std::thread worker;~Parent(){stopping=true;if(worker.joinable())worker.join();}};
}
int main(int argc,char** argv){try{
    SetConsoleCtrlHandler(handler,TRUE);
    if(argc<2||std::string_view(argv[1])=="--help"){std::cout<<"ExtraPane speakers helper\nlist-speakers\nreceive-speakers --bind <local-ipv4|auto> --peer <server-ipv4> --port N --device-stdin [--access-stdin] [--parent-pid N] [--check-config]\nStdin: output device ID, optional access credential, then M mute, U unmute, V 0..100 volume, Q stop.\n";return 0;}
    const std::string mode=argv[1];
    if(mode=="list-speakers"){
        if(argc!=2)throw std::invalid_argument("list-speakers takes no options");unsigned index=0;
        for(const auto& device:dm::speaker::enumerate()){
            if(index>=128)throw std::runtime_error("Too many playback devices");auto label=utf8(device.name),id=utf8(device.id);
            if(label.size()>2048||id.size()>4096||id.find_first_of("\r\n")!=std::string::npos||id.find('\0')!=std::string::npos)throw std::runtime_error("Unsupported output device identifier");
            for(auto& c:label)if(static_cast<unsigned char>(c)<32)c=' ';std::string hex;for(unsigned char c:id){hex+="0123456789abcdef"[c>>4];hex+="0123456789abcdef"[c&15];}
            report("SPEAKER_DEVICE_V1\t"+std::to_string(index++)+"\t"+label+"\t"+hex);
        }report("SPEAKER_LIST_DONE");return 0;
    }
    if(mode!="receive-speakers")throw std::invalid_argument("Unknown speaker operation");
    bool device=false,access=false,check=false;std::map<std::string,std::string> values;
    for(int i=2;i<argc;++i){const std::string key=argv[i];auto flag=[&](bool& value){if(value)throw std::invalid_argument("Duplicate flag");value=true;};if(key=="--device-stdin"){flag(device);continue;}if(key=="--access-stdin"){flag(access);continue;}if(key=="--check-config"){flag(check);continue;}
        if(key!="--bind"&&key!="--peer"&&key!="--port"&&key!="--parent-pid")throw std::invalid_argument("Unknown speaker option");if(++i>=argc||!values.emplace(key,argv[i]).second)throw std::invalid_argument("Missing or duplicate speaker option");
    }
    if(!device||!values.contains("--bind")||!values.contains("--peer")||!values.contains("--port"))throw std::invalid_argument("Select server address, port, local interface and output device");
    dm::net::Options options;options.bind_address=values.at("--bind");options.peer=values.at("--peer");options.port=static_cast<std::uint16_t>(number(values.at("--port"),65535));if(options.bind_address!="auto")ipv4(options.bind_address);ipv4(options.peer);if(values.contains("--parent-pid"))number(values.at("--parent-pid"),0xffffffffu);
    if(check){report("CONFIG_OK mode=speaker-receiver");return 0;}
    Parent parent;if(values.contains("--parent-pid")){HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,number(values.at("--parent-pid"),0xffffffffu));if(!process)throw std::runtime_error("Launcher unavailable");parent.worker=std::thread([process]{while(!stopping){if(WaitForSingleObject(process,100)==WAIT_OBJECT_0){stopping=true;break;}}CloseHandle(process);});}
    std::string selected;if(!readLine(selected,4096)||selected.empty()||selected.size()>4096||selected.find('\0')!=std::string::npos)throw std::invalid_argument("Select an output device");const auto output=wide(selected);
    if(access){std::string code;if(!readLine(code,256))throw std::invalid_argument("Missing access credential");dm::security::set_access_client(code);SecureZeroMemory(code.data(),code.size());}
    // This process-lifetime reader owns no native devices or stack references.
    std::thread([]{try{std::string line;while(readLine(line,64)){if(line=="Q"){stopping=true;return;}if(line=="M"){if(!muted.exchange(true))++muteEpoch;}else if(line=="U"){if(muted.exchange(false))++muteEpoch;}else if(line.starts_with("V ")){try{volume=number(std::string_view(line).substr(2),100,true);}catch(...){report("error: Speaker volume must be 0 through 100");}}}stopping=true;}catch(const std::exception& e){report(std::string("error: ")+e.what());stopping=true;}}).detach();
    try{dm::speaker::receive(options,output,stopping,muted,volume,muteEpoch,report);}catch(...){if(!stopping)throw;}report("SPEAKER_STOPPED");return 0;
}catch(const std::exception& e){report(std::string("error: ")+e.what());return 1;}}
