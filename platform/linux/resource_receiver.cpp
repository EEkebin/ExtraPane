#include "resource_receiver.hpp"
#include <extrapane/camera_protocol.hpp>
#include <extrapane/microphone_protocol.hpp>
#include <extrapane/negotiation.hpp>
#include <QDir>
#include <QUuid>
#include <QProcess>
#include <QThread>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <linux/videodev2.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>

namespace dm::linux_client {
namespace {
using namespace extrapane;
using Clock=std::chrono::steady_clock;
auto deadline(unsigned seconds=5){return Clock::now()+std::chrono::seconds(seconds);}
void require(bool result,const char* reason){if(!result)throw std::runtime_error(reason);}
QString missingFactories(std::initializer_list<const char*> names){
    for(const auto* name:names){auto* factory=gst_element_factory_find(name);if(!factory)return "Missing receiving component: "+QString::fromUtf8(name);gst_object_unref(factory);}return {};
}
class VideoNode final {
public:
    int fd=-1;unsigned stride=0,size=0;
    ~VideoNode(){if(fd>=0)::close(fd);}
    static bool identity(int descriptor){
        struct stat info{};if(fstat(descriptor,&info)||!S_ISCHR(info.st_mode))return false;
        v4l2_capability caps{};if(ioctl(descriptor,VIDIOC_QUERYCAP,&caps))return false;
        const auto driver=QByteArray(reinterpret_cast<char*>(caps.driver),strnlen(reinterpret_cast<char*>(caps.driver),sizeof(caps.driver)));
        const auto card=QByteArray(reinterpret_cast<char*>(caps.card),strnlen(reinterpret_cast<char*>(caps.card),sizeof(caps.card)));
        const auto capabilities=caps.capabilities&V4L2_CAP_DEVICE_CAPS?caps.device_caps:caps.capabilities;
        return (driver=="v4l2 loopback"||driver=="v4l2loopback")&&card.startsWith("ExtraPane Camera")&&(capabilities&V4L2_CAP_VIDEO_OUTPUT)&&(capabilities&V4L2_CAP_READWRITE);
    }
    void open(unsigned width,unsigned height,unsigned fps){
        // Only explicit ExtraPane loopback slots. Never write to physical cameras
        // or another application's loopback node, and never create kernel nodes.
        const auto nodes=QDir("/dev").entryList({"video*"},QDir::System|QDir::Files,QDir::Name);
        for(const auto& node:nodes){
            const auto suffix=node.mid(5);bool numeric=false;suffix.toUInt(&numeric);if(!numeric)continue;
            const auto path=("/dev/"+node).toUtf8();int candidate=::open(path.constData(),O_RDWR|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
            if(candidate<0)continue;
            if(!identity(candidate)||flock(candidate,LOCK_EX|LOCK_NB)){::close(candidate);continue;}
            fd=candidate;break;
        }
        require(fd>=0,"No writable, unused ExtraPane Camera v4l2loopback slot is available");
        require(identity(fd),"Virtual camera identity changed");
        v4l2_format format{};format.type=V4L2_BUF_TYPE_VIDEO_OUTPUT;format.fmt.pix.width=width;format.fmt.pix.height=height;
        format.fmt.pix.pixelformat=V4L2_PIX_FMT_YUYV;format.fmt.pix.field=V4L2_FIELD_NONE;
        require(ioctl(fd,VIDIOC_S_FMT,&format)==0,"Virtual camera rejected this resolution");
        require(format.fmt.pix.width==width&&format.fmt.pix.height==height&&format.fmt.pix.pixelformat==V4L2_PIX_FMT_YUYV,"Virtual camera changed the negotiated format");
        stride=format.fmt.pix.bytesperline;size=format.fmt.pix.sizeimage;
        require(stride>=width*2&&stride<=width*2+4096&&size>=static_cast<std::uint64_t>(stride)*height&&size<=32*1024*1024,"Virtual camera returned invalid buffer geometry");
        v4l2_streamparm rate{};rate.type=V4L2_BUF_TYPE_VIDEO_OUTPUT;rate.parm.output.timeperframe.numerator=1;rate.parm.output.timeperframe.denominator=fps;
        require(ioctl(fd,VIDIOC_S_PARM,&rate)==0,"Virtual camera rejected this frame rate");
        require(rate.parm.output.timeperframe.numerator!=0&&static_cast<std::uint64_t>(rate.parm.output.timeperframe.numerator)*fps==rate.parm.output.timeperframe.denominator,"Virtual camera changed the negotiated frame rate");
    }
    void write(std::span<const std::uint8_t> pixels,const std::function<bool()>& cancelled){
        const auto until=deadline();
        while(!cancelled()){
            const auto count=::write(fd,pixels.data(),pixels.size());if(count==static_cast<ssize_t>(pixels.size()))return;
            if(count>=0)throw std::runtime_error("Virtual camera accepted an incomplete frame");
            if(errno!=EAGAIN&&errno!=EINTR)throw std::runtime_error("Virtual camera output failed");
            if(Clock::now()>=until)throw std::runtime_error("Virtual camera output stalled");
            pollfd wait{fd,POLLOUT,0};poll(&wait,1,50);
        }
        throw std::runtime_error("Camera receiving stopped");
    }
};
class Endpoint final : public Resource {
public:
    Endpoint(ResourceKind kind,camera_wire::Offer offer,std::function<bool()> cancelled):kind_(kind),offer_(offer),cancelled_(std::move(cancelled)){}
    ~Endpoint(){stop();}
    Availability prepare()override{
        const auto reason=resourceReceiverUnavailable(kind_);return {reason.isEmpty(),reason.toStdString()};
    }
    Availability start()override{
        try{
            if(kind_==ResourceKind::camera)node_.open(offer_.width,offer_.height,offer_.fps);
            GError* error=nullptr;
            const auto graph=kind_==ResourceKind::camera
                ? QString("appsrc name=received is-live=true format=time block=false max-bytes=4194304 ! h264parse ! video/x-h264,width=%1,height=%2 ! openh264dec ! videoconvert ! video/x-raw,format=YUY2,width=%1,height=%2 ! appsink name=decoded sync=false max-buffers=3 drop=false").arg(offer_.width).arg(offer_.height)
                : "appsrc name=received is-live=true format=time block=false max-bytes=9600 leaky-type=downstream ! queue max-size-buffers=0 max-size-bytes=0 max-size-time=100000000 leaky=downstream ! pipewiresink name=endpoint mode=provide sync=false async=false";
            pipeline_=gst_parse_launch(graph.toUtf8().constData(),&error);
            if(error||!pipeline_){const std::string reason=error?error->message:"Cannot create resource endpoint";if(error)g_error_free(error);throw std::runtime_error(reason);}
            source_=gst_bin_get_by_name(GST_BIN(pipeline_),"received");require(source_,"Receiving media input unavailable");
            GstCaps* caps=kind_==ResourceKind::camera
                ? gst_caps_new_simple("video/x-h264","stream-format",G_TYPE_STRING,"byte-stream","alignment",G_TYPE_STRING,"au","width",G_TYPE_INT,static_cast<int>(offer_.width),"height",G_TYPE_INT,static_cast<int>(offer_.height),nullptr)
                : gst_caps_new_simple("audio/x-raw","format",G_TYPE_STRING,"S16LE","layout",G_TYPE_STRING,"interleaved","rate",G_TYPE_INT,48000,"channels",G_TYPE_INT,1,nullptr);
            gst_app_src_set_caps(GST_APP_SRC(source_),caps);gst_caps_unref(caps);
            if(kind_==ResourceKind::camera)sink_=gst_bin_get_by_name(GST_BIN(pipeline_),"decoded");
            else{
                sink_=gst_bin_get_by_name(GST_BIN(pipeline_),"endpoint");require(sink_,"PipeWire microphone endpoint unavailable");
                const auto token=QUuid::createUuid().toString(QUuid::Id128);nodeName_="extrapane-microphone-"+token;const auto name=nodeName_.toUtf8();
                const auto label=("ExtraPane Microphone "+token.left(6)).toUtf8();
                auto* properties=gst_structure_new("properties","media.class",G_TYPE_STRING,"Audio/Source","node.name",G_TYPE_STRING,name.constData(),"node.description",G_TYPE_STRING,label.constData(),"node.virtual",G_TYPE_STRING,"true","object.linger",G_TYPE_STRING,"false",nullptr);
                g_object_set(sink_,"stream-properties",properties,nullptr);gst_structure_free(properties);
            }
            require(gst_element_set_state(pipeline_,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE,"Receiving endpoint could not start");check();
            if(kind_==ResourceKind::microphone){
                std::array<std::uint8_t,960> silence{};push(silence,0,10*GST_MSECOND);verifyMicrophone();
            }
            return {true,{}};
        }catch(const std::exception& error){stop();return {false,error.what()};}
    }
    void stop()noexcept override{
        if(pipeline_)gst_element_set_state(pipeline_,GST_STATE_NULL);
        if(source_)gst_object_unref(source_);source_=nullptr;
        if(sink_)gst_object_unref(sink_);sink_=nullptr;
        if(pipeline_)gst_object_unref(pipeline_);pipeline_=nullptr;
        if(node_.fd>=0)::close(node_.fd);node_.fd=-1;
    }
    void check(){
        auto* bus=gst_element_get_bus(pipeline_);auto* message=gst_bus_pop_filtered(bus,static_cast<GstMessageType>(GST_MESSAGE_ERROR|GST_MESSAGE_EOS));gst_object_unref(bus);
        if(!message)return;std::string reason="Receiving endpoint ended";
        if(GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR){GError* error=nullptr;gchar* detail=nullptr;gst_message_parse_error(message,&error,&detail);if(error){reason=error->message;g_error_free(error);}g_free(detail);}
        gst_message_unref(message);throw std::runtime_error(reason);
    }
    void push(std::span<const std::uint8_t> bytes,std::uint64_t timestamp,std::uint64_t duration){
        check();guint64 queued=0;g_object_get(source_,"current-level-bytes",&queued,nullptr);
        require(queued<=(kind_==ResourceKind::camera?media::max_frame_bytes*2:48000),"Receiving endpoint is not consuming data");
        auto* buffer=gst_buffer_new_allocate(nullptr,bytes.size(),nullptr);require(buffer,"Cannot allocate received media buffer");
        gst_buffer_fill(buffer,0,bytes.data(),bytes.size());GST_BUFFER_PTS(buffer)=timestamp;GST_BUFFER_DURATION(buffer)=duration;
        require(gst_app_src_push_buffer(GST_APP_SRC(source_),buffer)==GST_FLOW_OK,"Receiving endpoint closed");
    }
    void camera(const media::Frame& frame){
        require(frame.timestamp>=0&&static_cast<std::uint64_t>(frame.timestamp)<=std::numeric_limits<std::uint64_t>::max()/100,"Invalid camera timestamp");
        push(frame.payload,static_cast<std::uint64_t>(frame.timestamp)*100,GST_SECOND/offer_.fps);
        while(auto* sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink_),10*GST_MSECOND)){
            GstVideoInfo info{};GstVideoFrame mapped{};auto* buffer=gst_sample_get_buffer(sample);
            if(!gst_video_info_from_caps(&info,gst_sample_get_caps(sample))||GST_VIDEO_INFO_WIDTH(&info)!=offer_.width||GST_VIDEO_INFO_HEIGHT(&info)!=offer_.height||GST_VIDEO_INFO_FORMAT(&info)!=GST_VIDEO_FORMAT_YUY2||!gst_video_frame_map(&mapped,&info,buffer,GST_MAP_READ)){
                gst_sample_unref(sample);throw std::runtime_error("Camera decoded an unexpected format");
            }
            try{
                const auto sourceStride=GST_VIDEO_FRAME_PLANE_STRIDE(&mapped,0);require(sourceStride>=static_cast<int>(offer_.width*2),"Invalid decoded camera stride");
                std::vector<std::uint8_t> output(node_.size,0);const auto* pixels=static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&mapped,0));
                for(unsigned y=0;y<offer_.height;++y)std::memcpy(output.data()+y*node_.stride,pixels+y*sourceStride,offer_.width*2);
                node_.write(output,cancelled_);
            }catch(...){gst_video_frame_unmap(&mapped);gst_sample_unref(sample);throw;}
            gst_video_frame_unmap(&mapped);gst_sample_unref(sample);
        }
    }
    void microphone(microphone_wire::Frame frame){
        if(frame.muted)frame.pcm.fill(0);
        require(frame.position<=gst_util_uint64_scale(std::numeric_limits<std::uint64_t>::max()-GST_SECOND,48000,GST_SECOND),"Microphone position overflow");
        push(frame.pcm,gst_util_uint64_scale(frame.position,GST_SECOND,48000),10*GST_MSECOND);
    }
private:
    void verifyMicrophone(){
        const auto executable=QStandardPaths::findExecutable("pw-dump");require(!executable.isEmpty(),"pw-dump is required to verify the virtual microphone");
        const auto until=deadline();
        while(!cancelled_()&&Clock::now()<until){
            check();QProcess process;process.start(executable,{});
            require(process.waitForStarted(1000),"Cannot inspect PipeWire virtual microphone");QByteArray output;
            while(process.state()!=QProcess::NotRunning&&!cancelled_()&&Clock::now()<until){
                process.waitForReadyRead(50);output+=process.readAllStandardOutput();
                if(process.readAllStandardError().size()>1024*1024){process.kill();process.waitForFinished(1000);throw std::runtime_error("PipeWire diagnostic output exceeded its limit");}
                if(output.size()>4*1024*1024){process.kill();process.waitForFinished(1000);throw std::runtime_error("PipeWire inventory exceeded its limit");}
            }
            if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(1000);break;}
            output+=process.readAllStandardOutput();
            if(output.size()>4*1024*1024)throw std::runtime_error("PipeWire inventory exceeded its limit");
            const auto document=QJsonDocument::fromJson(output);
            if(process.exitCode()==0&&document.isArray())for(const auto& item:document.array()){
                const auto object=item.toObject();const auto properties=object["info"].toObject()["props"].toObject();
                if(object["type"].toString()=="PipeWire:Interface:Node"&&properties["node.name"].toString()==nodeName_&&properties["media.class"].toString()=="Audio/Source")return;
            }
            QThread::msleep(50);
        }
        throw std::runtime_error("PipeWire did not publish the virtual microphone source");
    }
    QString nodeName_;
    ResourceKind kind_;camera_wire::Offer offer_;std::function<bool()> cancelled_;
    VideoNode node_;GstElement* pipeline_=nullptr;GstElement* source_=nullptr;GstElement* sink_=nullptr;
};
class Admitted final : public Listener {public:Availability start()override{return {true,{}};}void stop()noexcept override{}};
}
QString resourceReceiverUnavailable(extrapane::ResourceKind kind){
    if(kind==ResourceKind::camera)return missingFactories({"appsrc","h264parse","openh264dec","videoconvert","appsink"});
    if(kind==ResourceKind::microphone){if(QStandardPaths::findExecutable("pw-dump").isEmpty())return "PipeWire tools (pw-dump) are required for virtual microphones";return missingFactories({"appsrc","queue","pipewiresink"});}
    return "Linux USB receiving requires a privileged USB/IP backend and is not available in this build";
}
void receiveResource(Tls& stream,extrapane::ResourceKind kind,std::span<const std::uint8_t> prefix,const std::function<bool()>& cancelled,const std::function<void(QString)>& report){
    require(kind==ResourceKind::camera||kind==ResourceKind::microphone,"Unsupported Linux receiving resource");
    require(prefix.size()<=32,"Invalid resource preamble");
    std::array<std::uint8_t,32> bytes{};std::copy(prefix.begin(),prefix.end(),bytes.begin());stream.read(std::span(bytes).subspan(prefix.size()),deadline());
    camera_wire::Offer offer;
    if(kind==ResourceKind::camera)offer=camera_wire::decode(bytes);else offer.binding=microphone_wire::decode_offer(bytes);
    auto read=[&](auto data){if(cancelled())throw std::runtime_error("Server stopped");stream.read(data,deadline(10));};
    auto write=[&](auto data){if(cancelled())throw std::runtime_error("Server stopped");stream.write(data,deadline());};
    wire::ControlChannel control(read,write);Server server(1);require(server.start(std::make_unique<Admitted>()).available&&server.admit(1).available,"Cannot create admitted resource session");
    auto caps=unavailable_capabilities();const auto reason=resourceReceiverUnavailable(kind);caps[kind==ResourceKind::camera?1:2].destination={reason.isEmpty(),reason.toStdString()};Endpoint* endpoint=nullptr;
    Negotiation negotiation(server,1,caps,[&](ResourceKind requested)->std::unique_ptr<Resource>{require(requested==kind,"Resource does not match offer");auto resource=std::make_unique<Endpoint>(kind,offer,cancelled);endpoint=resource.get();return resource;});
    negotiation.serve_one(control);auto request=control.receive();require(request.header.operation==wire::Operation::request&&request.header.kind==kind&&request.header.resource_id==offer.binding.resource,"Resource request does not match offer");
    const auto response=negotiation.handle(request);control.send(response);
    if(response.header.operation!=wire::Operation::grant||!endpoint){for(const auto& state:server.snapshot(1))if(!state.detail.empty())report(QString::fromStdString(state.detail));throw std::runtime_error("Receiving endpoint could not start");}
    media::Channel camera(offer.binding,read,write);microphone_wire::Channel microphone(offer.binding,read,write);bool announced=false;
    while(!cancelled()){
        if(kind==ResourceKind::camera)endpoint->camera(camera.receive());else endpoint->microphone(microphone.receive());
        if(!announced){announced=true;report(kind==ResourceKind::camera?"Receiving camera into an ExtraPane virtual camera":"Receiving microphone into an ExtraPane PipeWire source");}
    }
}
}
