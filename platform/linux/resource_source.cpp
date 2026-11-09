#include "resource_source.hpp"
#include "resource_pcm.hpp"
#include "resource_fanout.hpp"
#include "shared_capture_registry.hpp"
#include "tls.hpp"
#include <extrapane/camera_protocol.hpp>
#include <extrapane/capabilities.hpp>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <gst/app/gstappsink.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <stdexcept>
#include <mutex>
#include <map>
#include <thread>

namespace dm::linux_client {
namespace {
using Kind=ResourceSource::Kind;
using Clock=std::chrono::steady_clock;
using namespace extrapane;
auto deadline(unsigned seconds=5){return Clock::now()+std::chrono::seconds(seconds);}
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct DeviceRelease {void operator()(GstDevice* value)const{gst_object_unref(value);}};
using Device=std::unique_ptr<GstDevice,DeviceRelease>;
struct CapsRelease {void operator()(GstCaps* value)const{gst_caps_unref(value);}};
using Caps=std::unique_ptr<GstCaps,CapsRelease>;
struct SampleRelease {void operator()(GstSample* value)const{gst_sample_unref(value);}};
using Sample=std::unique_ptr<GstSample,SampleRelease>;
QString deviceId(GstDevice* device){
    gchar* display=gst_device_get_display_name(device);gchar* type=gst_device_get_device_class(device);
    GstStructure* properties=gst_device_get_properties(device);gchar* encoded=properties?gst_structure_to_string(properties):nullptr;
    const QByteArray identity=QByteArray(type?type:"")+"\n"+QByteArray(display?display:"")+"\n"+QByteArray(encoded?encoded:"");
    g_free(display);g_free(type);g_free(encoded);if(properties)gst_structure_free(properties);
    return QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex());
}
struct DeviceList {
    GstDeviceMonitor* monitor=nullptr;std::vector<Device> values;
    explicit DeviceList(Kind kind){
        monitor=gst_device_monitor_new();require(monitor,"Cannot create device inventory");
        Caps caps(gst_caps_from_string(kind==Kind::Camera?"video/x-raw":"audio/x-raw"));
        gst_device_monitor_add_filter(monitor,kind==Kind::Camera?"Video/Source":"Audio/Source",caps.get());
        if(!gst_device_monitor_start(monitor)){gst_object_unref(monitor);monitor=nullptr;throw std::runtime_error("No GStreamer capture device provider is available");}
        auto* devices=gst_device_monitor_get_devices(monitor);
        for(auto* node=devices;node;node=node->next)values.emplace_back(GST_DEVICE(node->data));
        g_list_free(devices);
    }
    ~DeviceList(){values.clear();if(monitor){gst_device_monitor_stop(monitor);gst_object_unref(monitor);}}
};
struct Mode {unsigned width=0,height=0,fps=0;};
Mode cameraMode(GstDevice* device){
    Caps source(gst_device_get_caps(device));
    if(!source||gst_caps_is_empty(source.get())||gst_caps_is_any(source.get()))return {};
    Caps bound(gst_caps_from_string("video/x-raw,width=(int)[2,3840],height=(int)[2,2160],framerate=(fraction)[1/1,60/1]"));
    Caps supported(gst_caps_intersect(source.get(),bound.get()));Mode best;
    for(guint i=0;i<gst_caps_get_size(supported.get());++i){
        Caps one(gst_caps_copy_nth(supported.get(),i));auto* shape=gst_caps_get_structure(one.get(),0);
        gst_structure_fixate_field_nearest_int(shape,"width",1920);gst_structure_fixate_field_nearest_int(shape,"height",1080);gst_structure_fixate_field_nearest_fraction(shape,"framerate",30,1);
        one.reset(gst_caps_fixate(one.release()));shape=gst_caps_get_structure(one.get(),0);
        int width=0,height=0,numerator=0,denominator=0;
        if(!gst_structure_get_int(shape,"width",&width)||!gst_structure_get_int(shape,"height",&height)||!gst_structure_get_fraction(shape,"framerate",&numerator,&denominator)||denominator<=0)continue;
        const auto fps=numerator/denominator;
        if(width<2||height<2||width%2||height%2||fps<1||fps>60)continue;
        if(static_cast<unsigned>(width*height)>best.width*best.height)best={static_cast<unsigned>(width),static_cast<unsigned>(height),static_cast<unsigned>(fps)};
    }
    return best;
}
struct SourceLease {
    int fd=-1;
    explicit SourceLease(const QString& id){
        const auto base=QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        require(!base.isEmpty(),"A private XDG_RUNTIME_DIR is required for capture ownership");
        const auto path=QDir(base).filePath("extrapane-source-"+id+".lock").toUtf8();
        fd=open(path.constData(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        struct stat info{};
        if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_uid!=getuid()||info.st_nlink!=1||(info.st_mode&0077)||flock(fd,LOCK_EX|LOCK_NB)){
            if(fd>=0)close(fd);fd=-1;throw std::runtime_error("This source is already shared or its ownership lock is unavailable");
        }
    }
    ~SourceLease(){if(fd>=0)close(fd);}
};
struct Pipeline {
    GstElement* pipeline=nullptr;GstElement* sink=nullptr;GstElement* flip=nullptr;
    ~Pipeline(){if(pipeline)gst_element_set_state(pipeline,GST_STATE_NULL);if(flip)gst_object_unref(flip);if(sink)gst_object_unref(sink);if(pipeline)gst_object_unref(pipeline);}
    void create(GstDevice* device,Kind kind,Mode mode){
        pipeline=gst_pipeline_new(nullptr);require(pipeline,"Cannot allocate capture pipeline");
        auto* source=gst_device_create_element(device,"source");require(source,"Selected source cannot be opened by its provider");
        gst_bin_add(GST_BIN(pipeline),source);
        const auto graph=kind==Kind::Camera
            ? QString("videoconvert ! videoscale ! videorate drop-only=true ! video/x-raw,format=I420,width=%1,height=%2,framerate=%3/1 ! videoflip name=mirror method=none ! openh264enc complexity=low bitrate=8000000 gop-size=%3 rate-control=bitrate enable-frame-skip=false ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=frames sync=false max-buffers=3 drop=false").arg(mode.width).arg(mode.height).arg(mode.fps)
            : QString("audioconvert ! audioresample ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=1 ! appsink name=frames sync=false max-buffers=8 drop=false");
        GError* error=nullptr;auto* tail=gst_parse_bin_from_description(graph.toUtf8().constData(),TRUE,&error);
        if(error||!tail){const std::string reason=error?error->message:"Cannot construct capture conversion pipeline";if(error)g_error_free(error);if(tail)gst_object_unref(tail);throw std::runtime_error(reason);}
        gst_bin_add(GST_BIN(pipeline),tail);require(gst_element_link(source,tail),"Selected source does not support the required capture format");
        sink=gst_bin_get_by_name(GST_BIN(tail),"frames");require(sink,"Capture output is missing");
        if(kind==Kind::Camera)flip=gst_bin_get_by_name(GST_BIN(tail),"mirror");
    }
    void start(){require(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE,"Capture permission denied or selected source cannot start");}
    void check(){
        auto* bus=gst_element_get_bus(pipeline);auto* message=gst_bus_pop_filtered(bus,static_cast<GstMessageType>(GST_MESSAGE_ERROR|GST_MESSAGE_EOS));gst_object_unref(bus);
        if(!message)return;std::string reason="Capture source ended";
        if(GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR){GError* error=nullptr;gchar* detail=nullptr;gst_message_parse_error(message,&error,&detail);if(error){reason=error->message;g_error_free(error);}g_free(detail);}
        gst_message_unref(message);throw std::runtime_error(reason);
    }
};
class CaptureHub {
public:
    CaptureHub(GstDevice* device,QString id,Kind kind,Mode mode):device_(GST_DEVICE(gst_object_ref(device))),id_(std::move(id)),kind_(kind),mode_(mode){}
    ~CaptureHub(){stopping_=true;if(worker_.joinable())worker_.join();}
    void start(){worker_=std::thread([this]{run();});}
    void validateMode(Mode mode)const{require(mode.width==mode_.width&&mode.height==mode_.height&&mode.fps==mode_.fps,"This camera is already shared with a different mode");}
    std::shared_ptr<CaptureQueue> subscribe(Mode mode){
        std::lock_guard lock(mutex_);
        validateMode(mode);
        auto queue=std::make_shared<CaptureQueue>(kind_==Kind::Camera);
        if(!error_.empty())queue->fail(error_);
        queues_.push_back(queue);return queue;
    }
    std::shared_ptr<const CapturePacket> pop(const std::shared_ptr<CaptureQueue>& queue){
        std::lock_guard lock(mutex_);if(!queue->error.empty())throw std::runtime_error(queue->error);return queue->pop();
    }
    std::atomic_bool mirror=false;
private:
    Device device_;QString id_;Kind kind_;Mode mode_;std::atomic_bool stopping_=false;
    std::mutex mutex_;std::vector<std::weak_ptr<CaptureQueue>> queues_;std::string error_;std::thread worker_;
    void run(){
        try{
            SourceLease lease(id_);Pipeline capture;capture.create(device_.get(),kind_,mode_);bool lastMirror=mirror.load();
            if(capture.flip)g_object_set(capture.flip,"method",lastMirror?4:0,nullptr);
            capture.start();auto lastSample=Clock::now();
            while(!stopping_){
                capture.check();const bool nextMirror=mirror.load();if(capture.flip&&lastMirror!=nextMirror){lastMirror=nextMirror;g_object_set(capture.flip,"method",lastMirror?4:0,nullptr);}
                Sample sample(gst_app_sink_try_pull_sample(GST_APP_SINK(capture.sink),100*GST_MSECOND));
                if(!sample){require(Clock::now()-lastSample<=std::chrono::seconds(10),"Capture source stalled or permission was withdrawn");continue;}
                lastSample=Clock::now();auto* buffer=gst_sample_get_buffer(sample.get());GstMapInfo map{};
                require(buffer&&gst_buffer_map(buffer,&map,GST_MAP_READ),"Cannot read captured data");
                auto packet=std::make_shared<CapturePacket>();
                try{
                    require(map.size>0&&map.size<=media::max_frame_bytes,"Captured packet exceeds protocol bounds");
                    if(kind_==Kind::Camera)require(GST_BUFFER_PTS_IS_VALID(buffer),"Camera sample has no capture timestamp");
                    packet->bytes.assign(map.data,map.data+map.size);
                    packet->timestamp=GST_BUFFER_PTS_IS_VALID(buffer)?static_cast<std::int64_t>(GST_BUFFER_PTS(buffer)/100):0;
                    packet->keyframe=kind_==Kind::Camera?independentlyDecodable(packet->bytes):true;
                    packet->discontinuity=GST_BUFFER_FLAG_IS_SET(buffer,GST_BUFFER_FLAG_DISCONT);
                }catch(...){gst_buffer_unmap(buffer,&map);throw;}
                gst_buffer_unmap(buffer,&map);
                std::lock_guard lock(mutex_);
                for(auto it=queues_.begin();it!=queues_.end();)if(auto queue=it->lock()){queue->push(packet);++it;}else it=queues_.erase(it);
            }
        }catch(const std::exception& error){std::lock_guard lock(mutex_);error_=error.what();for(auto& weak:queues_)if(auto queue=weak.lock())queue->fail(error_);}
    }
};
SharedCaptureRegistry<QString,CaptureHub> captureRegistry;
media::Binding randomBinding(){media::Binding result;do{require(RAND_bytes(reinterpret_cast<unsigned char*>(&result),sizeof(result))==1,"Resource randomness unavailable");}while(!result.resource||!result.generation);return result;}
void negotiate(Tls& stream,ResourceKind kind,media::Binding binding){
    wire::ControlChannel control([&](auto bytes){stream.read(bytes,deadline(kind==ResourceKind::microphone?45:15));},[&](auto bytes){stream.write(bytes,deadline());});
    auto local=unavailable_capabilities();local[kind==ResourceKind::camera?1:2].source={true,{}};
    auto bytes=wire::encode_capabilities(local);control.send({{wire::Operation::capabilities,ResourceKind::display,0,static_cast<std::uint32_t>(bytes.size())},std::move(bytes)});
    const auto reply=control.receive();require(reply.header.operation==wire::Operation::capabilities&&reply.header.kind==ResourceKind::display&&!reply.header.resource_id,"Invalid server capability response");
    const auto available=can_connect(kind,local,wire::decode_capabilities(reply.payload));if(!available.available)throw std::runtime_error(available.reason);
    bytes=wire::encode_request({binding.resource,kind,kind==ResourceKind::camera?"camera":"microphone"});
    control.send({{wire::Operation::request,kind,binding.resource,static_cast<std::uint32_t>(bytes.size())},std::move(bytes)});
    const auto grant=control.receive();require(grant.header.operation==wire::Operation::grant&&grant.header.kind==kind&&grant.header.resource_id==binding.resource&&grant.payload.empty(),"Server rejected the resource; check its receiving component");
}
}
ResourceSource::ResourceSource(Options options,QObject* parent):QThread(parent),options_(std::move(options)){}
ResourceSource::~ResourceSource(){requestInterruption();wait();options_.code.fill(QChar(0));options_.code.clear();}
QString ResourceSource::unavailable(Kind kind){
    const std::vector<const char*> names=kind==Kind::Camera
        ? std::vector<const char*>{"videoconvert","videoscale","videorate","videoflip","openh264enc","h264parse","appsink"}
        : std::vector<const char*>{"audioconvert","audioresample","appsink"};
    for(const auto* name:names){auto* factory=gst_element_factory_find(name);if(!factory)return "Missing capture component: "+QString::fromUtf8(name);gst_object_unref(factory);}return {};
}
QVariantList ResourceSource::devices(Kind kind){
    QVariantList result;const auto reason=unavailable(kind);DeviceList devices(kind);std::set<QString> seen;
    for(const auto& device:devices.values){
        const auto id=deviceId(device.get());if(!seen.insert(id).second)continue;
        gchar* label=gst_device_get_display_name(device.get());const auto name=QString::fromUtf8(label?label:"Capture device");g_free(label);
        const auto mode=kind==Kind::Camera?cameraMode(device.get()):Mode{};
        const auto unavailableReason=!reason.isEmpty()?reason:(kind==Kind::Camera&&!mode.width?QString("No compatible raw camera mode (up to 3840 x 2160 at 60 FPS)"):QString{});
        result.push_back(QVariantMap{{"id",id},{"label",name},{"available",unavailableReason.isEmpty()},{"reason",unavailableReason},{"width",mode.width},{"height",mode.height},{"fps",mode.fps}});
    }
    return result;
}
void ResourceSource::run(){
    try{
        require(options_.port>0&&options_.port<=65535,"Invalid resource server port");
        const auto missing=unavailable(options_.kind);if(!missing.isEmpty())throw std::runtime_error(missing.toStdString());
        DeviceList devices(options_.kind);GstDevice* selected=nullptr;
        for(const auto& device:devices.values)if(deviceId(device.get())==options_.device){require(!selected,"Selected device identity is ambiguous");selected=device.get();}
        require(selected,"Selected source is no longer available; reopen the source list");
        Mode mode=options_.kind==Kind::Camera?cameraMode(selected):Mode{};
        if(options_.kind==Kind::Camera){
            require(mode.width>0,"No compatible raw camera mode");
            if(options_.width||options_.height||options_.fps){require(options_.width&&options_.height&&options_.fps,"Incomplete camera mode");mode={options_.width,options_.height,options_.fps};}
        }
        const auto binding=randomBinding();
        // Encode validates the entire camera mode before network/capture begins.
        auto offer=options_.kind==Kind::Camera?camera_wire::encode({mode.width,mode.height,mode.fps,binding}):microphone_wire::encode_offer(binding);
        emit state("Connecting...");
        auto code=options_.code.toStdString();options_.code.fill(QChar(0));options_.code.clear();
        struct Wipe{std::string& value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}}wipe{code};
        Tls stream(options_.server.toStdString(),options_.local.toStdString(),static_cast<std::uint16_t>(options_.port),[this]{return isInterruptionRequested();},options_.interfaceIndex,code);
        OPENSSL_cleanse(code.data(),code.size());code.clear();stream.write(offer,deadline());
        negotiate(stream,options_.kind==Kind::Camera?ResourceKind::camera:ResourceKind::microphone,binding);
        const bool initialMirror=mirrored_.load();
        auto capture=captureRegistry.acquire(QString::number(static_cast<int>(options_.kind))+":"+options_.device,[&]{
            auto hub=std::make_shared<CaptureHub>(selected,options_.device,options_.kind,mode);hub->mirror=initialMirror;hub->start();return hub;
        },[&](CaptureHub& hub){hub.validateMode(mode);});
        auto subscription=capture->subscribe(mode);
        bool announced=false;auto announce=[&]{if(!announced){announced=true;emit connected(mode.width,mode.height,mode.fps);emit state("Connected");}};
        auto read=[&](auto bytes){stream.read(bytes,deadline());};auto write=[&](auto bytes){stream.write(bytes,deadline());};
        media::Channel camera(binding,read,write);microphone_wire::Channel microphone(binding,read,write);
        PcmPacketizer packetizer(binding,[&](const auto& frame){microphone.send(frame);announce();});
        std::uint64_t sequence=0;const auto firstPacketDeadline=deadline(10);bool lastMirror=initialMirror;
        while(!isInterruptionRequested()){
            const bool desiredMirror=mirrored_.load();if(lastMirror!=desiredMirror){capture->mirror=desiredMirror;lastMirror=desiredMirror;}
            auto packet=capture->pop(subscription);
            if(!packet){require(announced||Clock::now()<firstPacketDeadline,"Capture did not provide a decodable starting frame");QThread::msleep(10);continue;}
            if(options_.kind==Kind::Camera){camera.send({binding,sequence++,packet->timestamp,packet->bytes});announce();}
            else packetizer.push(packet->bytes,muted_.load(),packet->discontinuity);
        }
        emit state("Disconnected");
    }catch(const std::exception& error){if(isInterruptionRequested())emit state("Disconnected");else emit failed(QString::fromUtf8(error.what()));}
    options_.code.fill(QChar(0));options_.code.clear();
}
}
