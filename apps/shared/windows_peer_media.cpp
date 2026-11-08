#include "windows_peer_media.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include "windows_child.hpp"
#endif
namespace extrapane::windows_peer {
namespace {
using Kind=peers::Kind;
void wipe(std::string& value){volatile char* p=value.data();for(std::size_t i=0;i<value.size();++i)p[i]=0;value.clear();}
struct Erase {std::string& value;~Erase(){wipe(value);}};
[[noreturn]] void fail(const char* message){throw std::runtime_error(message);}
unsigned index(Kind kind){const auto i=static_cast<unsigned>(kind);if(i>4)fail("Existing screen needs its own scoped capture host");return i;}
std::string name(Kind kind){return std::array{"display","camera","microphone","usb","speaker"}[index(kind)];}
bool valid_ticket(const std::string& value,Kind kind){
    if(value.size()!=105 || value.substr(0,6)!="EPCT1:" || value[6]!=char('0'+index(kind)) || value[7]!=':' || value[40]!=':')return false;
    for(std::size_t i=8;i<value.size();++i)if(i!=40 && !((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')))return false;
    return true;
}
bool safe_device(const std::string& value){return !value.empty()&&value.size()<=4096&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32||c==127;});}
std::string executable(Kind kind){switch(kind){case Kind::display_destination:return "dm-view.exe";case Kind::camera:return "ExtraPane.Resources.exe";case Kind::microphone:return "ExtraPane.Microphone.exe";case Kind::speaker:return "ExtraPane.Speakers.exe";case Kind::usb:return "ExtraPane.Usb.exe";default:fail("Unsupported native resource");}}
void append_lines(std::string& pending,const std::string& data){if(data.size()>16384 || pending.size()+data.size()>16384)fail("Native status exceeded its limit");pending+=data;}
std::vector<std::string> lines(std::string& pending){std::vector<std::string> out;for(auto n=pending.find('\n');n!=pending.npos;n=pending.find('\n')){auto line=pending.substr(0,n);if(!line.empty()&&line.back()=='\r')line.pop_back();out.push_back(std::move(line));pending.erase(0,n+1);}return out;}
std::string wait_line(Process& process,std::string& pending,const std::function<bool(const std::string&)>& match,std::chrono::steady_clock::time_point deadline){
    for(;;){append_lines(pending,process.drain());while(pending.find('\n')!=pending.npos){const auto at=pending.find('\n');auto line=pending.substr(0,at);pending.erase(0,at+1);if(!line.empty()&&line.back()=='\r')line.pop_back();if(match(line))return line;if(line.starts_with("error:"))fail("Native component preparation failed");}
        if(!process.running())fail("Native component exited before readiness");if(std::chrono::steady_clock::now()>=deadline)fail("Native component preparation timed out");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
}
#ifdef _WIN32
std::wstring wide(const std::string& text){if(text.empty())return {};const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);if(!n)fail("Invalid UTF-8 native option");std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),r.data(),n);return r;}
class NativeProcess final:public Process {
    windows_bridge::Child child_;
public:
    void start(const Command& command)override{
        std::vector<std::wstring> args;for(const auto& arg:command.arguments)args.push_back(wide(arg));
        if(command.elevated)child_.start_elevated(wide(command.executable),args,command.executable!="ExtraPane.Service.exe");
        else{args.insert(args.end(),{L"--parent-pid",std::to_wstring(GetCurrentProcessId())});child_.start((windows_bridge::directory()/wide(command.executable)).wstring(),args,command.graceful);}
    }
    void write(const std::string& text)override{child_.send_control(text);}
    std::string drain()override{return child_.drain();}
    bool running()const override{return child_.process&&WaitForSingleObject(child_.process,0)==WAIT_TIMEOUT;}
    void stop()noexcept override{child_.stop();}
};
#endif
}
#ifdef _WIN32
Factory native_process_factory(){return []{return std::make_unique<NativeProcess>();};}
#endif
struct WindowsPeerMediaAdapter::Impl {
    Factory factory;
    std::map<std::string,Source> sources;
    struct Shared {std::unique_ptr<Process> process;std::string pending;Kind kind;};
    struct Row {peers::Grant grant;std::string resource,shared,pending;std::unique_ptr<Process> process;bool active=false;};
    std::map<std::string,Shared> shared;
    std::map<std::uint64_t,Row> rows;
    explicit Impl(Factory f):factory(std::move(f)){if(!factory)fail("Missing process factory");}
};
WindowsPeerMediaAdapter::WindowsPeerMediaAdapter(Factory f):impl_(std::make_unique<Impl>(std::move(f))){}
WindowsPeerMediaAdapter::~WindowsPeerMediaAdapter(){stop_all();}
void WindowsPeerMediaAdapter::sources(std::vector<Source> sources){
    std::map<std::string,Source> replacement;
    for(auto& source:sources){peers::Catalog check;check.device[0]=1;check.instance=1;check.revision=1;check.name="Native sources";check.offers={source.offer};peers::validate(check);
        if(source.offer.kind!=Kind::display_destination && source.offer.kind!=Kind::existing_screen && !safe_device(source.device))fail("Invalid native source identifier");
        if(!replacement.emplace(source.offer.id,std::move(source)).second)fail("Duplicate native offer identifier");}
    std::vector<std::uint64_t> removed;for(const auto& [id,row]:impl_->rows){const auto found=replacement.find(row.resource),old=impl_->sources.find(row.resource);if(found==replacement.end()||!found->second.offer.available||old==impl_->sources.end()||old->second.device!=found->second.device||!(old->second.offer==found->second.offer))removed.push_back(id);}
    for(auto id:removed)stop(id);impl_->sources=std::move(replacement);
}
bool WindowsPeerMediaAdapter::start(const peers::Grant& grant,const peers::rpc::Open& open,std::uint16_t port,std::string credential,std::string& reason){
    Erase erase{credential};
    try{
        if(!grant.id || impl_->rows.contains(grant.id) || !port || grant.owner!=open.owner || grant.resource!=open.resource || grant.input!=open.input)fail("Invalid native grant");
        const auto source=impl_->sources.find(grant.resource);if(source==impl_->sources.end() || !source->second.offer.available || source->second.offer.kind!=grant.kind || (grant.input&&!source->second.offer.input_allowed))fail("Native resource is not offered");
        if(grant.delegated)fail("Delegated resources must execute at their actual owner");
        if(!valid_ticket(credential,grant.kind))fail("Missing kind-bound native credential");
        auto& config=source->second;auto& row=impl_->rows.try_emplace(grant.id,Impl::Row{grant,grant.resource}).first->second;
        if(grant.kind==Kind::camera || grant.kind==Kind::microphone){
            row.shared=name(grant.kind)+":"+config.device;auto& shared=impl_->shared[row.shared];
            if(!shared.process){shared.kind=grant.kind;shared.process=impl_->factory();shared.process->start({executable(grant.kind),{grant.kind==Kind::camera?"broker-camera":"broker-microphone","--device-stdin"}});shared.process->write(config.device+"\n");if(grant.kind==Kind::camera)shared.process->write(config.mirror?"MIRROR\t1\n":"MIRROR\t0\n");}
            shared.process->write("ADD\t"+std::to_string(grant.id)+"\t127.0.0.1\t127.0.0.1\t"+std::to_string(port)+"\t"+credential+"\n");
        }else{
            Command command;command.executable=executable(grant.kind);
            if(grant.kind==Kind::display_destination){
                if(config.renderer!="d3d11"&&config.renderer!="d3d12"&&config.renderer!="vulkan"&&config.renderer!="opengl")fail("Invalid renderer");
                command.arguments={"receive","--lan","--renderer",config.renderer};command.graceful=false;if(grant.input)command.arguments.push_back("--control");
                if(!config.device.empty()){if(!config.device.starts_with("\\\\.\\DISPLAY")||config.device.size()>32||!safe_device(config.device))fail("Invalid display destination");command.arguments.insert(command.arguments.end(),{"--monitor",config.device});}
                auto w=open.width,h=open.height,hz=open.refresh;if(!w){w=config.offer.width;h=config.offer.height;hz=config.offer.refresh;}
                if(w||h||hz){if(w<2||w>4096||h<2||h>4096||w%2||h%2||!hz||hz>360)fail("Unsupported native display mode");command.arguments.insert(command.arguments.end(),{"--width",std::to_string(w),"--height",std::to_string(h),"--fps",std::to_string(hz)});}
            }else{command.arguments={grant.kind==Kind::usb?"send-usb":"receive-speakers","--device-stdin"};command.elevated=grant.kind==Kind::usb;}
            command.arguments.insert(command.arguments.end(),{"--bind","127.0.0.1","--peer","127.0.0.1","--port",std::to_string(port),"--access-stdin"});
            row.process=impl_->factory();row.process->start(command);if(grant.kind!=Kind::display_destination)row.process->write(config.device+"\n");row.process->write(credential+"\n");
        }
        return true;
    }catch(const std::exception& e){reason=e.what();stop(grant.id);return false;}
}
void WindowsPeerMediaAdapter::stop(std::uint64_t id)noexcept{
    auto row=impl_->rows.find(id);if(row==impl_->rows.end())return;
    if(row->second.process)row->second.process->stop();
    const auto sharedKey=row->second.shared;
    if(!sharedKey.empty()){const auto source=impl_->shared.find(sharedKey);if(source!=impl_->shared.end())try{source->second.process->write("REMOVE\t"+std::to_string(id)+"\n");}catch(...){}}
    impl_->rows.erase(row);
    if(!sharedKey.empty() && std::none_of(impl_->rows.begin(),impl_->rows.end(),[&](const auto& item){return item.second.shared==sharedKey;})){auto source=impl_->shared.find(sharedKey);if(source!=impl_->shared.end()){source->second.process->stop();impl_->shared.erase(source);}}
}
void WindowsPeerMediaAdapter::stop_all()noexcept{while(!impl_->rows.empty())stop(impl_->rows.begin()->first);for(auto& [key,source]:impl_->shared)if(source.process)source.process->stop();impl_->shared.clear();}
std::vector<Event> WindowsPeerMediaAdapter::poll(){
    std::vector<Event> result;std::vector<std::uint64_t> ended;
    for(auto& [key,source]:impl_->shared){
        bool failed=!source.process->running();try{append_lines(source.pending,source.process->drain());for(const auto& line:lines(source.pending)){
            if(line.starts_with("error:")||line.starts_with("BROKER_ERROR"))failed=true;
            const auto first=line.find('\t'),second=line.find('\t',first==line.npos?line.size():first+1);if(first==line.npos||second==line.npos)continue;
            if(line.starts_with("error:")||line.starts_with("BROKER_ERROR"))failed=true;
            const auto idText=line.substr(first+1,second-first-1);for(auto& [id,row]:impl_->rows)if(row.shared==key&&std::to_string(id)==idText){const auto event=line.substr(second+1,line.find('\t',second+1)-second-1);const bool active=line.starts_with("RESOURCE_EVENT\t")&&event=="STARTED";const bool stopped=line.starts_with("RESOURCE_EVENT\t")&&(event=="ERROR"||event=="STOPPED");if(active){row.active=true;result.push_back({id,true,false,"Connected"});}if(stopped){ended.push_back(id);result.push_back({id,false,true,"Native resource ended"});}}
        }}catch(...){failed=true;}
        if(failed)for(const auto& [id,row]:impl_->rows)if(row.shared==key){ended.push_back(id);result.push_back({id,false,true,"Native source ended"});}
    }
    for(auto& [id,row]:impl_->rows)if(row.process){bool failed=!row.process->running();try{append_lines(row.pending,row.process->drain());for(const auto& line:lines(row.pending)){
        if(line.starts_with("VIDEO_RECEIVING")||line.starts_with("VIDEO_STARTED")||line.starts_with("SPEAKER_STARTED")||line.starts_with("USB_STARTED")){row.active=true;result.push_back({id,true,false,"Connected"});}
        if(line.starts_with("error:")||line.starts_with("USB_RETURN_FAILED")){failed=true;}
    }}catch(...){failed=true;}if(failed){ended.push_back(id);result.push_back({id,false,true,"Native resource ended"});}}
    for(auto id:ended)stop(id);return result;
}
struct WindowsPeerReceiver::Impl {
    Factory factory;std::unique_ptr<Process> host;std::map<std::uint64_t,std::unique_ptr<Process>> brokers;
    std::uint16_t port=0;std::uint64_t nextLease=0;std::string hostPending,initialCode;
    explicit Impl(Factory f):factory(std::move(f)){if(!factory)fail("Missing process factory");}
};
WindowsPeerReceiver::WindowsPeerReceiver(Factory f):impl_(std::make_unique<Impl>(std::move(f))){}
WindowsPeerReceiver::~WindowsPeerReceiver(){stop();}
void WindowsPeerReceiver::start(std::uint16_t port,std::chrono::steady_clock::time_point deadline){
    if(running()){if(impl_->port!=port)fail("Native receiver is already configured differently");return;}stop();if(!port)fail("Local receiver port is required");
    try{
        impl_->port=port;impl_->host=impl_->factory();Command command{"ExtraPane.Service.exe",{"--port",std::to_string(port),"--bind","127.0.0.1","--require-code","--input"},true,false};impl_->host->start(command);
        bool listening=false;
        wait_line(*impl_->host,impl_->hostPending,[&](const auto& line){
            if(line.starts_with("ACCESS_CODE "))impl_->initialCode=line.substr(12);
            if(line.starts_with("LISTENING ")||line.starts_with("HOST_LISTENING"))listening=true;
            return listening&&!impl_->initialCode.empty();
        },deadline);
        if(impl_->initialCode.size()!=6||!std::all_of(impl_->initialCode.begin(),impl_->initialCode.end(),[](char c){return c>='0'&&c<='9';}))fail("Invalid local receiver credential");
    }catch(...){stop();throw;}
}
Admission WindowsPeerReceiver::issue(Kind kind,bool input,std::chrono::steady_clock::time_point deadline){
    if(!running())fail("Local receiving components are not running");
    const auto kindIndex=index(kind);if(impl_->brokers.size()>=256||impl_->nextLease==UINT64_MAX)fail("Native admission budget exhausted");
    auto code=std::move(impl_->initialCode);impl_->initialCode.clear();Erase eraseCode{code};
    if(code.empty()){
        impl_->host->write("G");
        auto line=wait_line(*impl_->host,impl_->hostPending,[](const auto& value){return value.starts_with("ACCESS_CODE ");},deadline);Erase eraseLine{line};code=line.substr(12);
    }
    if(code.size()!=6||!std::all_of(code.begin(),code.end(),[](char c){return c>='0'&&c<='9';}))fail("Invalid local receiver credential");
    auto broker=impl_->factory();
    try{
        broker->start({"ExtraPane.Session.exe",{"--session","--access-stdin","--bind","127.0.0.1","--peer","127.0.0.1","--port",std::to_string(impl_->port)}});broker->write(code+"\n");std::string pending;
        wait_line(*broker,pending,[](const auto& value){return value=="SESSION_READY";},deadline);
        broker->write("ADD "+std::to_string(kindIndex)+(kind==Kind::display_destination?(input?" 1":" 0"):"")+"\n");const auto prefix="SESSION_TICKET_V1\t"+name(kind)+"\t";
        auto line=wait_line(*broker,pending,[&](const auto& value){return value.starts_with(prefix);},deadline);Erase eraseLine{line};auto ticket=line.substr(prefix.size());
        if(!valid_ticket(ticket,kind)){wipe(ticket);fail("Invalid native admission ticket");}
        const auto lease=++impl_->nextLease;impl_->brokers.emplace(lease,std::move(broker));return {lease,std::move(ticket)};
    }catch(...){if(broker)broker->stop();throw;}
}
void WindowsPeerReceiver::release(std::uint64_t lease)noexcept{const auto found=impl_->brokers.find(lease);if(found==impl_->brokers.end())return;found->second->stop();impl_->brokers.erase(found);}
std::vector<std::uint64_t> WindowsPeerReceiver::poll(){
    std::vector<std::uint64_t> lost;bool hostLost=!running();
    if(impl_->host){auto status=impl_->host->drain();wipe(status);}
    for(auto& [id,broker]:impl_->brokers){auto status=broker->drain();const bool failed=hostLost||!broker->running()||status.find("error:")!=status.npos;wipe(status);if(failed)lost.push_back(id);}
    for(auto id:lost)release(id);return lost;
}
bool WindowsPeerReceiver::running()const{return impl_->host&&impl_->host->running();}
std::uint16_t WindowsPeerReceiver::port()const{return impl_->port;}
void WindowsPeerReceiver::stop()noexcept{for(auto& [id,broker]:impl_->brokers)broker->stop();impl_->brokers.clear();if(impl_->host)impl_->host->stop();impl_->host.reset();wipe(impl_->hostPending);wipe(impl_->initialCode);impl_->port=0;}
}

