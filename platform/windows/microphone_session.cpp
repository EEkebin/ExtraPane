#include <windows.h>
#include <bcrypt.h>
#include <dm/microphone_session.hpp>
#include <dm/microphone.hpp>
#include <dm/microphone_fanout.hpp>
#include <dm/tls.hpp>
#include <extrapane/microphone_protocol.hpp>
#include <extrapane/negotiation.hpp>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <thread>
#include <stdexcept>
#include <map>
#include <mutex>

namespace dm::microphone {
namespace {
using namespace extrapane;
using Clock=std::chrono::steady_clock;
struct Handle {
    HANDLE h=nullptr;
    Handle()=default;explicit Handle(HANDLE value):h(value){}
    ~Handle(){reset();}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    void reset(HANDLE value=nullptr){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=value;}
    bool valid()const{return h&&h!=INVALID_HANDLE_VALUE;}
};
void need(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
media::Binding random_binding(){media::Binding b{};do{need(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&b),sizeof(b),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Microphone randomness unavailable");}while(!b.resource||!b.generation);return b;}
struct SourceLease {
    Handle handle;
    explicit SourceLease(const std::wstring& device){
        need(!device.empty()&&device.size()<=4096,"Invalid local microphone identifier");std::array<std::uint8_t,32> digest{};
        need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(device.data())),static_cast<ULONG>(device.size()*sizeof(wchar_t)),digest.data(),static_cast<ULONG>(digest.size()))>=0,"Microphone ownership identity failed");
        std::wstring name=L"Local\\ExtraPane.MicrophoneSource.";constexpr wchar_t hex[]=L"0123456789abcdef";for(auto byte:digest){name+=hex[byte>>4];name+=hex[byte&15];}
        handle.reset(CreateMutexW(nullptr,FALSE,name.c_str()));need(handle.valid(),"Create microphone ownership lease failed");const auto result=WaitForSingleObject(handle.h,0);
        need(result==WAIT_OBJECT_0||result==WAIT_ABANDONED,"This microphone is already shared by another ExtraPane connection");
    }
    ~SourceLease(){ReleaseMutex(handle.h);}
};
struct Cancellation {
    std::jthread worker;
    Cancellation(security::TlsStream& tls,std::atomic_bool& stop):worker([&](std::stop_token token){while(!token.stop_requested()){if(stop){tls.cancel();return;}Sleep(25);}}){}
};
struct Transport {
    security::TlsStream& tls;std::atomic_bool& stop;wire::ControlChannel control;
    Transport(security::TlsStream& t,std::atomic_bool& s):tls(t),stop(s),control([this](auto b){read(b);},[this](auto b){write(b);}){}
    void read(std::span<std::uint8_t> bytes,unsigned seconds=5){if(stop)throw std::runtime_error("Microphone stopped");tls.read(bytes,Clock::now()+std::chrono::seconds(seconds));}
    void write(std::span<const std::uint8_t> bytes){if(stop)throw std::runtime_error("Microphone stopped");tls.write(bytes,Clock::now()+std::chrono::seconds(5));}
};
wire::Message caps_message(const Capabilities& caps){auto bytes=wire::encode_capabilities(caps);return {{wire::Operation::capabilities,ResourceKind::display,0,static_cast<std::uint32_t>(bytes.size())},std::move(bytes)};}
class Endpoint final:public Resource {
public:
    explicit Endpoint(std::atomic_bool& stop,unsigned slot):stop_(stop),slot_(slot){validate_endpoint_slot(slot_);}
    ~Endpoint(){stop();}
    Availability prepare()override{
        std::wstring path(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(!length||length>=path.size())return {false,"Cannot locate microphone component"};path.resize(length);
        exe_=(std::filesystem::path(path).parent_path()/L"ExtraPane.MicrophoneEndpoint.exe").wstring();
        if(!std::filesystem::is_regular_file(exe_))return {false,"Microphone endpoint component is missing"};return {true,{}};
    }
    Availability start()override{
        try{launch();return {true,{}};}catch(const std::exception& e){stop();return {false,e.what()};}
    }
    void pcm(const microphone_wire::Frame& frame){
        if(frame.muted!=muted_){const std::array<std::uint8_t,1> value{static_cast<std::uint8_t>(frame.muted)};command(2,value,false);muted_=frame.muted;}
        command(1,frame.pcm,false);
    }
    void stop()noexcept override{
        if(!process_.valid())return;
        try{if(input_.valid())command(3,{},true);}catch(...){}
        input_.reset();
        // Endpoint owns only its attachment and detaches on Stop/EOF. Give its
        // bounded backend cleanup time before the process job is closed.
        const auto deadline=Clock::now()+std::chrono::seconds(12);
        while(WaitForSingleObject(process_.h,25)==WAIT_TIMEOUT&&Clock::now()<deadline){try{drain();}catch(...){break;}}
        job_.reset();process_.reset();output_.reset();
    }
private:
    std::atomic_bool& stop_;unsigned slot_;std::wstring exe_;Handle input_,output_,process_,job_;std::uint32_t sequence_=0;
    std::string outputText_;bool ready_=false,muted_=false;
    void drain(){
        DWORD available=0;if(!PeekNamedPipe(output_.h,nullptr,0,nullptr,&available,nullptr))return;
        while(available){char bytes[1024];DWORD read=0;need(ReadFile(output_.h,bytes,std::min<DWORD>(available,sizeof(bytes)),&read,nullptr)&&read,"Read microphone endpoint status failed");
            outputText_.append(bytes,read);need(outputText_.size()<=65536,"Microphone endpoint status exceeded limit");
            for(auto newline=outputText_.find('\n');newline!=std::string::npos;newline=outputText_.find('\n')){
                auto line=outputText_.substr(0,newline);outputText_.erase(0,newline+1);if(!line.empty()&&line.back()=='\r')line.pop_back();
                if(endpoint_ready(slot_,line))ready_=true;
                else if(line.starts_with("MICROPHONE_READY"))throw std::runtime_error("Microphone component reported a different endpoint slot");
                // This detail stays on the receiving PC; negotiation sends a
                // generic rejection to the peer, never local paths/identities.
                if(line.starts_with("error:"))throw std::runtime_error("Microphone endpoint: "+line.substr(6,512));
            }
            if(!PeekNamedPipe(output_.h,nullptr,0,nullptr,&available,nullptr))break;
        }
    }
    void launch(){
        const auto nonce=random_binding();const auto name=L"\\\\.\\pipe\\ExtraPane.Microphone.Input."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(nonce.resource);
        input_.reset(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_OUTBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr));need(input_.valid(),"Create microphone component input failed");
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        Handle childIn(CreateFileW(name.c_str(),GENERIC_READ,0,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));need(childIn.valid(),"Open microphone component input failed");
        OVERLAPPED connection{};Handle connected(CreateEventW(nullptr,TRUE,FALSE,nullptr));need(connected.valid(),"Create pipe event failed");connection.hEvent=connected.h;
        const auto accepted=ConnectNamedPipe(input_.h,&connection);need(accepted||GetLastError()==ERROR_PIPE_CONNECTED,"Connect microphone component input failed");
        HANDLE outRead=nullptr,outWrite=nullptr;need(CreatePipe(&outRead,&outWrite,&security,4096),"Create microphone component output failed");output_.reset(outRead);Handle childOut(outWrite);
        need(SetHandleInformation(output_.h,HANDLE_FLAG_INHERIT,0),"Restrict microphone component output failed");
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<std::uint8_t> attributes(size);
        auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());need(InitializeProcThreadAttributeList(list,1,0,&size),"Initialize microphone component handles failed");
        struct Delete{LPPROC_THREAD_ATTRIBUTE_LIST value;~Delete(){DeleteProcThreadAttributeList(value);}}cleanup{list};
        HANDLE inherit[]{childIn.h,childOut.h};need(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherit,sizeof(inherit),nullptr,nullptr),"Restrict microphone component handles failed");
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput=childIn.h;startup.StartupInfo.hStdOutput=childOut.h;startup.StartupInfo.hStdError=childOut.h;startup.lpAttributeList=list;
        job_.reset(CreateJobObjectW(nullptr,nullptr));need(job_.valid(),"Create microphone component job failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(job_.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Configure microphone component job failed");
        std::wstring command=L"\""+exe_+L"\""+endpoint_arguments(slot_);PROCESS_INFORMATION child{};
        need(CreateProcessW(exe_.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&child),"Launch microphone endpoint failed");
        process_.reset(child.hProcess);Handle thread(child.hThread);
        if(!AssignProcessToJobObject(job_.h,process_.h)){TerminateProcess(process_.h,1);throw std::runtime_error("Cannot supervise microphone endpoint");}
        need(ResumeThread(thread.h)!=static_cast<DWORD>(-1),"Start microphone endpoint failed");childIn.reset();childOut.reset();
        const auto deadline=Clock::now()+std::chrono::seconds(30);
        while(!ready_){need(!stop_,"Microphone stopped");drain();need(WaitForSingleObject(process_.h,25)==WAIT_TIMEOUT,"Microphone endpoint exited before ready");need(Clock::now()<deadline,"Microphone endpoint startup timed out");}
    }
    void write(std::span<const std::uint8_t> bytes,bool closing){
        OVERLAPPED operation{};Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));need(event.valid(),"Create microphone write event failed");operation.hEvent=event.h;
        DWORD count=0;const auto ok=WriteFile(input_.h,bytes.data(),static_cast<DWORD>(bytes.size()),&count,&operation);
        if(!ok){need(GetLastError()==ERROR_IO_PENDING,"Write microphone endpoint failed");const auto deadline=Clock::now()+std::chrono::seconds(2);
            while(WaitForSingleObject(event.h,25)==WAIT_TIMEOUT){
                if((stop_&&!closing)||Clock::now()>=deadline||WaitForSingleObject(process_.h,0)==WAIT_OBJECT_0){CancelIoEx(input_.h,&operation);GetOverlappedResult(input_.h,&operation,&count,TRUE);throw std::runtime_error("Microphone endpoint write canceled or timed out");}
            }
            need(GetOverlappedResult(input_.h,&operation,&count,FALSE),"Complete microphone endpoint write failed");
        }
        need(count==bytes.size(),"Short microphone endpoint write");
    }
    void command(std::uint8_t type,std::span<const std::uint8_t> payload,bool closing){
        need(sequence_!=std::numeric_limits<std::uint32_t>::max(),"Microphone endpoint sequence exhausted");
        std::array<std::uint8_t,16+960> bytes{};bytes[0]='E';bytes[1]='P';bytes[2]='M';bytes[3]='I';bytes[4]=1;bytes[5]=type;
        for(unsigned i=0;i<4;++i){bytes[11-i]=static_cast<std::uint8_t>(payload.size()>>(8*i));bytes[15-i]=static_cast<std::uint8_t>(sequence_>>(8*i));}
        need(payload.size()<=960,"Microphone endpoint packet too large");std::copy(payload.begin(),payload.end(),bytes.begin()+16);
        write(std::span(bytes).first(16+payload.size()),closing);++sequence_;if(!closing)drain();
    }
};
struct Admitted final:Listener {Availability start()override{return {true,{}};}void stop()noexcept override{}};
void receive_payload(security::TlsStream& tls,std::atomic_bool& stop,const Report& report,unsigned slot){
    validate_endpoint_slot(slot);
    Cancellation cancellation(tls,stop);
    Transport transport(tls,stop);microphone_wire::OfferBytes offer{};transport.read(offer);const auto binding=microphone_wire::decode_offer(offer);
    Server server(1);need(server.start(std::make_unique<Admitted>()).available&&server.admit(1).available,"Allocate microphone session failed");
    auto caps=unavailable_capabilities();caps[2].destination={true,{}};Endpoint* endpoint=nullptr;
    Negotiation negotiation(server,1,caps,[&](ResourceKind kind)->std::unique_ptr<Resource>{if(kind!=ResourceKind::microphone)throw std::runtime_error("Microphone listener received another resource");auto result=std::make_unique<Endpoint>(stop,slot);endpoint=result.get();return result;});
    negotiation.serve_one(transport.control);auto request=transport.control.receive();
    need(request.header.operation==wire::Operation::request&&request.header.kind==ResourceKind::microphone&&request.header.resource_id==binding.resource,"Microphone request does not match offer");
    const auto result=negotiation.handle(request);transport.control.send(result);
    if(result.header.operation!=wire::Operation::grant||!endpoint){
        for(const auto& state:server.snapshot(1))if(state.request.id==binding.resource&&!state.detail.empty())throw std::runtime_error(state.detail);
        throw std::runtime_error("Microphone endpoint unavailable; check the managed component");
    }
    report("MICROPHONE_STARTED receiving slot="+std::to_string(slot));
    microphone_wire::Channel frames(binding,[&](auto b){transport.read(b);},[&](auto b){transport.write(b);});
    while(!stop){auto frame=frames.receive();endpoint->pcm(frame);}
}
}
void receive_admitted(security::TlsStream& tls,std::atomic_bool& stop,const Report& report,unsigned slot){
    validate_endpoint_slot(slot);Handle lease(CreateMutexW(nullptr,FALSE,endpoint_server_lease(slot).c_str()));need(lease.valid(),"Cannot reserve microphone receiving slot");
    const auto acquired=WaitForSingleObject(lease.h,0);need(acquired==WAIT_OBJECT_0||acquired==WAIT_ABANDONED,"This microphone receiving slot is already occupied");
    struct Unlock{HANDLE h;~Unlock(){ReleaseMutex(h);}}unlock{lease.h};receive_payload(tls,stop,report,slot);
}
void serve(const net::Options& requested,std::atomic_bool& stop,const Report& report){
    need(report&& !requested.bind_address.empty()&&requested.port,"Microphone server requires interface, port and status sink");
    Handle lease(CreateMutexW(nullptr,FALSE,endpoint_server_lease(1).c_str()));need(lease.valid(),"Create microphone server lease failed");
    const auto acquired=WaitForSingleObject(lease.h,0);need(acquired==WAIT_OBJECT_0||acquired==WAIT_ABANDONED,"A microphone server is already running");
    struct Unlock{HANDLE h;~Unlock(){ReleaseMutex(h);}}unlock{lease.h};
    auto options=requested;options.timeout=std::chrono::milliseconds(250);net::TcpListener listener(options);report("LISTENING "+listener.endpoints());
    // The dedicated diagnostic listener reserves slot 1. The unified listener
    // allocates distinct slots and invokes receive_admitted concurrently.
    while(!stop){try{
        auto socket=listener.accept();try{security::TlsStream tls(std::move(socket),true,"");Cancellation cancellation(tls,stop);tls.handshake(Clock::now()+std::chrono::seconds(10));receive_payload(tls,stop,report,1);}catch(const std::exception& error){if(!stop)report(std::string("MICROPHONE_SESSION_ERROR ")+error.what());}
        report("MICROPHONE_DISCONNECTED receiving");
    }catch(const std::runtime_error& error){if(std::string_view(error.what())!="Accept timed out")throw;}}
}
void send(const net::Options& options,const std::wstring& device,std::atomic_bool& stop,std::atomic_bool& mute,const Report& report){
    need(report&&!device.empty(),"Select a local microphone");SourceLease lease(device);security::TlsStream tls(net::connect_stream(options),false,"");Cancellation cancellation(tls,stop);
    tls.handshake(Clock::now()+std::chrono::seconds(10));Transport transport(tls,stop);const auto binding=random_binding();transport.write(microphone_wire::encode_offer(binding));
    auto caps=unavailable_capabilities();caps[2].source={true,{}};transport.control.send(caps_message(caps));auto reply=transport.control.receive();
    need(reply.header.operation==wire::Operation::capabilities&&reply.header.kind==ResourceKind::display&&!reply.header.resource_id,"Invalid microphone capability response");
    need(can_connect(ResourceKind::microphone,caps,wire::decode_capabilities(reply.payload)).available,"Server cannot receive microphones");
    auto request=wire::encode_request({binding.resource,ResourceKind::microphone,"microphone"});transport.control.send({{wire::Operation::request,ResourceKind::microphone,binding.resource,static_cast<std::uint32_t>(request.size())},std::move(request)});
    // Receiver may need to attach its managed UAC endpoint before granting.
    wire::ControlChannel startup([&](auto b){transport.read(b,35);},[&](auto b){transport.write(b);});reply=startup.receive();
    need(reply.header.operation==wire::Operation::grant&&reply.header.kind==ResourceKind::microphone&&reply.header.resource_id==binding.resource&&reply.payload.empty(),"Server could not start microphone output; check its server status for details");
    Capture source(device);source.set_muted(mute);source.start();report("MICROPHONE_STARTED sending");
    microphone_wire::Channel frames(binding,[&](auto b){transport.read(b);},[&](auto b){transport.write(b);});
    microphone_wire::Frame frame;frame.binding=binding;std::size_t filled=0;bool previousMute=mute;
    while(!stop){
        const bool muted=mute;if(muted!=previousMute){source.set_muted(muted);previousMute=muted;if(muted){frame.pcm.fill(0);frame.muted=true;}}
        const auto packet=source.read(std::chrono::milliseconds(100));if(!packet)continue;
        for(std::size_t i=0;i<packet->samples.size();++i){
            if(!filled){frame.position=packet->device_position+i;frame.muted=packet->muted||muted;frame.discontinuity=packet->discontinuity;}
            if(packet->discontinuity)frame.discontinuity=true;if(packet->muted||muted)frame.muted=true;
            const auto value=static_cast<std::uint16_t>(packet->samples[i]);frame.pcm[filled*2]=static_cast<std::uint8_t>(value);frame.pcm[filled*2+1]=static_cast<std::uint8_t>(value>>8);
            if(++filled==microphone_wire::sample_count){if(frame.muted)frame.pcm.fill(0);frames.send(frame);++frame.sequence;filled=0;}
        }
    }
    source.stop();report("MICROPHONE_DISCONNECTED sending");
}

struct SourceBroker::State {
    struct Recipient {
        std::string row,credential;net::Options options;FanoutQueue queue;
        std::atomic_bool stop=false,ready=false,ended=false;
        std::mutex error_mutex;std::string source_error;std::thread worker;
        ~Recipient(){if(!credential.empty())SecureZeroMemory(credential.data(),credential.size());}
    };
    std::wstring device;std::atomic_bool& external_stop;Report report;std::atomic_bool quit=false;
    std::mutex mutex;std::map<std::string,std::shared_ptr<Recipient>> recipients;std::thread capture_worker;
    State(std::wstring selected,std::atomic_bool& stopping,Report sink):device(std::move(selected)),external_stop(stopping),report(std::move(sink)){
        need(!device.empty()&&device.size()<=4096&&static_cast<bool>(report),"Select a valid microphone source and status sink");
        capture_worker=std::thread([this]{capture_loop();});
    }
    ~State(){
        quit=true;for(auto& r:snapshot(false)){r->stop=true;r->queue.close();}
        if(capture_worker.joinable())capture_worker.join();
        for(auto& [id,r]:recipients)if(r->worker.joinable())r->worker.join();
    }
    std::vector<std::shared_ptr<Recipient>> snapshot(bool ready_only){
        std::vector<std::shared_ptr<Recipient>> out;std::lock_guard lock(mutex);
        for(const auto& [id,r]:recipients)if(!r->ended&&!r->stop&&(!ready_only||r->ready))out.push_back(r);return out;
    }
    void event(const Recipient& r,const char* state,std::string message={})noexcept{
        for(auto& ch:message)if(static_cast<unsigned char>(ch)<32)ch=' ';if(message.size()>512)message.resize(512);
        try{report("RESOURCE_EVENT\t"+r.row+"\t"+state+"\t"+message);}catch(...){ }
    }
    void fail_source(const std::string& reason){
        for(auto& r:snapshot(false)){{std::lock_guard lock(r->error_mutex);r->source_error=reason;}r->stop=true;r->queue.close();}
    }
    void capture_loop()noexcept{
        std::unique_ptr<SourceLease> lease;std::unique_ptr<Capture> source;FanoutFrame frame;std::size_t filled=0;
        while(!quit&&!external_stop){
            auto active=snapshot(true);
            if(active.empty()){
                source.reset();lease.reset();filled=0;std::this_thread::sleep_for(std::chrono::milliseconds(25));continue;
            }
            try{
                if(!source){lease=std::make_unique<SourceLease>(device);source=std::make_unique<Capture>(device);source->start();filled=0;}
                const auto packet=source->read(std::chrono::milliseconds(100));if(!packet)continue;
                for(std::size_t i=0;i<packet->samples.size();++i){
                    if(!filled){frame.position=packet->device_position+i;frame.muted=packet->muted;frame.discontinuity=packet->discontinuity;}
                    frame.discontinuity|=packet->discontinuity;frame.muted|=packet->muted;
                    const auto value=static_cast<std::uint16_t>(packet->samples[i]);frame.pcm[filled*2]=static_cast<std::uint8_t>(value);frame.pcm[filled*2+1]=static_cast<std::uint8_t>(value>>8);
                    if(++filled==microphone_wire::sample_count){if(frame.muted)frame.pcm.fill(0);for(auto& recipient:active)if(!recipient->stop)recipient->queue.push(frame);filled=0;}
                }
            }catch(const std::exception& error){source.reset();lease.reset();filled=0;fail_source(error.what());}
            catch(...){source.reset();lease.reset();filled=0;fail_source("Shared microphone capture failed");}
        }
        source.reset();lease.reset();for(auto& r:snapshot(false)){r->stop=true;r->queue.close();}
    }
    void transmit(const std::shared_ptr<Recipient>& r)noexcept{
        std::string error;
        try{
            need(!quit&&!external_stop&&!r->stop,"Microphone recipient canceled before connection");
            security::TlsStream tls(net::connect_stream(r->options),false,"",r->credential);
            if(!r->credential.empty())SecureZeroMemory(r->credential.data(),r->credential.size());r->credential.clear();
            Cancellation cancellation(tls,r->stop);tls.handshake(Clock::now()+std::chrono::seconds(10));
            Transport transport(tls,r->stop);const auto binding=random_binding();transport.write(microphone_wire::encode_offer(binding));
            auto caps=unavailable_capabilities();caps[2].source={true,{}};transport.control.send(caps_message(caps));auto reply=transport.control.receive();
            need(reply.header.operation==wire::Operation::capabilities&&reply.header.kind==ResourceKind::display&&!reply.header.resource_id,"Invalid microphone capability response");
            need(can_connect(ResourceKind::microphone,caps,wire::decode_capabilities(reply.payload)).available,"Server cannot receive microphones");
            auto request=wire::encode_request({binding.resource,ResourceKind::microphone,"microphone"});transport.control.send({{wire::Operation::request,ResourceKind::microphone,binding.resource,static_cast<std::uint32_t>(request.size())},std::move(request)});
            wire::ControlChannel startup([&](auto b){transport.read(b,35);},[&](auto b){transport.write(b);});reply=startup.receive();
            need(reply.header.operation==wire::Operation::grant&&reply.header.kind==ResourceKind::microphone&&reply.header.resource_id==binding.resource&&reply.payload.empty(),"Server could not allocate a microphone endpoint");
            microphone_wire::Channel frames(binding,[&](auto b){transport.read(b);},[&](auto b){transport.write(b);});
            r->ready=true;std::uint64_t sequence=0;bool started=false;
            while(!r->stop&&!quit&&!external_stop){
                const auto packet=r->queue.pop(std::chrono::milliseconds(100));if(!packet)continue;if(r->stop)break;
                microphone_wire::Frame outgoing;outgoing.binding=binding;outgoing.sequence=sequence;outgoing.position=packet->position;outgoing.muted=packet->muted;outgoing.discontinuity=packet->discontinuity;outgoing.pcm=packet->pcm;
                frames.send(outgoing);need(sequence!=std::numeric_limits<std::uint64_t>::max(),"Microphone sequence exhausted");++sequence;
                if(!started){event(*r,"STARTED","Microphone stream admitted and sending");started=true;}
            }
        }catch(const std::exception& failure){if(!r->stop&&!quit&&!external_stop)error=failure.what();}
        catch(...){if(!r->stop&&!quit&&!external_stop)error="Microphone recipient failed";}
        if(!r->credential.empty())SecureZeroMemory(r->credential.data(),r->credential.size());r->credential.clear();
        {std::lock_guard lock(r->error_mutex);if(!r->source_error.empty())error=r->source_error;}
        r->ready=false;r->stop=true;r->queue.close();
        if(!error.empty())event(*r,"ERROR",std::move(error));else event(*r,"STOPPED","Microphone recipient stopped");r->ended=true;
    }
};
SourceBroker::SourceBroker(std::wstring device,std::atomic_bool& stop,Report report):state_(std::make_unique<State>(std::move(device),stop,std::move(report))){}
SourceBroker::~SourceBroker()=default;
void SourceBroker::add(std::string row,net::Options options,std::string credential){
    struct Wipe{std::string& value;~Wipe(){if(!value.empty())SecureZeroMemory(value.data(),value.size());}}wipe{credential};
    need(!row.empty()&&row.size()<=128&&std::all_of(row.begin(),row.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';}),"Invalid microphone recipient row identity");
    if(!credential.empty())need(access::validCredential(credential),"Malformed microphone recipient credential");
    auto& s=*state_;need(!s.quit&&!s.external_stop,"Microphone broker is stopping");
    std::shared_ptr<State::Recipient> previous;{std::lock_guard lock(s.mutex);const auto found=s.recipients.find(row);if(found!=s.recipients.end()){need(found->second->ended,"Microphone recipient is already active");previous=found->second;s.recipients.erase(found);}}
    if(previous&&previous->worker.joinable())previous->worker.join();
    auto r=std::make_shared<State::Recipient>();r->row=std::move(row);r->options=std::move(options);r->credential=credential;
    {std::lock_guard lock(s.mutex);need(s.recipients.size()<16,"One microphone source supports at most 16 recipient rows");s.recipients.emplace(r->row,r);}
    try{r->worker=std::thread([&s,r]{s.transmit(r);});}catch(...){std::lock_guard lock(s.mutex);s.recipients.erase(r->row);throw;}
}
void SourceBroker::remove(const std::string& row){
    auto& s=*state_;std::shared_ptr<State::Recipient> r;{std::lock_guard lock(s.mutex);const auto found=s.recipients.find(row);need(found!=s.recipients.end(),"Microphone recipient does not exist");r=found->second;s.recipients.erase(found);}
    r->stop=true;r->queue.close();if(r->worker.joinable())r->worker.join();
}
void SourceBroker::mute(const std::string& row,bool value){
    auto& s=*state_;std::lock_guard lock(s.mutex);const auto found=s.recipients.find(row);need(found!=s.recipients.end()&&!found->second->ended,"Microphone recipient does not exist");found->second->queue.mute(value);
}
}
