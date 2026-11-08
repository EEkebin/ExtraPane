#include "host.hpp"
#include "portal.hpp"
#include "tls.hpp"
#include "resource_receiver.hpp"
#include <dm/control.hpp>
#include <extrapane/session_route.hpp>
#include <vector>
#include <dm/video_protocol.hpp>
#include <QCoreApplication>
#include <QStringList>
#include <gst/app/gstappsink.h>
#include <openssl/rand.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <utility>
#include <stdexcept>

namespace dm::linux_client {
namespace {
using Clock=std::chrono::steady_clock;
auto deadline(int seconds=10){return Clock::now()+std::chrono::seconds(seconds);}
struct Pipeline {
    GstElement* pipeline=nullptr;GstElement* sink=nullptr;
    ~Pipeline(){if(pipeline)gst_element_set_state(pipeline,GST_STATE_NULL);if(sink)gst_object_unref(sink);if(pipeline)gst_object_unref(pipeline);}
    void check(){
        auto* bus=gst_element_get_bus(pipeline);auto* message=gst_bus_pop_filtered(bus,static_cast<GstMessageType>(GST_MESSAGE_ERROR|GST_MESSAGE_EOS));gst_object_unref(bus);
        if(!message)return;
        std::string reason="Virtual display capture ended";
        if(GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR){GError* error=nullptr;gchar* details=nullptr;gst_message_parse_error(message,&error,&details);if(error){reason=error->message;g_error_free(error);}g_free(details);}
        gst_message_unref(message);throw std::runtime_error(reason);
    }
};
struct SampleRelease{void operator()(GstSample* p)const{if(p)gst_sample_unref(p);}};
}
QString Host::unavailable(bool existing,bool input){
    auto reason=Portal::unavailable(existing,input);if(!reason.isEmpty())return reason;
    for(const char* name:{"pipewiresrc","videoconvert","videoscale","videorate","openh264enc","h264parse","appsink"}){
        auto* factory=gst_element_factory_find(name);if(!factory)return "Missing hosting component: "+QString::fromUtf8(name);gst_object_unref(factory);
    }return {};
}
Host::~Host(){if(preparedListener_>=0)close(preparedListener_);}
void Host::prepareScopedListener(){
    if(isRunning()||preparedListener_>=0||scope_.kind<0||address_!="127.0.0.1"||!gate_)throw std::runtime_error("Invalid scoped listener preparation");
    const int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)throw std::runtime_error("Cannot create scoped listener");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port_);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0||listen(fd,8)<0){close(fd);throw std::runtime_error("Cannot bind scoped listener");}preparedListener_=fd;
}
void Host::run(){
    std::vector<pollfd> listeners;stopping_=false;std::atomic_uint pending=0;
    std::vector<std::unique_ptr<QThread>> workers;
    struct Join {std::vector<std::unique_ptr<QThread>>& workers;std::atomic_bool& stopping;~Join(){stopping=true;for(auto& worker:workers)worker->wait();}} join{workers,stopping_};
    try{
        if(preparedListener_>=0)listeners.push_back({std::exchange(preparedListener_,-1),POLLIN,0});
        QStringList bindings;if(listeners.empty())bindings.push_back(address_);if(listeners.empty()&&address_!="127.0.0.1")bindings.push_back("127.0.0.1");
        for(const auto& binding:bindings){
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port_);
            if(inet_pton(AF_INET,binding.toStdString().c_str(),&address.sin_addr)!=1 || address.sin_addr.s_addr==INADDR_ANY)throw std::runtime_error("Select a connected server IPv4 address");
            const auto listener=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(listener<0)throw std::runtime_error(std::strerror(errno));
            listeners.push_back({listener,POLLIN,0});
            int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
            if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0 || listen(listener,32)<0)throw std::runtime_error(std::strerror(errno));
        }
        emit state("Listening on "+address_+":"+QString::number(port_)+" — waiting for a client");
        while(!isInterruptionRequested()){
            int ready=poll(listeners.data(),listeners.size(),100);
            if(ready<0){if(errno==EINTR)continue;throw std::runtime_error(std::strerror(errno));}if(!ready)continue;
            int peer=-1;for(const auto& listener:listeners)if(listener.revents&POLLIN){peer=accept4(listener.fd,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC);if(peer>=0)break;}if(peer<0)continue;
            for(auto it=workers.begin();it!=workers.end();){if((*it)->isFinished()){(*it)->wait();it=workers.erase(it);}else ++it;}
            if(pending.load()>=32){close(peer);emit state("Too many pending admissions; retry shortly");continue;}
            ++pending;
            try{
                auto worker=std::unique_ptr<QThread>(QThread::create([this,peer,&pending]{
                    try{serve(peer,pending);}catch(const std::exception& e){emit state("Resource ended: "+QString::fromUtf8(e.what()));}
                }));
                workers.push_back(std::move(worker));workers.back()->start();
            }catch(...){--pending;close(peer);emit state("Cannot allocate another connection worker; existing sessions continue");}

        }
        emit state("Server stopped");
    }catch(const std::exception& e){emit state("Server error: "+QString::fromUtf8(e.what()));}
    for(const auto& listener:listeners)close(listener.fd);
}
void Host::serve(int socket,std::atomic_uint& pending){
    struct Admission {std::atomic_uint& count;bool held=true;void ready(){if(held){--count;held=false;}}~Admission(){ready();}} admission{pending};
    const auto cancelled=[this]{return stopping_.load()||isInterruptionRequested();};
    Tls stream(socket,cancelled,gate_.get());
    std::array<uint8_t,8> prefix{};stream.read(prefix,deadline());
    if(prefix==access::control_prefix){
        if(scope_.kind>=0)throw std::runtime_error("Scoped media endpoint does not accept control connections");
        access::ControlRequest request{};std::copy(prefix.begin(),prefix.end(),request.begin());stream.read(std::span(request).subspan(prefix.size()),deadline());
        if(access::parse_control(request).first!=access::ControlOperation::open)throw std::runtime_error("Control must open first");
        const bool protectedMode=stream.openControl();admission.ready();
        stream.write(access::control_reply(access::ControlOperation::open,0,protectedMode),deadline());
        while(!cancelled()){
            stream.read(request,deadline(15));const auto [operation,kind]=access::parse_control(request);
            if(operation==access::ControlOperation::close)return;
            if(operation==access::ControlOperation::open)throw std::runtime_error("Control already open");
            std::string credential;
            if(access::control_add(operation)){auto tickets=stream.controlChannels(kind,access::control_input(operation));if(protectedMode){credential=access::formatCredential(tickets.at(0));std::fill(tickets[0].secret.begin(),tickets[0].secret.end(),0);}}
            auto reply=access::control_reply(operation,kind,protectedMode,credential);
            struct Wipe{std::string& code;access::ControlReply& reply;~Wipe(){volatile char* c=code.data();for(size_t i=0;i<code.size();++i)c[i]=0;volatile uint8_t* p=reply.data();for(size_t i=0;i<reply.size();++i)p[i]=0;}}wipe{credential,reply};
            stream.write(reply,deadline());
        }
        return;
    }
    const auto kind=extrapane::classify_session_prefix(prefix);if(scope_.kind>=0&&static_cast<int>(kind)!=scope_.kind)throw std::runtime_error("This endpoint permits only its granted resource");stream.requireResource(static_cast<unsigned>(kind));admission.ready();
    if(kind==extrapane::ResourceKind::display)serveDisplay(stream,prefix);
    else receiveResource(stream,kind,prefix,cancelled,[this](QString text){emit state(text);});
}
void Host::serveDisplay(Tls& stream,std::span<const uint8_t> prefix){
    namespace wire=dm::video::wire;using namespace dm::input;
    auto cancelled=[this]{return stopping_.load()||isInterruptionRequested();};
    auto request=wire::request;std::copy(prefix.begin(),prefix.end(),request.begin());stream.read(std::span(request).subspan(prefix.size()),deadline());
    const auto display=wire::take_client_display(request);const auto screen=wire::take_screen_request(request);
    const bool existing=screen.mode==wire::DisplayMode::existing_screen;
    if(existing&&(!screen_.id||screen.screen_id!=screen_.id))throw std::runtime_error("Existing-screen sharing is not offered");
    if(scope_.existingOnly&&!existing)throw std::runtime_error("This grant permits only an existing screen");
    const bool inputAllowed=scope_.input&&(!existing||screen_.allowInput)&&stream.authorizedDisplayInput().value_or(true);
    if(existing&&display.exact_mode)throw std::runtime_error("Existing screens use the selected screen mode");
    const auto input=request[7],caps=request[8],scale=request[9],keyboard=request[10];request[7]=request[8]=request[9]=request[10]=0;
    if(request!=wire::request || input>1 || caps>all_caps || (bool(input)!=bool(caps)) || scale>2 || keyboard>2 || (keyboard&&(!input||!(caps&touch_cap))))throw std::runtime_error("Invalid client request");
    if(input&&!inputAllowed)throw std::runtime_error("This grant does not permit remote input");
    emit state(existing?"Client connected - choose a screen in the desktop-sharing prompt":"Client connected - approve the prompt to create a virtual monitor");
    if(display.exact_mode&&(display.hz>60||scale!=0))throw std::runtime_error("Linux portal hosting cannot provide this exact frame rate or scale; use up to 60 FPS at the portal monitor size");
    const auto unavailableReason=unavailable(existing,input!=0&&inputAllowed);if(!unavailableReason.isEmpty())throw std::runtime_error(unavailableReason.toStdString());
    Portal portal(cancelled);portal.start(input!=0&&inputAllowed,existing);
    if(display.exact_mode&&(display.width!=portal.width||display.height!=portal.height))throw std::runtime_error("The desktop portal chose a different monitor size; use its supported resolution or Match local monitor mode");
    unsigned granted=input?((portal.devices&1?keyboard_cap:0)|(portal.devices&2?mouse_cap:0)|(portal.devices&4?touch_cap:0))&caps:0;
    if(input && inputAllowed && !granted)throw std::runtime_error("The portal did not grant the requested input devices");
    // KDE's RemoteDesktop portal currently creates a 1920x1080 virtual output.
    // The portal owns its dimensions. Advertise the actual aspect ratio and
    // scale only the transmitted image; never pretend to change monitor geometry.
    const unsigned factor=scale==1?75:scale==2?50:100;
    const unsigned width=std::clamp(portal.width*factor/100/2*2,2u,4096u),height=std::clamp(portal.height*factor/100/2*2,2u,4096u);
    if(static_cast<uint64_t>(width)*height>8847360)throw std::runtime_error("Virtual display exceeds encoder limits");
    const unsigned fps=existing?60u:std::clamp(display.hz?display.hz:60,1u,60u);
    std::uint64_t epoch=0;if(granted){if(RAND_bytes(reinterpret_cast<unsigned char*>(&epoch),sizeof(epoch))!=1)throw std::runtime_error("Cannot generate input epoch");if(!epoch)epoch=1;}
    Pipeline media;GError* error=nullptr;
    const auto description=QString("pipewiresrc name=capture do-timestamp=true ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream ! videoconvert ! videoscale ! videorate drop-only=true max-rate=%3 ! video/x-raw,format=I420,width=%1,height=%2,framerate=%3/1 ! openh264enc complexity=low bitrate=12000000 gop-size=%3 rate-control=bitrate enable-frame-skip=false ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=encoded sync=false max-buffers=2 drop=false").arg(width).arg(height).arg(fps);
    media.pipeline=gst_parse_launch(description.toUtf8().constData(),&error);
    if(error || !media.pipeline){std::string reason=error?error->message:"Cannot create capture encoder";if(error)g_error_free(error);throw std::runtime_error(reason);}
    auto* source=gst_bin_get_by_name(GST_BIN(media.pipeline),"capture");
    if(!g_object_class_find_property(G_OBJECT_GET_CLASS(source),"keepalive-time")){gst_object_unref(source);throw std::runtime_error("Update the PipeWire GStreamer plugin: keepalive-time support is required");}
    g_object_set(source,"keepalive-time",static_cast<int>(std::max(1u,1000/fps)),nullptr);
    const auto node=std::to_string(portal.node);g_object_set(source,"fd",portal.pipewire.fileDescriptor(),"path",node.c_str(),nullptr);gst_object_unref(source);
    media.sink=gst_bin_get_by_name(GST_BIN(media.pipeline),"encoded");
    stream.write(wire::encode_offer({width,height,fps,0,wire::Codec::h264,static_cast<uint8_t>(granted),epoch}),deadline());
    auto ready=wire::ready;stream.read(ready,deadline(20));if(ready!=wire::ready)throw std::runtime_error("Invalid client ready marker");
    if(gst_element_set_state(media.pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE)throw std::runtime_error("Cannot start virtual display capture");
    emit state(QString("Streaming %1×%2 at up to %3 FPS — software H.264; portal monitor %4×%5").arg(width).arg(height).arg(fps).arg(portal.width).arg(portal.height));
    uint32_t sequence=0;auto lastFrame=Clock::now();const auto start=lastFrame;
    while(!cancelled()){
        QCoreApplication::processEvents();if(portal.closed())throw std::runtime_error("Desktop sharing permission was revoked");media.check();
        std::unique_ptr<GstSample,SampleRelease> sample(gst_app_sink_try_pull_sample(GST_APP_SINK(media.sink),100*GST_MSECOND));
        if(!sample){if(Clock::now()-lastFrame>std::chrono::seconds(5))throw std::runtime_error("Virtual display capture stalled");continue;}
        lastFrame=Clock::now();auto* buffer=gst_sample_get_buffer(sample.get());GstMapInfo map{};
        if(!gst_buffer_map(buffer,&map,GST_MAP_READ))throw std::runtime_error("Cannot read encoded frame");
        try{
            if(!map.size || map.size>wire::max_jpeg)throw std::runtime_error("Encoded frame exceeds protocol bounds");
            auto micros=std::chrono::duration_cast<std::chrono::microseconds>(lastFrame-start).count();
            stream.write(wire::encode_header({sequence,static_cast<uint32_t>(map.size),static_cast<uint64_t>(micros)}),deadline(2));
            stream.write({map.data,map.size},deadline(2));
        }catch(...){gst_buffer_unmap(buffer,&map);throw;}gst_buffer_unmap(buffer,&map);
        if(granted){
            HeaderBytes bytes{};stream.read(bytes,deadline(2));const auto header=decode_header(bytes);
            if(header.epoch!=epoch || header.sequence!=sequence)throw std::runtime_error("Invalid input epoch or sequence");
            std::vector<uint8_t> payload(header.count*event_size);stream.read(payload,deadline(2));
            for(const auto& event:decode_events(payload,header.count)){
                if(capability(event.kind)&~granted)throw std::runtime_error("Client sent ungranted input");
                portal.inject(event);
            }
        }
        ++sequence;
    }
}
}
