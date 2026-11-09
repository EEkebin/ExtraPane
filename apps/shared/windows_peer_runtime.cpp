#include "windows_peer_runtime.hpp"
#include "windows_peer_screen.hpp"
#include <openssl/sha.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
namespace extrapane::windows_peer {
namespace {
using Service=peers::Service;using Kind=peers::Kind;using Clock=std::chrono::steady_clock;
[[noreturn]] void fail(const char* text){throw std::runtime_error(text);}
auto deadline(){return Clock::now()+std::chrono::seconds(30);}
std::string opaque(Kind kind,const std::string& device){std::string text=std::to_string(static_cast<unsigned>(kind))+":"+device;std::array<unsigned char,32> hash{};SHA256(reinterpret_cast<const unsigned char*>(text.data()),text.size(),hash.data());std::string result="resource-";for(unsigned i=0;i<16;++i){result+="0123456789abcdef"[hash[i]>>4];result+="0123456789abcdef"[hash[i]&15];}return result;}
std::string short_label(std::string value){if(value.size()>128){value.resize(128);while(!value.empty()&&(static_cast<unsigned char>(value.back())&0xc0)==0x80)value.pop_back();if(!value.empty()&&static_cast<unsigned char>(value.back())>=0xc0)value.pop_back();}return value;}
std::string unhex(const std::string& text){if(text.empty()||text.size()%2||text.size()>8192)fail("Invalid native inventory identity");std::string result;auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;};for(std::size_t i=0;i<text.size();i+=2){int a=digit(text[i]),b=digit(text[i+1]);if(a<0||b<0)fail("Invalid native inventory encoding");const auto c=static_cast<unsigned char>(a*16+b);if(c<32||c==127)fail("Unsafe native inventory identity");result+=static_cast<char>(c);}return result;}
std::vector<std::string> split(const std::string& text,char separator){std::vector<std::string> result;std::size_t start=0;for(;;){auto end=text.find(separator,start);result.push_back(text.substr(start,end==text.npos?end:end-start));if(end==text.npos)break;start=end+1;}return result;}
std::vector<Source> devices(Factory factory,Kind kind){
    const std::array<std::string,5> exe{"","ExtraPane.Resources.exe","ExtraPane.Microphone.exe","ExtraPane.Usb.exe","ExtraPane.Speakers.exe"};
    const std::array<std::string,5> command{"","list-cameras","list-microphones","list-storage","list-speakers"};
    const std::array<std::string,5> prefix{"","CAMERA","MICROPHONE","USB","SPEAKER"};const auto i=static_cast<unsigned>(kind);
    auto process=factory();if(!process)fail("Missing native inventory process");std::string output;
    try{process->start({exe[i],{command[i]},false,false,false});const auto until=Clock::now()+std::chrono::seconds(10);
        do{auto bytes=process->drain();if(bytes.size()>1048576-output.size())fail("Native inventory exceeded limit");output+=bytes;if(!process->running())break;if(Clock::now()>=until)fail("Native inventory timed out");std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(true);auto tail=process->drain();if(tail.size()>1048576-output.size())fail("Native inventory exceeded limit");output+=tail;process->stop();
    }catch(...){process->stop();throw;}
    bool complete=false;std::vector<Source> result;
    for(auto line:split(output,'\n')){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;if(line==prefix[i]+"_LIST_DONE"){if(complete)fail("Duplicate native inventory completion");complete=true;continue;}
        const auto fields=split(line,'\t');if(complete||fields.size()!=4||fields[0]!=prefix[i]+"_DEVICE_V1"||fields[2].empty()||fields[2].size()>2048||result.size()>=peers::max_resources)fail("Invalid native inventory response");
        auto device=unhex(kind==Kind::usb?fields[1]:fields[3]);const bool available=kind!=Kind::usb||fields[3]=="available";
        result.push_back({{opaque(kind,device),short_label(fields[2]),kind,available,false,available?std::string{}:fields[3]},std::move(device)});
    }
    if(!complete)fail("Native inventory did not finish");return result;
}
#ifdef _WIN32
std::string utf8(const wchar_t* text){const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,nullptr,0,nullptr,nullptr);if(n<1)fail("Invalid monitor label");std::string result(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,result.data(),n,nullptr,nullptr);result.pop_back();return result;}
std::uint16_t free_port(){struct Runtime{Runtime(){WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))fail("Socket startup failed");}~Runtime(){WSACleanup();}};static Runtime runtime;SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)fail("Cannot allocate local backend port");sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);int length=sizeof(addr);const auto ok=bind(s,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0&&getsockname(s,reinterpret_cast<sockaddr*>(&addr),&length)==0;closesocket(s);if(!ok)fail("Cannot allocate local backend port");return ntohs(addr.sin_port);}
std::vector<Source> monitors(){
    std::vector<Source> result{{{opaque(Kind::display_destination,"custom"),"Custom resolution",Kind::display_destination,true,true,"",1920,1080,60},{}}};
    for(DWORD i=0;i<128;++i){DISPLAY_DEVICEW device{};device.cb=sizeof(device);if(!EnumDisplayDevicesW(nullptr,i,&device,0))break;if(!(device.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))continue;DEVMODEW mode{};mode.dmSize=sizeof(mode);if(!EnumDisplaySettingsW(device.DeviceName,ENUM_CURRENT_SETTINGS,&mode))continue;const auto name=utf8(device.DeviceName);result.push_back({{opaque(Kind::display_destination,name),"Monitor "+std::to_string(result.size()),Kind::display_destination,true,true,"",mode.dmPelsWidth,mode.dmPelsHeight,mode.dmDisplayFrequency},name});}
    UINT32 pathCount=0,modeCount=0;auto status=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount);if(status!=ERROR_SUCCESS)return result;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);if(status!=ERROR_SUCCESS)return result;
    for(UINT32 i=0;i<pathCount;++i){const auto& path=paths[i];if(path.targetInfo.outputTechnology==DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED||path.targetInfo.outputTechnology==DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL)continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};target.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;target.header.size=sizeof(target);target.header.adapterId=path.targetInfo.adapterId;target.header.id=path.targetInfo.id;if(DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS||!target.monitorDevicePath[0])continue;
        const auto identity=utf8(target.monitorDevicePath);const auto label=target.monitorFriendlyDeviceName[0]?utf8(target.monitorFriendlyDeviceName):"Monitor "+std::to_string(i+1);
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};source.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;source.header.size=sizeof(source);source.header.adapterId=path.sourceInfo.adapterId;source.header.id=path.sourceInfo.id;
        if(DisplayConfigGetDeviceInfo(&source.header)==ERROR_SUCCESS){const auto destinationId=opaque(Kind::display_destination,utf8(source.viewGdiDeviceName));for(auto& item:result)if(item.offer.id==destinationId){item.offer.label=short_label(label);break;}}
        result.push_back({{opaque(Kind::existing_screen,identity),short_label(label),Kind::existing_screen,true,true,""},identity});
    }
    return result;
}
#endif
}
struct WindowsPeerRuntime::State {
    using Key=std::pair<Service::Token,Service::Token>;
    Factory factory;Inventory inventory;Port allocate;mutable std::mutex mutex;
    std::vector<Source> sources;WindowsPeerMediaAdapter provider;WindowsPeerReceiver receiver;
    struct Use {Service::Context context;std::uint64_t lease=0;std::unique_ptr<ScopedScreenProvider> screenHost;std::unique_ptr<ScopedScreenReceiver> screenClient;};
    std::map<Key,Use> uses;
    State(Factory f,Inventory i,Port p):factory(std::move(f)),inventory(std::move(i)),allocate(std::move(p)),provider(factory),receiver(factory){
#ifdef _WIN32
        if(!allocate)allocate=free_port;
        if(!inventory)inventory=[f=factory]{auto result=monitors();for(auto kind:{Kind::camera,Kind::microphone,Kind::usb,Kind::speaker}){try{auto extra=devices(f,kind);result.insert(result.end(),std::make_move_iterator(extra.begin()),std::make_move_iterator(extra.end()));}catch(...){/* A missing inventory helper does not enable a fictional resource. */}}return result;};
#endif
        if(!factory||!allocate||!inventory)fail("Native peer runtime dependencies are missing");
    }
    const Source& source(const std::string& id){auto found=std::find_if(sources.begin(),sources.end(),[&](const auto& s){return s.offer.id==id;});if(found==sources.end()||!found->offer.available)fail("Local source is no longer available");return *found;}
    static Key key(const Service::Context& c){return {c.connection,c.request};}
    peers::Grant grant(const Service::Context& c,const peers::rpc::Open& request){return {c.grant,c.peer,request.owner,request.resource,c.kind,request.input,false,false};}
    void stop(const Service::Context& c){const auto found=uses.find(key(c));if(found==uses.end())return;auto& use=found->second;if(c.grant&&use.context.grant&&c.grant!=use.context.grant)return;if(use.context.incoming&&use.context.kind!=Kind::existing_screen)provider.stop(use.context.grant);if(use.lease)receiver.release(use.lease);if(use.screenHost)use.screenHost->stop();if(use.screenClient)use.screenClient->stop();uses.erase(found);if(std::none_of(uses.begin(),uses.end(),[](const auto& item){return item.second.lease!=0;}))receiver.stop();}
};
WindowsPeerRuntime::WindowsPeerRuntime(Factory f,Inventory i,Port p):state_(std::make_shared<State>(std::move(f),std::move(i),std::move(p))){}
std::vector<peers::Offer> WindowsPeerRuntime::refresh(){auto inventory=state_->inventory();if(inventory.size()>peers::max_resources)fail("Native inventory exceeds resource catalog budget");std::lock_guard lock(state_->mutex);state_->provider.sources(inventory);state_->sources=std::move(inventory);std::vector<peers::Offer> result;for(const auto& s:state_->sources)result.push_back(s.offer);return result;}
std::vector<Source> WindowsPeerRuntime::sources()const{std::lock_guard lock(state_->mutex);return state_->sources;}
Service::Hooks WindowsPeerRuntime::hooks()const{
    auto state=state_;Service::Hooks hooks;
    hooks.prepare_request=[state](const Service::Context& c,peers::rpc::Open& request){std::lock_guard lock(state->mutex);if(state->uses.contains(State::key(c)))fail("Duplicate native request");State::Use use;use.context=c;state->uses.emplace(State::key(c),std::move(use));
        try{if(c.kind!=Kind::existing_screen){if(!state->receiver.running())state->receiver.start(state->allocate(),deadline());const auto admission=state->receiver.issue(c.kind,request.input,deadline());state->uses.at(State::key(c)).lease=admission.lease;request.native_credential=admission.credential;}}
        catch(...){state->stop(c);throw;}
    };
    hooks.prepare=[state](const Service::Context& c,peers::rpc::Open& request){std::lock_guard lock(state->mutex);
        if(c.incoming){if(state->uses.contains(State::key(c)))fail("Duplicate native grant");State::Use use;use.context=c;
            const auto& source=state->source(request.resource);if(source.offer.kind!=c.kind)fail("Native resource kind mismatch");
            if(c.kind==Kind::existing_screen){use.screenHost=std::make_unique<ScopedScreenProvider>(state->factory);request.native_credential=use.screenHost->start(source,state->grant(c,request),state->allocate(),deadline());const auto port=use.screenHost->port();state->uses.emplace(State::key(c),std::move(use));return Service::Endpoint{Service::Role::connect_local,port};}
            state->uses.emplace(State::key(c),std::move(use));return Service::Endpoint{Service::Role::listen_local,0};
        }
        auto found=state->uses.find(State::key(c));if(found==state->uses.end())fail("Native request admission is missing");found->second.context=c;
        if(c.kind==Kind::existing_screen)return Service::Endpoint{Service::Role::listen_local,0};
        if(!found->second.lease||!state->receiver.running())fail("Native receiving components stopped");return Service::Endpoint{Service::Role::connect_local,state->receiver.port()};
    };
    hooks.ready=[state](const Service::Context& c,const peers::rpc::Open& request,std::uint16_t port){std::lock_guard lock(state->mutex);auto found=state->uses.find(State::key(c));if(found==state->uses.end())fail("Native grant was not prepared");
        if(c.incoming&&c.kind!=Kind::existing_screen){std::string reason;if(!state->provider.start(state->grant(c,request),request,port,request.native_credential,reason))throw std::runtime_error(reason);}
        if(!c.incoming&&c.kind==Kind::existing_screen){found->second.screenClient=std::make_unique<ScopedScreenReceiver>(state->factory);found->second.screenClient->start(state->grant(c,request),port,request.native_credential);}
    };
    hooks.stopped=[state](const Service::Context& c){std::lock_guard lock(state->mutex);state->stop(c);};hooks.cancel_request=hooks.stopped;
    hooks.poll=[state]{std::lock_guard lock(state->mutex);std::vector<Service::Context> ended;const auto events=state->provider.poll();const auto lost=state->receiver.poll();
        for(const auto& [key,use]:state->uses){bool failed=std::find(lost.begin(),lost.end(),use.lease)!=lost.end();if(use.screenHost&&!use.screenHost->running())failed=true;if(use.screenClient&&!use.screenClient->running())failed=true;if(use.context.incoming)for(const auto& event:events)if(event.ended&&event.grant==use.context.grant)failed=true;if(failed){auto context=use.context;if(use.screenHost)context.failure_detail=use.screenHost->failure();if(use.screenClient)context.failure_detail=use.screenClient->failure();ended.push_back(std::move(context));}}return ended;
    };return hooks;
}
}
