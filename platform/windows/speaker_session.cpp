#include <windows.h>
#include <bcrypt.h>
#include <dm/speaker_session.hpp>
#include <dm/speaker.hpp>
#include <dm/tls.hpp>
#include <extrapane/speaker_protocol.hpp>
#include <extrapane/negotiation.hpp>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <stdexcept>
namespace dm::speaker {
namespace {
using namespace extrapane;using Clock=std::chrono::steady_clock;
void need(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Handle {HANDLE h=nullptr;Handle()=default;explicit Handle(HANDLE v):h(v){}~Handle(){reset();}Handle(const Handle&)=delete;void reset(HANDLE v=nullptr){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=v;}bool valid()const{return h&&h!=INVALID_HANDLE_VALUE;}};
struct Cancellation {std::jthread worker;Cancellation(security::TlsStream& tls,std::atomic_bool& stop):worker([&](std::stop_token token){while(!token.stop_requested()){if(stop){tls.cancel();return;}Sleep(10);}}){}};
struct Transport {
    security::TlsStream& tls;std::atomic_bool& stop;wire::ControlChannel control;
    Transport(security::TlsStream& t,std::atomic_bool& s):tls(t),stop(s),control([this](auto b){read(b);},[this](auto b){write(b);}){}
    void read(std::span<std::uint8_t> b,unsigned seconds=5){if(stop)throw std::runtime_error("Speakers stopped");tls.read(b,Clock::now()+std::chrono::seconds(seconds));}
    void write(std::span<const std::uint8_t> b){if(stop)throw std::runtime_error("Speakers stopped");tls.write(b,Clock::now()+std::chrono::seconds(5));}
};
media::Binding random_binding(){media::Binding b{};do{need(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&b),sizeof(b),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Speaker randomness unavailable");}while(!b.resource||!b.generation);return b;}
wire::Message caps_message(const Capabilities& caps){auto bytes=wire::encode_capabilities(caps,wire::CapabilityFormat::with_speakers);return {{wire::Operation::capabilities,ResourceKind::display,0,static_cast<std::uint32_t>(bytes.size())},std::move(bytes)};}
class Endpoint final:public Resource {
public:
    Endpoint(std::atomic_bool& stop,unsigned slot,const Report& report):stopping_(stop),slot_(slot),report_(report){need(slot!=0,"Invalid virtual speaker slot");}
    ~Endpoint(){stop();}
    Availability prepare()override{
        std::wstring path(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));if(!n||n>=path.size())return {false,"Cannot locate speaker component"};path.resize(n);
        exe_=(std::filesystem::path(path).parent_path()/L"ExtraPane.MicrophoneEndpoint.exe").wstring();if(!std::filesystem::is_regular_file(exe_))return {false,"Managed audio endpoint component is missing"};return {true,{}};
    }
    Availability start()override{try{launch();return {true,{}};}catch(const std::exception& e){stop();return {false,e.what()};}}
    speaker_wire::Frame next(media::Binding binding){
        std::array<std::uint8_t,24> header{};read(header);need(std::equal(header.begin(),header.begin()+4,"EPSP")&&header[4]==1&&(header[5]&~1u)==0&&!header[6]&&!header[7],"Invalid local speaker packet header");
        auto integer=[&](unsigned offset){std::uint64_t value=0;for(unsigned i=0;i<8;++i)value=(value<<8)|header[offset+i];return value;};
        const auto sequence=integer(8),position=integer(16);need(sequence==sequence_&&(position==position_||((header[5]&1)&&position>=position_)),"Local speaker packet sequence or position changed");
        need(sequence_!=UINT64_MAX&&position<=UINT64_MAX-480,"Local speaker sequence exhausted");++sequence_;position_=position+480;
        speaker_wire::Frame frame;frame.binding=binding;frame.sequence=sequence;frame.position=position;frame.discontinuity=(header[5]&1)!=0;read(frame.pcm);return frame;
    }
    void stop()noexcept override{
        if(!process_.valid())return;
        if(input_.valid()){const std::array<std::uint8_t,16> stop{'E','P','M','I',1,3};DWORD written=0;WriteFile(input_.h,stop.data(),static_cast<DWORD>(stop.size()),&written,nullptr);input_.reset();}
        const auto deadline=Clock::now()+std::chrono::seconds(12);
        while(WaitForSingleObject(process_.h,20)==WAIT_TIMEOUT&&Clock::now()<deadline){discard(output_);discard(errors_);}
        job_.reset();process_.reset();output_.reset();errors_.reset();
    }
private:
    std::atomic_bool& stopping_;unsigned slot_;Report report_;std::wstring exe_;Handle input_,output_,errors_,process_,job_;std::string pending_;bool ready_=false;std::uint64_t sequence_=0,position_=0;
    static void discard(Handle& pipe)noexcept{DWORD available=0;if(!pipe.valid()||!PeekNamedPipe(pipe.h,nullptr,0,nullptr,&available,nullptr))return;while(available){char buffer[4096];DWORD got=0;if(!ReadFile(pipe.h,buffer,std::min<DWORD>(available,sizeof(buffer)),&got,nullptr)||!got)return;if(!PeekNamedPipe(pipe.h,nullptr,0,nullptr,&available,nullptr))return;}}
    void status(){
        DWORD available=0;if(!PeekNamedPipe(errors_.h,nullptr,0,nullptr,&available,nullptr))return;
        while(available){char bytes[1024];DWORD got=0;need(ReadFile(errors_.h,bytes,std::min<DWORD>(available,sizeof(bytes)),&got,nullptr)&&got,"Read speaker endpoint status failed");pending_.append(bytes,got);need(pending_.size()<=16384,"Speaker endpoint status exceeded limit");
            for(auto end=pending_.find('\n');end!=std::string::npos;end=pending_.find('\n')){auto line=pending_.substr(0,end);pending_.erase(0,end+1);if(!line.empty()&&line.back()=='\r')line.pop_back();
                if(line=="SPEAKER_READY slot="+std::to_string(slot_))ready_=true;
                else if(line.starts_with("SPEAKER_READY"))throw std::runtime_error("Speaker component reported a different slot");
                else if(line.starts_with("error:"))throw std::runtime_error("Speaker endpoint: "+line.substr(6,512));
                else if(line.starts_with("SPEAKER_STATS"))report_(line.substr(0,1024));
            }if(!PeekNamedPipe(errors_.h,nullptr,0,nullptr,&available,nullptr))break;
        }
    }
    void read(std::span<std::uint8_t> bytes){const auto deadline=Clock::now()+std::chrono::seconds(5);while(!bytes.empty()){
        need(!stopping_,"Speakers stopped");status();DWORD available=0;need(PeekNamedPipe(output_.h,nullptr,0,nullptr,&available,nullptr),"Speaker endpoint output closed");
        if(available){DWORD got=0;need(ReadFile(output_.h,bytes.data(),std::min<DWORD>(available,static_cast<DWORD>(bytes.size())),&got,nullptr)&&got,"Read speaker PCM failed");bytes=bytes.subspan(got);continue;}
        need(WaitForSingleObject(process_.h,0)==WAIT_TIMEOUT,"Speaker endpoint exited");need(Clock::now()<deadline,"Speaker endpoint audio timed out");Sleep(2);
    }}
    void launch(){
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE a=nullptr,b=nullptr;
        need(CreatePipe(&a,&b,&security,4096),"Create speaker command pipe failed");Handle childIn(a);input_.reset(b);need(SetHandleInformation(input_.h,HANDLE_FLAG_INHERIT,0),"Restrict speaker command pipe failed");
        need(CreatePipe(&a,&b,&security,4096),"Create speaker PCM pipe failed");output_.reset(a);Handle childOut(b);need(SetHandleInformation(output_.h,HANDLE_FLAG_INHERIT,0),"Restrict speaker PCM pipe failed");
        need(CreatePipe(&a,&b,&security,4096),"Create speaker status pipe failed");errors_.reset(a);Handle childError(b);need(SetHandleInformation(errors_.h,HANDLE_FLAG_INHERIT,0),"Restrict speaker status pipe failed");
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<std::uint8_t> attributes(size);auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());need(InitializeProcThreadAttributeList(list,1,0,&size),"Initialize speaker handle list failed");struct Delete{LPPROC_THREAD_ATTRIBUTE_LIST p;~Delete(){DeleteProcThreadAttributeList(p);}}cleanup{list};
        HANDLE inherit[]{childIn.h,childOut.h,childError.h};need(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherit,sizeof(inherit),nullptr,nullptr),"Restrict speaker inherited handles failed");
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=childIn.h;startup.StartupInfo.hStdOutput=childOut.h;startup.StartupInfo.hStdError=childError.h;startup.lpAttributeList=list;
        job_.reset(CreateJobObjectW(nullptr,nullptr));need(job_.valid(),"Create speaker supervisor failed");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(job_.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Configure speaker supervisor failed");
        auto command=L"\""+exe_+L"\" --run-speakers --slot "+std::to_wstring(slot_);PROCESS_INFORMATION child{};need(CreateProcessW(exe_.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&child),"Launch speaker endpoint failed");process_.reset(child.hProcess);Handle thread(child.hThread);
        if(!AssignProcessToJobObject(job_.h,process_.h)){TerminateProcess(process_.h,1);throw std::runtime_error("Cannot supervise speaker endpoint");}need(ResumeThread(thread.h)!=static_cast<DWORD>(-1),"Start speaker endpoint failed");childIn.reset();childOut.reset();childError.reset();
        const auto deadline=Clock::now()+std::chrono::seconds(30);while(!ready_){need(!stopping_,"Speakers stopped");status();need(WaitForSingleObject(process_.h,10)==WAIT_TIMEOUT,"Speaker endpoint exited before ready");need(Clock::now()<deadline,"Speaker endpoint startup timed out");}
    }
};
struct Admitted final:Listener {Availability start()override{return {true,{}};}void stop()noexcept override{}};
}
void send_admitted(security::TlsStream& tls,std::atomic_bool& stop,const Report& report,unsigned slot){
    need(slot&&static_cast<bool>(report),"Invalid speaker session");Cancellation cancellation(tls,stop);Transport transport(tls,stop);speaker_wire::OfferBytes offer{};transport.read(offer);const auto binding=speaker_wire::decode_offer(offer);
    Server server(1);need(server.start(std::make_unique<Admitted>()).available&&server.admit(1).available,"Allocate speaker session failed");auto caps=unavailable_capabilities();caps[4].source={true,{}};Endpoint* endpoint=nullptr;
    Negotiation negotiation(server,1,caps,[&](ResourceKind kind)->std::unique_ptr<Resource>{need(kind==ResourceKind::speaker,"Speaker session received a different resource");auto result=std::make_unique<Endpoint>(stop,slot,report);endpoint=result.get();return result;},wire::CapabilityFormat::with_speakers);
    negotiation.serve_one(transport.control);const auto request=transport.control.receive();need(request.header.operation==wire::Operation::request&&request.header.kind==ResourceKind::speaker&&request.header.resource_id==binding.resource,"Speaker request does not match offer");
    const auto reply=negotiation.handle(request);transport.control.send(reply);if(reply.header.operation!=wire::Operation::grant||!endpoint){for(const auto& state:server.snapshot(1))if(state.request.id==binding.resource&&!state.detail.empty())throw std::runtime_error(state.detail);throw std::runtime_error("Virtual speaker endpoint unavailable");}
    report("SPEAKER_STARTED sending slot="+std::to_string(slot));speaker_wire::Channel frames(binding,[&](auto b){transport.read(b);},[&](auto b){transport.write(b);});while(!stop)frames.send(endpoint->next(binding));
}
void receive(const net::Options& options,const std::wstring& device,std::atomic_bool& stop,std::atomic_bool& mute,std::atomic_uint& volume,std::atomic_uint64_t& muteEpoch,const Report& report){
    need(static_cast<bool>(report),"Speaker status sink required");need(!stop,"Speakers stopped");Playback playback(device,mute,volume,muteEpoch);
    security::TlsStream tls(net::connect_stream(options),false,"");Cancellation cancellation(tls,stop);tls.handshake(Clock::now()+std::chrono::seconds(10));Transport transport(tls,stop);const auto binding=random_binding();transport.write(speaker_wire::encode_offer(binding));
    auto caps=unavailable_capabilities();caps[4].destination={true,{}};transport.control.send(caps_message(caps));auto reply=transport.control.receive();need(reply.header.operation==wire::Operation::capabilities&&reply.header.kind==ResourceKind::display&&!reply.header.resource_id,"Invalid speaker capability response");need(can_connect(ResourceKind::speaker,caps,wire::decode_capabilities(reply.payload)).available,"Server cannot create a virtual speaker");
    auto request=wire::encode_request({binding.resource,ResourceKind::speaker,"speaker"});transport.control.send({{wire::Operation::request,ResourceKind::speaker,binding.resource,static_cast<std::uint32_t>(request.size())},std::move(request)});
    wire::ControlChannel startup([&](auto b){transport.read(b,35);},[&](auto b){transport.write(b);});reply=startup.receive();need(reply.header.operation==wire::Operation::grant&&reply.header.kind==ResourceKind::speaker&&reply.header.resource_id==binding.resource&&reply.payload.empty(),"Server could not start its virtual speaker; check server status");
    report("SPEAKER_STARTED receiving");speaker_wire::Channel frames(binding,[&](auto b){transport.read(b);},[&](auto b){transport.write(b);});while(!stop){const auto epoch=muteEpoch.load();auto frame=frames.receive();playback.push(frame,epoch);}
}
}
