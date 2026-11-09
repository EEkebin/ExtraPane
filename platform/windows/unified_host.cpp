#include <windows.h>
#include <dm/unified_host.hpp>
#include <dm/tls.hpp>
#include <dm/video.hpp>
#include <dm/display.hpp>
#include <dm/resource_session.hpp>
#include <dm/microphone_session.hpp>
#include <dm/speaker_session.hpp>
#include <dm/usb_session.hpp>
#include <extrapane/session_route.hpp>
#include <extrapane/host_resources.hpp>
#include <dm/group.hpp>
#include <dm/control.hpp>
#include <array>
#include <thread>
#include <mutex>
#include <iostream>
#include <syncstream>
#include <stdexcept>
namespace dm::host {
namespace {
using Clock=std::chrono::steady_clock;
using extrapane::ResourceKind;
void report(const std::string& text){std::osyncstream(std::cout)<<text<<std::endl;}
struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(FAILED(result))throw std::runtime_error("Initialize unified server COM failed");}~Apartment(){CoUninitialize();}};
struct Cancel {
    std::jthread thread;
    Cancel(security::TlsStream& tls,std::atomic_bool& stop):thread([&](std::stop_token token){while(!token.stop_requested()&&!stop.load())Sleep(25);if(stop.load())tls.cancel();}){}
};
const char* name(ResourceKind kind){switch(kind){case ResourceKind::display:return "display";case ResourceKind::camera:return "camera";case ResourceKind::microphone:return "microphone";case ResourceKind::usb:return "usb";case ResourceKind::speaker:return "speaker";}return "unknown";}
// Preserve the ownership boundary with old diagnostic listeners. A missing/busy
// component reports only for its resource; it does not prevent server startup.
struct Lease {
    HANDLE value=nullptr;
    explicit Lease(const wchar_t* id){value=CreateMutexW(nullptr,FALSE,id);if(!value)throw std::runtime_error("Resource ownership mutex unavailable");const auto status=WaitForSingleObject(value,0);if(status!=WAIT_OBJECT_0&&status!=WAIT_ABANDONED){CloseHandle(value);value=nullptr;throw std::runtime_error("Resource is hosted by another ExtraPane process");}}
    ~Lease(){if(value){ReleaseMutex(value);CloseHandle(value);}}
};
}
void serve(const net::Options& requested,const std::string& pair,bool allow_input,unsigned frames,Control& control,std::span<const video::SharedScreen> shared_screens,bool existing_only){
    if(existing_only&&(requested.bind_address!="127.0.0.1"||shared_screens.size()!=1||shared_screens[0].id!=1||shared_screens[0].identity.empty()))throw std::invalid_argument("Scoped screen requires one local screen and loopback listener");
    Apartment apartment;
    auto options=requested;options.timeout=std::chrono::milliseconds(250);
    net::TcpListener listener(options);extrapane::HostResources slots;
    std::atomic_bool quit_pending=false;
    Clock::time_point quit_deadline{};
    std::unique_ptr<Lease> camera_lease;std::string camera_busy;
    if(!existing_only)try{camera_lease=std::make_unique<Lease>(L"Local\\ExtraPane.CameraServer");}catch(const std::exception& error){camera_busy=error.what();report("SERVER_RESOURCE_UNAVAILABLE kind=camera "+camera_busy);}
    struct Worker{std::thread thread;std::atomic_bool done=false;};std::vector<std::unique_ptr<Worker>> workers;
    // Bound unauthenticated/unfinished admission work, not established sessions.
    std::atomic_uint pending=0;
    report(existing_only?"SERVER_STARTED resources=existing-screen":"SERVER_STARTED resources=display,camera,microphone,usb,speaker");report("LISTENING "+listener.endpoints());
    auto events=[&](const std::string& text){
        if(control.quit.load()&&(text.starts_with("USB_EJECT_BUSY")||text.starts_with("USB_RETURN_FAILED")))report("SERVER_STOPPING USB could not eject cleanly; ending the session so the source can reclaim its device. Writes may be incomplete.");
        report(text);
    };
    struct Join{std::vector<std::unique_ptr<Worker>>& workers;Control& control;~Join(){control.stop=true;for(auto& w:workers)if(w->thread.joinable())w->thread.join();}}join{workers,control};
    while(!control.stop.load()){
        for(auto it=workers.begin();it!=workers.end();){if((*it)->done.load()){if((*it)->thread.joinable())(*it)->thread.join();it=workers.erase(it);}else ++it;}
        if(control.eject_usb.exchange(false))slots.eject_all();
        if(control.quit.load()){
            if(slots.stop_if_no_usb(control.stop))break;
            if(!quit_pending.exchange(true)){slots.eject_all();quit_deadline=Clock::now()+std::chrono::seconds(10);}
            if(Clock::now()>=quit_deadline){report("SERVER_STOPPING USB eject timed out; ending the import session. Writes may be incomplete.");control.stop=true;break;}
        }
        try{
            auto socket=listener.accept();if(control.stop.load())break;
            if(pending.load()>=32){report("SERVER_SESSION_ERROR Too many pending connection handshakes; retry shortly");continue;}
            auto worker=std::make_unique<Worker>();auto* available=worker.get();workers.push_back(std::move(worker));++pending;
            try{available->thread=std::thread([&,available,socket=std::move(socket)]()mutable{
                struct Admission{std::atomic_uint& pending;bool held=true;void finish(){if(held){--pending;held=false;}}~Admission(){finish();}}admission{pending};
                std::string resource="unclassified";unsigned slot=0;
                try{
                    Apartment worker_apartment;const auto peer=socket.peer_endpoint();
                    security::TlsStream tls(std::move(socket),true,pair);Cancel cancel(tls,control.stop);
                    tls.handshake(Clock::now()+std::chrono::seconds(10));
                    std::array<std::uint8_t,extrapane::session_prefix_size> prefix{};tls.peek(prefix,Clock::now()+std::chrono::seconds(30));
                    if(prefix==access::control_prefix){
                        resource="control";
                        access::ControlRequest request{};tls.read(request,Clock::now()+std::chrono::seconds(5));
                        if(access::parse_control(request).first!=access::ControlOperation::open)throw std::runtime_error("Control must open first");
                        if(control.quit.load()||control.stop.load())throw std::runtime_error("Server is stopping");
                        const bool protected_mode=tls.open_control();
                        admission.finish();
                        tls.write(access::control_reply(access::ControlOperation::open,0,protected_mode),Clock::now()+std::chrono::seconds(5));
                        while(!control.stop.load()&&!control.quit.load()){
                            tls.read(request,Clock::now()+std::chrono::seconds(15));const auto [op,kind]=access::parse_control(request);
                            if(op==access::ControlOperation::close)break;
                            if(op==access::ControlOperation::open)throw std::runtime_error("Control is already open");
                            std::string credential;
                            if(access::control_add(op)){if(existing_only&&kind!=0)throw std::runtime_error("This grant permits only screen capture");auto tickets=tls.control_channels(kind,access::control_input(op));if(protected_mode){credential=access::formatCredential(tickets.at(0));SecureZeroMemory(tickets[0].secret.data(),tickets[0].secret.size());}}
                            auto reply=access::control_reply(op,kind,protected_mode,credential);
                            struct Wipe{std::string& credential;access::ControlReply& reply;~Wipe(){SecureZeroMemory(credential.data(),credential.size());SecureZeroMemory(reply.data(),reply.size());}}wipe{credential,reply};
                            tls.write(reply,Clock::now()+std::chrono::seconds(5));
                        }
                    }else if(prefix==access::group_prefix){
                        if(control.quit.load()||control.stop.load())throw std::runtime_error("Server is stopping");
                        access::GroupRequest request{};tls.read(request,Clock::now()+std::chrono::seconds(5));const auto mask=access::group_mask(request);if(existing_only&&mask!=1)throw std::runtime_error("This grant permits only screen capture");
                        auto tickets=tls.issue_channels(mask);auto reply=access::group_reply(mask,tickets);
                        struct Wipe {std::vector<access::ChannelCredential>& tickets;access::GroupReply& reply;~Wipe(){for(auto& item:tickets)SecureZeroMemory(item.secret.data(),item.secret.size());SecureZeroMemory(reply.data(),reply.size());}}wipe{tickets,reply};
                        tls.write(reply,Clock::now()+std::chrono::seconds(5));resource="admission";
                    }else{
                    const auto kind=extrapane::classify_session_prefix(prefix);if(existing_only&&kind!=ResourceKind::display)throw std::runtime_error("This grant permits only screen capture");tls.require_resource(static_cast<unsigned>(kind));resource=name(kind);
                    if(control.quit.load()||control.stop.load())throw std::runtime_error("Server is stopping; retry after restart");
                    slot=slots.acquire(static_cast<unsigned>(kind),control.quit,control.stop);
                    struct Release{extrapane::HostResources& slots;ResourceKind kind;unsigned slot;~Release(){slots.release(static_cast<unsigned>(kind),slot);}}release{slots,kind,slot};
                    admission.finish();
                    report("SERVER_RESOURCE_STARTING kind="+resource+" slot="+std::to_string(slot)+" peer="+peer);
                    switch(kind){
                    case ResourceKind::display:{
                        const auto ip=peer.substr(0,peer.find(':'));if(!existing_only)display::select_client_layout_profile(std::wstring(ip.begin(),ip.end()),slot);
                        video::host_video(options,pair,frames,60,video::wire::Codec::h264,L"",!existing_only,existing_only?false:allow_input,1280,720,!existing_only,!existing_only,{},nullptr,nullptr,slot,&tls,shared_screens,existing_only);break;
                    }
                    case ResourceKind::camera: if(!camera_lease)throw std::runtime_error(camera_busy);resources::receive_camera_admitted(tls,slot,control.stop,events);break;
                    case ResourceKind::microphone:{microphone::receive_admitted(tls,control.stop,events,slot);break;}
                    case ResourceKind::speaker:{speaker::send_admitted(tls,control.stop,events,slot);break;}
                    case ResourceKind::usb: {auto eject=slots.eject_signal(slot);usb::receive_storage_admitted(tls,control.stop,*eject,events);break;}
                    }
                    }
                }catch(const std::exception& error){if(!control.stop.load())report("SERVER_SESSION_ERROR kind="+resource+" "+error.what());}
                catch(...){if(!control.stop.load())report("SERVER_SESSION_ERROR kind="+resource+" Unexpected connection failure");}
                report("SERVER_RESOURCE_DISCONNECTED kind="+resource+" slot="+std::to_string(slot));available->done=true;
            });}catch(...){--pending;available->done=true;throw;}
        }catch(const std::exception& error){if(std::string_view(error.what())!="Accept timed out")report("SERVER_SESSION_ERROR Unable to accept new connection: "+std::string(error.what()));}
    }
    report("SERVER_STOPPING");
}
}
