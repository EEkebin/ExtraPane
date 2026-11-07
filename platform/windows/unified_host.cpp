#include <windows.h>
#include <dm/unified_host.hpp>
#include <dm/tls.hpp>
#include <dm/video.hpp>
#include <dm/display.hpp>
#include <dm/resource_session.hpp>
#include <dm/microphone_session.hpp>
#include <dm/usb_session.hpp>
#include <extrapane/session_route.hpp>
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
struct Slots {
    std::mutex mutex;std::array<std::array<bool,4>,4> used{};
    unsigned acquire(ResourceKind kind,Control& control){std::lock_guard lock(mutex);if(control.quit.load()||control.stop.load())throw std::runtime_error("Server is stopping; retry after restart");const auto index=static_cast<unsigned>(kind);if(index>=4)throw std::runtime_error("Unsupported server resource");const unsigned capacity=index<2?4:1;for(unsigned slot=0;slot<capacity;++slot)if(!used[index][slot]){used[index][slot]=true;return slot+1;}throw std::runtime_error("Resource is already in use or its connection limit was reached");}
    void release(ResourceKind kind,unsigned slot){std::lock_guard lock(mutex);used[static_cast<unsigned>(kind)][slot-1]=false;}
    bool stop_if_no_usb(Control& control){std::lock_guard lock(mutex);if(used[3][0])return false;control.stop=true;return true;}
};
const char* name(ResourceKind kind){switch(kind){case ResourceKind::display:return "display";case ResourceKind::camera:return "camera";case ResourceKind::microphone:return "microphone";case ResourceKind::usb:return "usb";}return "unknown";}
// Preserve the ownership boundary with old diagnostic listeners. A missing/busy
// component reports only for its resource; it does not prevent server startup.
struct Lease {
    HANDLE value=nullptr;
    explicit Lease(const wchar_t* id){value=CreateMutexW(nullptr,FALSE,id);if(!value)throw std::runtime_error("Resource ownership mutex unavailable");const auto status=WaitForSingleObject(value,0);if(status!=WAIT_OBJECT_0&&status!=WAIT_ABANDONED){CloseHandle(value);value=nullptr;throw std::runtime_error("Resource is hosted by another ExtraPane process");}}
    ~Lease(){if(value){ReleaseMutex(value);CloseHandle(value);}}
};
}
void serve(const net::Options& requested,const std::string& pair,bool allow_input,unsigned frames,Control& control){
    Apartment apartment;
    auto options=requested;options.timeout=std::chrono::milliseconds(250);
    net::TcpListener listener(options);Slots slots;
    std::atomic_bool quit_pending=false;
    std::unique_ptr<Lease> camera_lease;std::string camera_busy;
    try{camera_lease=std::make_unique<Lease>(L"Local\\ExtraPane.CameraServer");}catch(const std::exception& error){camera_busy=error.what();report("SERVER_RESOURCE_UNAVAILABLE kind=camera "+camera_busy);}
    struct Worker{std::thread thread;std::atomic_bool done=true;};std::array<Worker,16> workers;
    report("SERVER_STARTED resources=display,camera,microphone,usb");report("LISTENING "+listener.endpoints());
    auto events=[&](const std::string& text){
        if(text.starts_with("USB_EJECT_BUSY")){control.quit=false;quit_pending=false;report("SERVER_STOP_BLOCKED USB storage is busy; close its files and retry Stop server.");}
        report(text);
    };
    struct Join{std::array<Worker,16>& workers;Control& control;~Join(){control.stop=true;for(auto& w:workers)if(w.thread.joinable())w.thread.join();}}join{workers,control};
    while(!control.stop.load()){
        if(control.quit.load()){
            if(slots.stop_if_no_usb(control))break;
            if(!quit_pending.exchange(true))control.eject_usb=true;
        }
        try{
            auto socket=listener.accept();if(control.stop.load())break;
            Worker* available=nullptr;for(auto& w:workers)if(w.done.load()){available=&w;break;}
            if(!available){report("SERVER_SESSION_ERROR Connection capacity reached");continue;}
            if(available->thread.joinable())available->thread.join();available->done=false;
            try{available->thread=std::thread([&,available,socket=std::move(socket)]()mutable{
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
                        tls.write(access::control_reply(access::ControlOperation::open,0,protected_mode),Clock::now()+std::chrono::seconds(5));
                        while(!control.stop.load()&&!control.quit.load()){
                            tls.read(request,Clock::now()+std::chrono::seconds(15));const auto [op,kind]=access::parse_control(request);
                            if(op==access::ControlOperation::close)break;
                            if(op==access::ControlOperation::open)throw std::runtime_error("Control is already open");
                            std::string credential;
                            if(op==access::ControlOperation::add){auto tickets=tls.control_channels(kind);if(protected_mode){credential=access::formatCredential(tickets.at(0));SecureZeroMemory(tickets[0].secret.data(),tickets[0].secret.size());}}
                            auto reply=access::control_reply(op,kind,protected_mode,credential);
                            struct Wipe{std::string& credential;access::ControlReply& reply;~Wipe(){SecureZeroMemory(credential.data(),credential.size());SecureZeroMemory(reply.data(),reply.size());}}wipe{credential,reply};
                            tls.write(reply,Clock::now()+std::chrono::seconds(5));
                        }
                    }else if(prefix==access::group_prefix){
                        if(control.quit.load()||control.stop.load())throw std::runtime_error("Server is stopping");
                        access::GroupRequest request{};tls.read(request,Clock::now()+std::chrono::seconds(5));const auto mask=access::group_mask(request);
                        auto tickets=tls.issue_channels(mask);auto reply=access::group_reply(mask,tickets);
                        struct Wipe {std::vector<access::ChannelCredential>& tickets;access::GroupReply& reply;~Wipe(){for(auto& item:tickets)SecureZeroMemory(item.secret.data(),item.secret.size());SecureZeroMemory(reply.data(),reply.size());}}wipe{tickets,reply};
                        tls.write(reply,Clock::now()+std::chrono::seconds(5));resource="admission";
                    }else{
                    const auto kind=extrapane::classify_session_prefix(prefix);tls.require_resource(static_cast<unsigned>(kind));resource=name(kind);
                    if(control.quit.load()||control.stop.load())throw std::runtime_error("Server is stopping; retry after restart");
                    slot=slots.acquire(kind,control);
                    if(kind==ResourceKind::usb)control.eject_usb=control.quit.load();
                    struct Release{Slots& slots;ResourceKind kind;unsigned slot;Control& control;~Release(){if(kind==ResourceKind::usb)control.eject_usb=false;slots.release(kind,slot);}}release{slots,kind,slot,control};
                    report("SERVER_RESOURCE_STARTING kind="+resource+" slot="+std::to_string(slot)+" peer="+peer);
                    switch(kind){
                    case ResourceKind::display:{
                        const auto ip=peer.substr(0,peer.find(':'));display::select_layout_profile(std::wstring(ip.begin(),ip.end()));
                        video::host_video(options,pair,frames,60,video::wire::Codec::h264,L"",true,allow_input,1280,720,true,true,{},nullptr,nullptr,slot,&tls);break;
                    }
                    case ResourceKind::camera: if(!camera_lease)throw std::runtime_error(camera_busy);resources::receive_camera_admitted(tls,slot,control.stop,events);break;
                    case ResourceKind::microphone:{Lease lease(L"Local\\ExtraPane.MicrophoneServer");microphone::receive_admitted(tls,control.stop,events);break;}
                    case ResourceKind::usb: usb::receive_storage_admitted(tls,control.stop,control.eject_usb,events);break;
                    }
                    }
                }catch(const std::exception& error){if(!control.stop.load())report("SERVER_SESSION_ERROR kind="+resource+" "+error.what());}
                catch(...){if(!control.stop.load())report("SERVER_SESSION_ERROR kind="+resource+" Unexpected connection failure");}
                report("SERVER_RESOURCE_DISCONNECTED kind="+resource+" slot="+std::to_string(slot));available->done=true;
            });}catch(...){available->done=true;throw;}
        }catch(const std::runtime_error& error){if(std::string_view(error.what())!="Accept timed out")throw;}
    }
    report("SERVER_STOPPING");
}
}
