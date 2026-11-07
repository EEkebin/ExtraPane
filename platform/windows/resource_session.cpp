#include <windows.h>
#include <bcrypt.h>
#include <dm/resource_session.hpp>
#include <dm/tls.hpp>
#include <dm/camera_receiver.hpp>
#include <dm/camera_stream.hpp>
#include <extrapane/camera_protocol.hpp>
#include <extrapane/negotiation.hpp>
#include <extrapane/camera_fanout.hpp>
#include <map>
#include <array>
#include <thread>
#include <mutex>
#include <stdexcept>

namespace dm::resources {
namespace {
using namespace extrapane;
using Clock=std::chrono::steady_clock;
struct Com {
    Com(){if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))throw std::runtime_error("Camera worker COM initialization failed");}
    ~Com(){CoUninitialize();}
};
struct AdmittedListener final:Listener {Availability start()override{return {true,{}};}void stop()noexcept override{}};
struct LocalLease {
    HANDLE handle=nullptr;
    LocalLease(const std::wstring& name,const char* busy){
        handle=CreateMutexW(nullptr,FALSE,name.c_str());
        if(!handle)throw std::runtime_error("Local camera ownership unavailable");
        const auto result=WaitForSingleObject(handle,0);
        if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;throw std::runtime_error(busy);}
    }
    ~LocalLease(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}
    LocalLease(const LocalLease&)=delete;LocalLease& operator=(const LocalLease&)=delete;
};
std::wstring source_lease_name(const std::wstring& device){
    if(device.empty()||device.size()>4096)throw std::invalid_argument("Invalid local camera identifier");
    std::array<std::uint8_t,32> digest{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(device.data())),
        static_cast<ULONG>(device.size()*sizeof(wchar_t)),digest.data(),static_cast<ULONG>(digest.size()))<0)
        throw std::runtime_error("Camera source lease identity unavailable");
    constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring name=L"Local\\ExtraPane.CameraSource.";
    for(auto byte:digest){name.push_back(hex[byte>>4]);name.push_back(hex[byte&15]);}return name;
}
// This guard is declared after TLS and joins before TLS destruction. cancel()
// only shutdowns its still-owned socket, never closes a potentially reused handle.
struct Cancellation {
    std::jthread worker;
    Cancellation(security::TlsStream& tls,std::atomic_bool& stop):worker([&](std::stop_token local){
        while(!local.stop_requested()){if(stop){tls.cancel();return;}Sleep(50);}
    }){}
};
wire::Message capabilities_message(const Capabilities& caps){auto bytes=wire::encode_capabilities(caps);return {{wire::Operation::capabilities,ResourceKind::display,0,static_cast<std::uint32_t>(bytes.size())},std::move(bytes)};}
media::Binding random_binding(){
    media::Binding value{};
    do{if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&value),sizeof(value),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Camera connection randomness unavailable");}while(!value.resource||!value.generation);
    return value;
}
void check_stop(const std::atomic_bool& stop){if(stop)throw std::runtime_error("Camera connection stopped");}
struct Channels {
    security::TlsStream& tls;std::atomic_bool& stop;
    wire::ControlChannel control;
    Channels(security::TlsStream& t,std::atomic_bool& stopping):tls(t),stop(stopping),
        control([this](auto bytes){read(bytes);},[this](auto bytes){write(bytes);}){}
    void read(std::span<std::uint8_t> bytes){check_stop(stop);tls.read(bytes,Clock::now()+std::chrono::seconds(5));}
    void write(std::span<const std::uint8_t> bytes){check_stop(stop);tls.write(bytes,Clock::now()+std::chrono::seconds(5));}
};
void receive_admitted(security::TlsStream& tls,unsigned slot,std::atomic_bool& stop,const Report& report){
    Com com;Cancellation cancellation(tls,stop);check_stop(stop);
    Channels transport(tls,stop);camera_wire::OfferBytes bytes{};transport.read(bytes);
    const auto offer=camera_wire::decode(bytes);camera::Format format{offer.width,offer.height,offer.fps};
    // This local coordinator owns exactly this admitted TLS connection. Each
    // worker has a unique server-local slot allocated by the listener owner.
    std::string receiverFailure;
    Server server(1);if(!server.start(std::make_unique<AdmittedListener>()).available||!server.admit(1).available)
        throw std::runtime_error("Camera session allocation failed");
    auto capabilities=unavailable_capabilities();capabilities[1].destination={true,{}};
    camera::Receiver* receiver=nullptr;
    Negotiation negotiation(server,1,capabilities,[&](ResourceKind kind)->std::unique_ptr<Resource>{
        if(kind!=ResourceKind::camera)throw std::invalid_argument("Only camera is supported by this listener");
        auto resource=std::make_unique<camera::Receiver>(format,slot,offer.binding,camera::native_camera_registration(),camera::native_camera_decoder,[&](const std::string& reason){receiverFailure=reason;});receiver=resource.get();return resource;
    });
    negotiation.serve_one(transport.control);
    auto request=transport.control.receive();
    if(request.header.operation!=wire::Operation::request||request.header.kind!=ResourceKind::camera||request.header.resource_id!=offer.binding.resource)
        throw std::invalid_argument("Camera request does not match its negotiated session");
    const auto result=negotiation.handle(request);transport.control.send(result);
    if(result.header.operation!=wire::Operation::grant||!receiver)throw std::runtime_error(receiverFailure.empty()?"Camera endpoint unavailable; check camera component installation and GPU decoder support":receiverFailure);
    report("CAMERA_STARTED slot="+std::to_string(slot));
    bool firstHeader=true;
    media::Channel frames(offer.binding,[&](auto data){
        if(firstHeader){firstHeader=false;check_stop(stop);tls.read(data,Clock::now()+std::chrono::seconds(15));}
        else transport.read(data);
    },[&](auto data){transport.write(data);});
    while(!stop)receiver->receive_one(frames);
    server.disconnect(1);
}
}
void receive_camera_admitted(security::TlsStream& tls,unsigned slot,std::atomic_bool& stop,const Report& report){receive_admitted(tls,slot,stop,report);}
void serve_camera(const net::Options& requested,std::atomic_bool& stop,const Report& sink){
    if(!sink||requested.bind_address.empty()||!requested.port)throw std::invalid_argument("Camera server needs an interface, port and status sink");
    // Local namespace intentionally covers one Windows interactive session.
    // Cross-session virtual-camera naming still requires a machine coordinator.
    LocalLease hostLease(L"Local\\ExtraPane.CameraServer","A camera server is already running in this Windows session");
    auto options=requested;options.timeout=std::chrono::milliseconds(250);
    net::TcpListener listener(options);std::mutex reportMutex;
    auto report=[&](const std::string& line)noexcept{try{std::lock_guard lock(reportMutex);sink(line);}catch(...){}};
    report("LISTENING "+listener.endpoints());
    struct Worker {std::thread thread;std::atomic_bool done=true;};
    std::array<Worker,4> workers;
    // On every exit, stop pending workers and wait for their bounded TLS reads.
    struct Join {std::array<Worker,4>& workers;std::atomic_bool& stop;~Join(){stop=true;for(auto& worker:workers)if(worker.thread.joinable())worker.thread.join();}} join{workers,stop};
    while(!stop){
        try{
            auto socket=listener.accept();if(stop)break;
            Worker* available=nullptr;unsigned slot=0;
            for(unsigned i=0;i<workers.size();++i)if(workers[i].done){available=&workers[i];slot=i+1;break;}
            if(!available){report("CAMERA_SESSION_ERROR Camera connection limit reached");continue;}
            if(available->thread.joinable())available->thread.join();available->done=false;
            try{available->thread=std::thread([&,available,slot,socket=std::move(socket)]()mutable{
                try{security::TlsStream tls(std::move(socket),true,"");Cancellation cancellation(tls,stop);tls.handshake(Clock::now()+std::chrono::seconds(10));receive_admitted(tls,slot,stop,report);}
                catch(const std::exception& error){if(!stop)report("CAMERA_SESSION_ERROR slot="+std::to_string(slot)+" "+error.what());}
                catch(...){if(!stop)report("CAMERA_SESSION_ERROR slot="+std::to_string(slot)+" Camera session failed");}
                report("CAMERA_DISCONNECTED slot="+std::to_string(slot));available->done=true;
            });}catch(...){available->done=true;throw;}
        }catch(const std::runtime_error& error){if(std::string_view(error.what())!="Accept timed out")throw;}
    }
}
void send_camera(const net::Options& options,const std::wstring& local_device,std::optional<camera::Format> format,std::atomic_bool& stop,const Report& report,const std::atomic_bool* mirrored){
    if(!report||local_device.empty())throw std::invalid_argument("Camera client requires a selected local device");
    if(format)camera::validate_format(*format);Com com;
    LocalLease sourceLease(source_lease_name(local_device),"This camera is already being shared by another ExtraPane connection");
    security::TlsStream tls(net::connect_stream(options),false,"");Cancellation cancellation(tls,stop);
    tls.handshake(Clock::now()+std::chrono::seconds(10));check_stop(stop);
    Channels transport(tls,stop);const auto binding=random_binding();
    camera::QueuedSource source(local_device,format,binding,mirrored);
    const auto prepared=source.prepare();if(!prepared.available)throw std::runtime_error(prepared.reason);
    check_stop(stop);const auto selected=source.format();
    report("CAMERA_FORMAT "+std::to_string(selected.width)+"x"+std::to_string(selected.height)+" @ "+std::to_string(selected.fps)+" FPS");
    const camera_wire::Offer offer{selected.width,selected.height,selected.fps,binding};
    transport.write(camera_wire::encode(offer));
    auto local=unavailable_capabilities();local[1].source={true,{}};
    transport.control.send(capabilities_message(local));auto reply=transport.control.receive();
    if(reply.header.operation!=wire::Operation::capabilities||reply.header.kind!=ResourceKind::display||reply.header.resource_id!=0)throw std::runtime_error("Camera capability exchange rejected");
    auto peer=wire::decode_capabilities(reply.payload);if(!can_connect(ResourceKind::camera,local,peer).available)throw std::runtime_error("Server cannot receive a camera");
    auto request=wire::encode_request({offer.binding.resource,ResourceKind::camera,"camera"});
    transport.control.send({{wire::Operation::request,ResourceKind::camera,offer.binding.resource,static_cast<std::uint32_t>(request.size())},std::move(request)});
    reply=transport.control.receive();
    if(reply.header.operation!=wire::Operation::grant||reply.header.kind!=ResourceKind::camera||reply.header.resource_id!=offer.binding.resource||!reply.payload.empty())
        throw std::runtime_error("Server rejected the camera; check its installed camera component");
    const auto started=source.start();if(!started.available)throw std::runtime_error(started.reason);
    report("CAMERA_STARTED sending");
    media::Channel frames(offer.binding,[&](auto data){transport.read(data);},[&](auto data){transport.write(data);});
    try{
        while(!stop){
            auto frame=source.next_frame(std::chrono::milliseconds(100));
            if(source.failed())throw std::runtime_error("Camera capture, encoding or its bounded queue failed");
            if(frame)frames.send(*frame);
        }
    }catch(...){source.stop();if(!stop)throw;}
    source.stop();report("CAMERA_DISCONNECTED sending");
}
void broker_camera(const std::wstring& local_device,std::optional<camera::Format> format,std::atomic_bool& stop,const Report& sink,const std::atomic_bool* mirrored,const std::function<std::optional<CameraRecipientCommand>()>& next_command){
    if(local_device.empty()||!sink||!next_command)throw std::invalid_argument("Camera broker requires a source and control channel");
    if(format)camera::validate_format(*format);Com com;
    LocalLease lease(source_lease_name(local_device),"This camera is already owned by another ExtraPane source broker");
    extrapane::camera::Fanout fanout;std::mutex report_mutex;
    auto report=[&](const std::string& row,const char* event,std::string detail){for(auto& c:detail)if(static_cast<unsigned char>(c)<32)c=' ';std::lock_guard lock(report_mutex);sink("RESOURCE_EVENT\t"+row+"\t"+event+"\t"+detail.substr(0,1024));};
    struct Worker{std::atomic_bool stop=false,done=false;std::shared_ptr<extrapane::camera::Fanout::Recipient> recipient;std::thread thread;};
    std::map<std::string,std::unique_ptr<Worker>> workers;
    std::unique_ptr<camera::EncodedSource> source;
    auto remove=[&](const std::string& id){auto it=workers.find(id);if(it==workers.end())return;it->second->stop=true;fanout.remove(id);if(it->second->thread.joinable())it->second->thread.join();workers.erase(it);};
    struct Cleanup{std::map<std::string,std::unique_ptr<Worker>>& workers;std::unique_ptr<camera::EncodedSource>& source;extrapane::camera::Fanout& fanout;~Cleanup(){for(auto& [id,w]:workers){w->stop=true;fanout.remove(id);}if(source)source->stop();for(auto& [id,w]:workers)if(w->thread.joinable())w->thread.join();}}cleanup{workers,source,fanout};
    sink("BROKER_READY");
    while(!stop.load()){
        for(auto it=workers.begin();it!=workers.end();){if(it->second->done){const auto id=it->first;++it;remove(id);}else ++it;}
        if(workers.empty()&&source){source->stop();source.reset();}
        if(source&&source->failed()){for(auto& [id,w]:workers){report(id,"ERROR","Shared camera capture or encoding failed");w->stop=true;fanout.remove(id);}while(!workers.empty())remove(workers.begin()->first);source->stop();source.reset();}
        auto command=next_command();
        if(!command){Sleep(20);continue;}
        struct Erase{std::string& value;~Erase(){SecureZeroMemory(value.data(),value.size());}}erase{command->credential};
        if(command->kind==CameraRecipientCommand::Kind::remove){remove(command->row);continue;}
        if(workers.contains(command->row)){report(command->row,"ERROR","Recipient already exists");continue;}
        const auto request_row=command->row;
        try{
            if(workers.size()>=8)throw std::runtime_error("One camera source supports up to eight recipients");
            if(!source){
                source=std::make_unique<camera::EncodedSource>(local_device,format,[&](camera::EncodedFrame frame){fanout.publish(frame.sequence,frame.capture_timestamp,frame.h264,frame.fresh_encoder);},mirrored);
                auto prepared=source->prepare();if(!prepared.available){source.reset();throw std::runtime_error(prepared.reason);}
                auto started=source->start();if(!started.available){source.reset();throw std::runtime_error(started.reason);}
            }
            const auto selected=source->format();const auto binding=random_binding();
            auto item=std::make_unique<Worker>();auto* worker=item.get();item->recipient=fanout.add(command->row,binding);const auto id=command->row;
            workers.emplace(id,std::move(item));
            try{worker->thread=std::thread([&,worker,selected,binding,command=std::move(*command)]()mutable{
                struct Finish{Worker& worker;~Finish(){worker.done=true;}}finish{*worker};
                struct Wipe{std::string& value;~Wipe(){SecureZeroMemory(value.data(),value.size());value.clear();}}wipe{command.credential};
                try{
                    Com worker_com;security::TlsStream tls(net::connect_stream(command.options),false,"",command.credential);SecureZeroMemory(command.credential.data(),command.credential.size());command.credential.clear();Cancellation cancellation(tls,worker->stop);
                    tls.handshake(Clock::now()+std::chrono::seconds(10));check_stop(worker->stop);Channels transport(tls,worker->stop);
                    const camera_wire::Offer offer{selected.width,selected.height,selected.fps,binding};transport.write(camera_wire::encode(offer));
                    auto local=unavailable_capabilities();local[1].source={true,{}};transport.control.send(capabilities_message(local));auto reply=transport.control.receive();
                    if(reply.header.operation!=wire::Operation::capabilities||reply.header.kind!=ResourceKind::display||reply.header.resource_id!=0)throw std::runtime_error("Camera capability exchange rejected");
                    if(!can_connect(ResourceKind::camera,local,wire::decode_capabilities(reply.payload)).available)throw std::runtime_error("Server cannot receive a camera");
                    auto request=wire::encode_request({binding.resource,ResourceKind::camera,"camera"});transport.control.send({{wire::Operation::request,ResourceKind::camera,binding.resource,static_cast<std::uint32_t>(request.size())},std::move(request)});reply=transport.control.receive();
                    if(reply.header.operation!=wire::Operation::grant||reply.header.kind!=ResourceKind::camera||reply.header.resource_id!=binding.resource||!reply.payload.empty())throw std::runtime_error("Server rejected this camera recipient");
                    worker->recipient->admitted=true;source->request_keyframe();report(command.row,"STARTED","Camera connected; source-wide mirror applies to every recipient");
                    media::Channel frames(binding,[&](auto bytes){transport.read(bytes);},[&](auto bytes){transport.write(bytes);});
                    while(!worker->stop.load()){
                        auto frame=worker->recipient->frames.pop_for(std::chrono::milliseconds(100));
                        if(worker->recipient->failed)throw std::runtime_error("Recipient is too slow; its bounded camera queue overflowed");
                        if(frame)frames.send(*frame);
                    }
                }catch(const std::exception& error){if(!worker->stop)report(command.row,"ERROR",error.what());}
                worker->recipient->admitted=false;worker->recipient->frames.close();report(command.row,"STOPPED","Camera recipient stopped");
            });}catch(...){fanout.remove(id);workers.erase(id);throw;}
        }catch(const std::exception& error){report(request_row,"ERROR",error.what());}
    }
}

}
