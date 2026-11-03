#include "media.hpp"
#include <QPointer>
#include <QQuickWindow>
#include <QRunnable>
#include <QTimer>
#include <gst/app/gstappsrc.h>
#include <chrono>
#include <stdexcept>

namespace dm::linux_client {
struct Media::RenderState {
    std::mutex mutex;
    bool active=true;
    GstElement* pipeline;
    GstElement* sink;
    RenderState(GstElement* p,GstElement* s):pipeline(GST_ELEMENT(gst_object_ref(p))),sink(GST_ELEMENT(gst_object_ref(s))){}
    ~RenderState(){gst_object_unref(sink);gst_object_unref(pipeline);}
};
namespace {
class RenderJob final:public QRunnable {
    std::function<void()> work_;
public:
    explicit RenderJob(std::function<void()> work):work_(std::move(work)){}
    void run() override{work_();}
};
}
Media::Media(QObject* parent):QObject(parent){
    auto* timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&Media::inspectBus);timer->start(25);
}
Media::~Media(){stop();}
void Media::stop(){
    if(render_){std::lock_guard lock(render_->mutex);render_->active=false;gst_element_set_state(render_->pipeline,GST_STATE_NULL);}
    render_.reset();
    std::lock_guard lock(mutex_);
    if(bus_){gst_object_unref(bus_);bus_=nullptr;}
    if(source_){gst_object_unref(source_);source_=nullptr;}
    if(pipeline_){gst_object_unref(pipeline_);pipeline_=nullptr;}
    readySerial_=0;eos_=false;failure_.clear();changed_.notify_all();
}
void Media::prepare(QQuickItem* item,unsigned width,unsigned height,unsigned serial){
    stop();
    {std::lock_guard lock(mutex_);serial_=serial;failure_.clear();}
    auto reject=[&](const QString& reason){
        {std::lock_guard lock(mutex_);failure_=reason;changed_.notify_all();}emit failed(reason);
    };
    if(!item || !item->window()){reject("Video surface is not ready");return;}
    GError* error=nullptr;
    pipeline_=gst_parse_launch("appsrc name=network is-live=true format=time block=false max-buffers=2 max-bytes=4194304 "
        "caps=video/x-h264,stream-format=byte-stream,alignment=au ! h264parse ! decodebin name=decoder "
        "! capsfilter name=geometry ! identity name=decoded silent=true ! glupload ! glcolorconvert ! qml6glsink name=screen sync=false enable-last-sample=false",&error);
    if(error || !pipeline_){const QString reason=error?QString::fromUtf8(error->message):"Cannot create video pipeline";
        if(error)g_error_free(error);
        stop();reject(reason);return;}
    source_=gst_bin_get_by_name(GST_BIN(pipeline_),"network");
    auto* sink=gst_bin_get_by_name(GST_BIN(pipeline_),"screen");
    auto* decoder=gst_bin_get_by_name(GST_BIN(pipeline_),"decoder");
    auto* geometry=gst_bin_get_by_name(GST_BIN(pipeline_),"geometry");
    if(!source_ || !sink || !decoder || !geometry){if(sink)gst_object_unref(sink);if(decoder)gst_object_unref(decoder);if(geometry)gst_object_unref(geometry);stop();reject("Incomplete video pipeline");return;}
    auto* caps=gst_caps_new_simple("video/x-raw","width",G_TYPE_INT,static_cast<int>(width),"height",G_TYPE_INT,static_cast<int>(height),nullptr);
    // Preserve hardware-memory features while enforcing the negotiated geometry.
    gst_caps_set_features(caps,0,gst_caps_features_new_any());
    g_object_set(geometry,"caps",caps,nullptr);gst_caps_unref(caps);gst_object_unref(geometry);
    g_signal_connect(decoder,"element-added",G_CALLBACK(+[](GstBin*,GstElement* element,gpointer data){
        auto* self=static_cast<Media*>(data);
        auto* factory=gst_element_get_factory(element);
        if(!factory)return;
        const char* type=gst_element_factory_get_metadata(factory,GST_ELEMENT_METADATA_KLASS);
        if(type && QString::fromUtf8(type).contains("Decoder")){
            const QString name=QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));
            QMetaObject::invokeMethod(self,[self,name]{emit self->decoderChanged(name);},Qt::QueuedConnection);
        }
    }),this);
    gst_object_unref(decoder);
    auto* counter=gst_bin_get_by_name(GST_BIN(pipeline_),"decoded");
    if(!counter){gst_object_unref(sink);stop();reject("Missing decoded-frame counter");return;}
    auto* pad=gst_element_get_static_pad(counter,"src");
    gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,+[](GstPad*,GstPadProbeInfo*,gpointer data){
        static_cast<Media*>(data)->decoded_.fetch_add(1,std::memory_order_relaxed);return GST_PAD_PROBE_OK;
    },this,nullptr);
    gst_object_unref(pad);gst_object_unref(counter);
    g_object_set(sink,"widget",item,nullptr);
    bus_=gst_element_get_bus(pipeline_);
    render_=std::make_shared<RenderState>(pipeline_,sink);gst_object_unref(sink);
    const auto state=render_;QPointer<Media> self(this);
    item->window()->scheduleRenderJob(new RenderJob([state,self,serial]{
        bool good=false;
        {std::lock_guard lock(state->mutex);if(!state->active)return;
            // Establish Qt's GstGLDisplay before glupload or the decoder creates another one.
            good=gst_element_set_state(state->sink,GST_STATE_READY)!=GST_STATE_CHANGE_FAILURE;
            if(good)good=gst_element_set_state(state->pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE;}
        if(self)QMetaObject::invokeMethod(self,[self,state,serial,good]{
            if(!self || self->render_!=state)return;
            {std::lock_guard lock(self->mutex_);if(self->serial_!=serial)return;
                if(good)self->readySerial_=serial;else self->failure_="GStreamer could not initialize the Qt OpenGL surface";
                self->changed_.notify_all();}
            if(!good)emit self->failed("GStreamer could not initialize the Qt OpenGL surface");
        },Qt::QueuedConnection);
    }),QQuickWindow::BeforeSynchronizingStage);
    item->window()->update();
}
GstElement* Media::waitSource(unsigned serial,const std::function<bool()>& cancelled){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    std::unique_lock lock(mutex_);
    for(;;){
        if(cancelled())throw std::runtime_error("Disconnected");
        if(!failure_.isEmpty())throw std::runtime_error(failure_.toStdString());
        if(readySerial_==serial && source_)return GST_ELEMENT(gst_object_ref(source_));
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Video surface initialization timed out");
        changed_.wait_for(lock,std::chrono::milliseconds(50));
    }
}
void Media::waitEos(const std::function<bool()>& cancelled,int seconds){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    std::unique_lock lock(mutex_);
    while(!eos_){
        if(cancelled())throw std::runtime_error("Disconnected");
        if(!failure_.isEmpty())throw std::runtime_error(failure_.toStdString());
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Decoder drain timed out");
        changed_.wait_for(lock,std::chrono::milliseconds(50));
    }
}
void Media::inspectBus(){
    if(!bus_)return;
    while(auto* message=gst_bus_pop_filtered(bus_,static_cast<GstMessageType>(GST_MESSAGE_ERROR|GST_MESSAGE_EOS))){
        QString reason;
        if(GST_MESSAGE_TYPE(message)==GST_MESSAGE_ERROR){GError* error=nullptr;gchar* details=nullptr;
            gst_message_parse_error(message,&error,&details);reason=error?QString::fromUtf8(error->message):"Video pipeline failed";
            if(error)g_error_free(error);
            g_free(details);}
        {std::lock_guard lock(mutex_);if(!reason.isEmpty())failure_=reason;else eos_=true;changed_.notify_all();}
        gst_message_unref(message);if(!reason.isEmpty()){emit failed(reason);return;}
    }
}
}
