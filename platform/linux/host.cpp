#include "host.hpp"
#include "portal.hpp"
#include "tls.hpp"
#include <dm/video_protocol.hpp>
#include <QCoreApplication>
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
QString Host::unavailable(){
    auto reason=Portal::unavailable();if(!reason.isEmpty())return reason;
    for(const char* name:{"pipewiresrc","videoconvert","videoscale","videorate","x264enc","h264parse","appsink"}){
        auto* factory=gst_element_factory_find(name);if(!factory)return "Missing hosting component: "+QString::fromUtf8(name);gst_object_unref(factory);
    }return {};
}
void Host::run(){
    int listener=-1;
    try{
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(43824);
        if(inet_pton(AF_INET,address_.toStdString().c_str(),&address.sin_addr)!=1 || address.sin_addr.s_addr==INADDR_ANY)throw std::runtime_error("Select a connected server IPv4 address");
        listener=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(listener<0)throw std::runtime_error(std::strerror(errno));
        int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
        if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0 || listen(listener,4)<0)throw std::runtime_error(std::strerror(errno));
        while(!isInterruptionRequested()){
            emit state("Listening on "+address_+":43824 — waiting for a client");
            pollfd fd{listener,POLLIN,0};int ready=poll(&fd,1,100);
            if(ready<0){if(errno==EINTR)continue;throw std::runtime_error(std::strerror(errno));}if(!ready)continue;
            int peer=accept4(listener,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC);if(peer<0)continue;
            try{serve(peer);}catch(const std::exception& e){emit state("Client disconnected: "+QString::fromUtf8(e.what()));}
            // Discard queued clients: each client must explicitly retry after the
            // active session ends, rather than unexpectedly opening a monitor.
            while((peer=accept4(listener,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC))>=0)close(peer);
        }
        emit state("Server stopped");
    }catch(const std::exception& e){emit state("Server error: "+QString::fromUtf8(e.what()));}
    if(listener>=0)close(listener);
}
void Host::serve(int socket){
    namespace wire=dm::video::wire;using namespace dm::input;
    auto cancelled=[this]{return isInterruptionRequested();};
    Tls stream(socket,cancelled);auto request=wire::request;stream.read(request,deadline());
    const auto display=wire::take_client_display(request);
    const auto input=request[7],caps=request[8],scale=request[9];request[7]=request[8]=request[9]=0;
    if(request!=wire::request || input>1 || caps>all_caps || (bool(input)!=bool(caps)) || scale>2)throw std::runtime_error("Invalid client request");
    emit state("Client connected — approve KDE's desktop-sharing prompt to create a virtual monitor");
    Portal portal(cancelled);portal.start(input!=0);
    unsigned granted=input?((portal.devices&1?keyboard_cap:0)|(portal.devices&2?mouse_cap:0)|(portal.devices&4?touch_cap:0))&caps:0;
    if(input && !granted)throw std::runtime_error("The portal did not grant the requested input devices");
    // KDE's RemoteDesktop portal currently creates a 1920x1080 virtual output.
    // The portal owns its dimensions. Advertise the actual aspect ratio and
    // scale only the transmitted image; never pretend to change monitor geometry.
    const unsigned factor=scale==1?75:scale==2?50:100;
    const unsigned width=std::clamp(portal.width*factor/100/2*2,2u,4096u),height=std::clamp(portal.height*factor/100/2*2,2u,4096u);
    if(static_cast<uint64_t>(width)*height>8847360)throw std::runtime_error("Virtual display exceeds encoder limits");
    const unsigned fps=std::clamp(display.hz?display.hz:60,1u,60u);
    std::uint64_t epoch=0;if(granted){if(RAND_bytes(reinterpret_cast<unsigned char*>(&epoch),sizeof(epoch))!=1)throw std::runtime_error("Cannot generate input epoch");if(!epoch)epoch=1;}
    Pipeline media;GError* error=nullptr;
    const auto description=QString("pipewiresrc name=capture do-timestamp=true ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream ! videoconvert ! videoscale ! videorate ! video/x-raw,format=I420,width=%1,height=%2,framerate=%3/1 ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=12000 key-int-max=%3 bframes=0 threads=2 byte-stream=true ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=encoded sync=false max-buffers=2 drop=false").arg(width).arg(height).arg(fps);
    media.pipeline=gst_parse_launch(description.toUtf8().constData(),&error);
    if(error || !media.pipeline){std::string reason=error?error->message:"Cannot create capture encoder";if(error)g_error_free(error);throw std::runtime_error(reason);}
    auto* source=gst_bin_get_by_name(GST_BIN(media.pipeline),"capture");
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
