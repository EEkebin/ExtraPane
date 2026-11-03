#include "portal.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusArgument>
#include <QTimer>
#include <QUuid>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <algorithm>
#include <stdexcept>

namespace dm::linux_client {
namespace {
const QString service="org.freedesktop.portal.Desktop",path="/org/freedesktop/portal/desktop";
const QString remote="org.freedesktop.portal.RemoteDesktop",cast="org.freedesktop.portal.ScreenCast";
QDBusMessage call(const QString& object,const QString& interface,const QString& method,const QVariantList& args){
    auto message=QDBusMessage::createMethodCall(service,object,interface,method);message.setArguments(args);
    auto result=QDBusConnection::sessionBus().call(message,QDBus::Block,5000);
    if(result.type()==QDBusMessage::ErrorMessage)throw std::runtime_error(result.errorMessage().toStdString());
    return result;
}
unsigned portalProperty(const QString& interface,const QString& name){
    QDBusReply<QDBusVariant> reply(call(path,"org.freedesktop.DBus.Properties","Get",{interface,name}));
    if(!reply.isValid())throw std::runtime_error("Desktop portal is unavailable");
    return reply.value().variant().toUInt();
}
unsigned evdev(const dm::input::Event& e){
    if(!e.value){if(e.code<=0x58)return e.code;return 0;}
    switch(e.code){case 0x1c:return 96;case 0x1d:return 97;case 0x35:return 98;case 0x38:return 100;
    case 0x47:return 102;case 0x48:return 103;case 0x49:return 104;case 0x4b:return 105;case 0x4d:return 106;
    case 0x4f:return 107;case 0x50:return 108;case 0x51:return 109;case 0x52:return 110;case 0x53:return 111;
    case 0x5b:return 125;case 0x5c:return 126;case 0x5d:return 127;default:return 0;}
}
}
Portal::Portal(std::function<bool()> cancelled):cancelled_(std::move(cancelled)){}
Portal::~Portal(){
    try{release();}catch(...){}
    if(!session_.path().isEmpty())try{call(session_.path(),"org.freedesktop.portal.Session","Close",{});}catch(...){}
}
QString Portal::unavailable(){
    if(!QGuiApplication::platformName().startsWith("wayland"))return "Hosting requires a Wayland desktop with a virtual-display portal.";
    try{if(!(portalProperty(cast,"AvailableSourceTypes")&4))return "This desktop portal does not provide virtual monitors.";
        if((portalProperty(remote,"AvailableDeviceTypes")&3)!=3)return "The desktop portal does not provide mouse and keyboard control.";
    }catch(const std::exception& e){return QString::fromUtf8(e.what());}return {};
}
void Portal::response(uint code,const QVariantMap& results){responseCode_=code;results_=results;replied_=true;if(loop_)loop_->quit();}
void Portal::sessionClosed(){closed_=true;if(loop_)loop_->quit();}
QVariantMap Portal::request(const QString& interface,const QString& method,QVariantList arguments,QVariantMap options){
    auto bus=QDBusConnection::sessionBus();auto sender=bus.baseService().mid(1);sender.replace('.','_');
    const auto token="extrapane_"+QUuid::createUuid().toString(QUuid::Id128);
    const QString handle="/org/freedesktop/portal/desktop/request/"+sender+"/"+token;
    options.insert("handle_token",token);arguments.append(options);replied_=false;results_.clear();responseCode_=2;
    if(!bus.connect(service,handle,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap))))throw std::runtime_error("Cannot observe portal request");
    try{
        QDBusReply<QDBusObjectPath> reply(call(path,interface,method,arguments));
        if(!reply.isValid() || reply.value().path()!=handle)throw std::runtime_error("Unexpected portal request handle");
        QEventLoop loop;loop_=&loop;QElapsedTimer elapsed;elapsed.start();QTimer timer;
        connect(&timer,&QTimer::timeout,&loop,[&]{if(cancelled_() || closed_ || elapsed.elapsed()>40000)loop.quit();});timer.start(50);
        if(!replied_)loop.exec();
        loop_=nullptr;
        if(!replied_){try{call(handle,"org.freedesktop.portal.Request","Close",{});}catch(...){}throw std::runtime_error("Portal permission canceled or timed out");}
        if(responseCode_!=0)throw std::runtime_error("Desktop sharing permission was declined");
    }catch(...){loop_=nullptr;bus.disconnect(service,handle,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)));throw;}
    bus.disconnect(service,handle,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)));return results_;
}
void Portal::start(bool input){
    auto result=request(remote,"CreateSession",{},{{"session_handle_token","extrapane_"+QUuid::createUuid().toString(QUuid::Id128)}});
    session_=QDBusObjectPath(result.value("session_handle").toString());
    if(session_.path().isEmpty())throw std::runtime_error("Missing portal session");
    QDBusConnection::sessionBus().connect(service,session_.path(),"org.freedesktop.portal.Session","Closed",this,SLOT(sessionClosed()));
    // RemoteDesktop requires at least one device. Only forward negotiated input.
    request(remote,"SelectDevices",{QVariant::fromValue(session_)},{{"types",portalProperty(remote,"AvailableDeviceTypes")&7u},{"persist_mode",0u}});
    request(cast,"SelectSources",{QVariant::fromValue(session_)},{{"types",4u},{"multiple",false},{"cursor_mode",2u}});
    result=request(remote,"Start",{QVariant::fromValue(session_),QString{}},{});
    devices=input?result.value("devices").toUInt():0;
    auto streams=result.value("streams").value<QDBusArgument>();streams.beginArray();
    if(streams.atEnd())throw std::runtime_error("Portal did not create a virtual display");
    QVariantMap properties;streams.beginStructure();streams>>node>>properties;streams.endStructure();
    if(!streams.atEnd())throw std::runtime_error("Expected one session-owned virtual display");
    streams.endArray();
    if(properties.value("source_type").toUInt()!=4)throw std::runtime_error("Portal returned a physical display; extended hosting requires a virtual display");
    const auto size=properties.value("logical_size",properties.value("size"));
    if(size.canConvert<QDBusArgument>()){auto pair=size.value<QDBusArgument>();int w=0,h=0;pair.beginStructure();pair>>w>>h;pair.endStructure();if(w>0&&h>0){width=w;height=h;}}
    QDBusReply<QDBusUnixFileDescriptor> fd(call(path,cast,"OpenPipeWireRemote",{QVariant::fromValue(session_),QVariantMap{}}));
    if(!fd.isValid() || !fd.value().isValid())throw std::runtime_error("Cannot open portal PipeWire connection");
    pipewire=fd.value();
}
void Portal::notify(const QString& method,QVariantList arguments){
    if(closed_)throw std::runtime_error("Desktop sharing permission was revoked");
    arguments.prepend(QVariantMap{});arguments.prepend(QVariant::fromValue(session_));call(path,remote,method,arguments);
}
void Portal::release(){
    // Only release inputs actually pressed by this session.
    for(auto key:keys_)notify("NotifyKeyboardKeycode",{static_cast<int>(key),0u});
    keys_.clear();
    for(auto button:buttons_)notify("NotifyPointerButton",{static_cast<int>(button),0u});
    buttons_.clear();
    for(auto touch:touches_)notify("NotifyTouchUp",{touch});
    touches_.clear();
}
void Portal::inject(const dm::input::Event& e){
    using namespace dm::input;validate(e);
    if(e.kind==Kind::reset){release();return;}
    const double x=static_cast<double>(e.x)*std::max(1u,width-1)/65535,y=static_cast<double>(e.y)*std::max(1u,height-1)/65535;
    if(e.kind==Kind::key && (devices&1)){
        const auto key=evdev(e);if(!key)return;
        if(e.action==Action::down){keys_.insert(key);notify("NotifyKeyboardKeycode",{static_cast<int>(key),1u});}
        else if(keys_.erase(key))notify("NotifyKeyboardKeycode",{static_cast<int>(key),0u});
    }else if(e.kind==Kind::mouse && (devices&2)){
        notify("NotifyPointerMotionAbsolute",{node,x,y});
        if(e.action==Action::down || e.action==Action::up){
            const unsigned codes[]={0,272,273,274,275,276};const auto code=codes[e.code];
            if(e.action==Action::down){buttons_.insert(code);notify("NotifyPointerButton",{static_cast<int>(code),1u});}
            else if(buttons_.erase(code))notify("NotifyPointerButton",{static_cast<int>(code),0u});
        }else if(e.action==Action::wheel){
            // Portal axis units are continuous surface units; Windows wheel values are 120/notch.
            const double delta=-static_cast<double>(e.value)/12;
            notify("NotifyPointerAxis",{e.code?delta:0.0,e.code?0.0:delta});
        }
    }else if(e.kind==Kind::touch && (devices&4)){
        if(e.action==Action::down){if(touches_.size()>=10 || touches_.contains(e.id))throw std::runtime_error("Invalid touch transition");touches_.insert(e.id);notify("NotifyTouchDown",{node,e.id,x,y});}
        else if(e.action==Action::move && touches_.contains(e.id))notify("NotifyTouchMotion",{node,e.id,x,y});
        else if((e.action==Action::up || e.action==Action::cancel)&&touches_.erase(e.id))notify("NotifyTouchUp",{e.id});
    }
}
}
