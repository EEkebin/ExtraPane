#include "host.hpp"
#include "display_slots.hpp"
#include "host_tls.hpp"
#include "host_media.hpp"
#include "host_input.hpp"
#include "display.hpp"
#include "transport.hpp"
#include "network.hpp"
#include <dm/video_protocol.hpp>
#include <dm/control.hpp>
#include <dm/quality.hpp>
#import <ApplicationServices/ApplicationServices.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>
#include <thread>
#include <array>
#include <memory>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace dm::mac {
namespace wire=video::wire;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
struct Host::State {
    std::shared_ptr<access::Gate> gate;unsigned port;std::atomic_bool stop=false,running=true;std::string address;std::function<void(std::string)> report;std::thread listener;
    struct Worker {std::thread thread;std::atomic_bool done=false;};
    std::vector<std::unique_ptr<Worker>> workers;std::atomic_uint pending=0;DisplaySlots displays;
    struct Admission {std::atomic_uint& count;bool pending=true;~Admission(){ready();}void ready(){if(pending){pending=false;--count;}}};
    State(std::string bind,std::function<void(std::string)> callback,unsigned selectedPort,std::shared_ptr<access::Gate> selectedGate):gate(std::move(selectedGate)),port(selectedPort),address(std::move(bind)),report(std::move(callback)){}
    void serve(int socket){Admission admission{pending};@autoreleasepool{if(@available(macOS 13.0,*)){
        auto cancelled=[this]{return stop.load();};HostTls transport(socket,cancelled,gate.get());auto bytes=transport.read(8,10);
        if(std::equal(bytes.begin(),bytes.end(),access::control_prefix.begin())){
            access::ControlRequest request{};std::copy(bytes.begin(),bytes.end(),request.begin());auto tail=transport.read(8,10);std::copy(tail.begin(),tail.end(),request.begin()+8);
            auto [operation,kind]=access::parse_control(request);if(operation!=access::ControlOperation::open)throw std::runtime_error("Control must open first");
            const bool protectedMode=transport.openControl();admission.ready();transport.write(access::control_reply(operation,kind,protectedMode),10);
            while(!stop){auto data=transport.read(16,20);std::copy(data.begin(),data.end(),request.begin());auto parsed=access::parse_control(request);operation=parsed.first;kind=parsed.second;
                if(operation==access::ControlOperation::open)throw std::runtime_error("Control already open");std::string credential;
                if(operation==access::ControlOperation::add){auto tickets=transport.controlChannels(kind);if(protectedMode){credential=access::formatCredential(tickets.at(0));std::fill(tickets[0].secret.begin(),tickets[0].secret.end(),0);}}
                auto reply=access::control_reply(operation,kind,protectedMode,credential);transport.write(reply,10);std::fill(credential.begin(),credential.end(),0);std::fill(reply.begin(),reply.end(),0);
                if(operation==access::ControlOperation::close)break;
            }return;
        }
        transport.requireResource(0);auto rest=transport.read(24,10);bytes.insert(bytes.end(),rest.begin(),rest.end());auto display=wire::take_client_display(bytes);unsigned wanted=bytes[8],scale=bytes[9],inputRequested=bytes[7],keyboard=bytes[10];bytes[7]=bytes[8]=bytes[9]=bytes[10]=0;
        if(!std::equal(bytes.begin(),bytes.end(),wire::request.begin()) || inputRequested>1 || wanted>15 || bool(inputRequested)!=bool(wanted) || scale>2 || keyboard>2 || (keyboard&&(!inputRequested||!(wanted&4))))throw std::runtime_error("Invalid client request");
        if(!CGPreflightScreenCaptureAccess())throw std::runtime_error("Screen Recording permission is required");
        auto displayLease=displays.acquire();const unsigned slot=displayLease.index();admission.ready();
        unsigned w=display.width?display.width:1920,h=display.height?display.height:1080,hz=std::clamp(display.hz?display.hz:60,1u,60u);
        double reduction=std::min({1.0,4096.0/w,4096.0/h,std::sqrt(8847360.0/(double(w)*h))});w=std::max(640u,unsigned(w*reduction)&~1u);h=std::max(480u,unsigned(h*reduction)&~1u);
        VirtualDisplay monitor(w,h,hz,slot);HostInput broker(slot,monitor.displayID());unsigned granted=wanted&(AXIsProcessTrusted()?3u:0u);std::unique_ptr<HostMedia> media;uint64_t epoch=0;unsigned outW=0,outH=0;
        auto prepare=[&]{broker.reset();media.reset();auto rect=CGDisplayBounds(monitor.displayID());w=unsigned(rect.size.width);h=unsigned(rect.size.height);auto dimensions=video::stream_size(w,h,scale==1?75:scale==2?50:100);outW=dimensions.first;outH=dimensions.second;
            if(outW>4096 || outH>4096 || uint64_t(outW)*outH>8847360)throw std::runtime_error("Display mode exceeds streaming limits");
            media=std::make_unique<HostMedia>(monitor.displayID(),outW,outH,hz);if(granted){uint64_t next=0;do{arc4random_buf(&next,sizeof(next));}while(!next || next==epoch);epoch=next;}
            transport.write(wire::encode_offer({outW,outH,hz,0,wire::Codec::h264,uint8_t(granted),epoch}),10);if(transport.read(4,20)!=std::vector<uint8_t>(wire::ready.begin(),wire::ready.end()))throw std::runtime_error("Invalid client ready marker");
            report("Client "+std::to_string(slot+1)+": "+std::to_string(w)+"x"+std::to_string(h)+" virtual monitor, "+std::to_string(outW)+"x"+std::to_string(outH)+" stream"+(granted?", mouse/keyboard enabled":", view only (Accessibility permission required for input)"));
        };prepare();uint32_t sequence=0;const auto start=Clock::now();auto tick=start;uint64_t lastMicros=0;
        while(!stop){if(!displayOnline(monitor.displayID()))throw std::runtime_error("Virtual monitor removed");if(granted && !AXIsProcessTrusted())throw std::runtime_error("Accessibility permission was withdrawn");
            auto now=Clock::now();uint64_t micros=std::max(lastMicros+1,uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now-start).count()));lastMicros=micros;
            auto rect=CGDisplayBounds(monitor.displayID());if(unsigned(rect.size.width)!=w || unsigned(rect.size.height)!=h){transport.write(wire::encode_header({sequence,0,micros}));prepare();continue;}
            auto packet=media->frame(micros,cancelled);transport.write(wire::encode_header({sequence,uint32_t(packet.size()),micros}));transport.write(packet);
            if(granted){auto header=input::decode_header(transport.read(input::header_size,1));if(header.epoch!=epoch || header.sequence!=sequence)throw std::runtime_error("Invalid input epoch or sequence");auto events=input::decode_events(transport.read(header.count*input::event_size,1),header.count);
                for(const auto& event:events){if(input::capability(event.kind)&~granted)throw std::runtime_error("Ungrant​ed input family");broker.inject(event);}}
            ++sequence;tick+=std::chrono::microseconds(1000000/hz);if(tick<Clock::now())tick=Clock::now();while(!stop && Clock::now()<tick)std::this_thread::sleep_for(1ms);
        }
    }else{close(socket);throw std::runtime_error("Hosting requires macOS 13 or newer");}}}
    void run(){@autoreleasepool{std::vector<pollfd> listeners;try{
        auto local=localAddresses();std::vector<std::string> addresses;
        for(const auto& entry:local)if(address.empty() || entry.address==address)addresses.push_back(entry.address);
        if(addresses.empty())throw std::runtime_error(address.empty()?"No connected Ethernet/Wi-Fi IPv4 address is available":"Selected local address is no longer connected; refresh addresses and retry");
        std::string listening;
        for(const auto& ip:addresses){
            int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)throw std::runtime_error("Cannot create listener");
            listeners.push_back({fd,POLLIN,0});fcntl(fd,F_SETFD,FD_CLOEXEC);fcntl(fd,F_SETFL,O_NONBLOCK);
            int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));sockaddr_in bind{};
            bind.sin_family=AF_INET;bind.sin_len=sizeof(bind);bind.sin_port=htons(port);inet_pton(AF_INET,ip.c_str(),&bind.sin_addr);
            if(::bind(fd,reinterpret_cast<sockaddr*>(&bind),sizeof(bind)) || listen(fd,4))throw std::runtime_error("Cannot listen on "+ip+":"+std::to_string(port)+" (address unavailable or port in use)");
            if(!listening.empty())listening+=", ";listening+=ip+":"+std::to_string(port)+"";
        }
        // Freeze this address list for the listener lifetime; workers only read it.
        address=listening;
        report("Listening on "+address+" ? waiting for clients; no virtual monitors until connection");
        while(!stop){if(poll(listeners.data(),listeners.size(),100)<=0)continue;
            for(const auto& listener:listeners){if(stop)break;if(!(listener.revents&POLLIN))continue;
                int peer=accept(listener.fd,nullptr,nullptr);if(peer<0)continue;fcntl(peer,F_SETFD,FD_CLOEXEC);
                for(auto it=workers.begin();it!=workers.end();){if((*it)->done){(*it)->thread.join();it=workers.erase(it);}else ++it;}
                if(pending>=32){close(peer);report("Too many pending admissions; retry shortly");continue;}
                ++pending;
                try{
                    workers.push_back(std::make_unique<Worker>());auto* worker=workers.back().get();
                    try{worker->thread=std::thread([this,peer,worker]{@autoreleasepool{try{serve(peer);}catch(const std::exception& e){report(std::string("Connection ended: ")+e.what());}catch(...){report("Connection ended unexpectedly");}if(!stop)report("Listening on "+address+" ? connection ended; other sessions remain active");worker->done=true;}});}
                    catch(...){workers.pop_back();throw;}
                }catch(...){--pending;close(peer);report("Cannot create another connection worker; existing sessions continue");}
            }
        }
    }catch(const std::exception& e){report(std::string("Server error: ")+e.what());}
        for(const auto& listener:listeners)close(listener.fd);stop=true;
        for(auto& worker:workers)if(worker->thread.joinable())worker->thread.join();running=false;report("Server stopped");
    }}

};
Host::Host(std::string address,std::function<void(std::string)> report,unsigned port,std::shared_ptr<access::Gate> gate):state_(std::make_shared<State>(std::move(address),std::move(report),port,std::move(gate))){auto state=state_;state_->listener=std::thread([state]{state->run();});}
Host::~Host(){stop();if(state_->listener.joinable())state_->listener.join();}
void Host::stop(){state_->stop=true;}
bool Host::running()const{return state_->running;}
}
