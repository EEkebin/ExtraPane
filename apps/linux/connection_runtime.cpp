#include "connection_runtime.hpp"
#include "controller.hpp"
#include "admission.hpp"
#include "resource_source.hpp"
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
using State=ConnectionModel::State;
using Source=dm::linux_client::ResourceSource;
QString targetKey(const QVariantMap& target){return target["address"].toString()+":"+target["port"].toString()+"@"+target["localAddress"].toString()+"#"+target["interfaceIndex"].toString();}
unsigned kindNumber(const QString& kind){return kind=="display"?0:kind=="camera"?1:kind=="microphone"?2:3;}
}
struct LinuxConnectionRuntime::Row {
    std::unique_ptr<Controller> display;
    std::unique_ptr<QQmlContext> context;
    QPointer<QQuickWindow> window;
    QPointer<Source> source;
    QString admission,error,kind,ticketId;
    uint64_t generation=0;
    bool input=true,stopping=false;
    ~Row(){if(display)display->shutdown();if(window)delete window;if(source){source->requestInterruption();source->wait();delete source;}}
};
LinuxConnectionRuntime::LinuxConnectionRuntime(QString availability,QObject* parent):QObject(parent),availability_(std::move(availability)){
    for(const auto* kind:{"camera","microphone","usb"})devices_[kind]=QVariantList{QVariantMap{{"id",""},{"label",kind==QString("usb")?"USB sharing is not available in this Linux build":"Open to find connected devices"},{"available",false}}};
    connect(qGuiApp,&QGuiApplication::screenAdded,this,[this]{emit monitorsChanged();});
    connect(qGuiApp,&QGuiApplication::screenRemoved,this,[this]{emit monitorsChanged();});
}
LinuxConnectionRuntime::~LinuxConnectionRuntime(){shutdown();}
bool LinuxConnectionRuntime::active()const{return !rows_.empty();}
QVariantList LinuxConnectionRuntime::monitors()const{
    QVariantList result;for(auto* screen:QGuiApplication::screens()){
        const auto size=screen->geometry().size()*screen->devicePixelRatio();
        result.push_back(QVariantMap{{"id",screen->name()},{"label",screen->name()+" - "+QString::number(size.width())+" x "+QString::number(size.height())+" @ "+QString::number(screen->refreshRate(),'f',0)+" Hz"},{"width",size.width()},{"height",size.height()},{"fps",static_cast<int>(std::clamp(std::lround(screen->refreshRate()),1L,360L))}});
    }return result;
}
void LinuxConnectionRuntime::refreshDevices(QString kind){
    if(shuttingDown_||(kind!="camera"&&kind!="microphone")||scanning_.contains(kind))return;
    scanning_.insert(kind);
    auto* scanner=QThread::create([this,kind]{
        const auto type=kind=="camera"?Source::Kind::Camera:Source::Kind::Microphone;
        QString unavailable;QVariantList list;
        try{unavailable=Source::unavailable(type);if(unavailable.isEmpty())list=Source::devices(type);}
        catch(const std::exception& error){unavailable=QString::fromUtf8(error.what());}
        if(list.isEmpty())list.push_back(QVariantMap{{"id",""},{"label",unavailable.isEmpty()?"No compatible "+kind+" source found":unavailable},{"available",false}});
        QMetaObject::invokeMethod(this,[this,kind,list=std::move(list)]{
            scanning_.remove(kind);if(shuttingDown_)return;devices_[kind]=list;emit devicesChanged();
        },Qt::QueuedConnection);
    });
    scanner->setObjectName("ExtraPane source enumeration");scanner->setParent(this);
    connect(scanner,&QThread::finished,scanner,&QObject::deleteLater);scanner->start();
}
void LinuxConnectionRuntime::toggle(QString id,bool enabled,QString code,bool input){
    struct Wipe{QString& text;~Wipe(){text.fill(QChar(0));}} wipe{code};
    if(shuttingDown_||model_.connection(id).isEmpty())return;
    if(enabled&&rows_.contains(id)){if(rows_[id]->stopping)model_.setEnabled(id,false);return;}
    model_.setEnabled(id,enabled);
    if(!enabled){
        const auto found=rows_.find(id);if(found==rows_.end())return;
        auto& row=*found->second;row.stopping=true;if(admissions_.contains(row.admission)&&admissions_[row.admission])admissions_[row.admission]->cancel(row.ticketId);model_.setState(id,State::returning,"Disconnecting...");
        if(row.display)row.display->disconnectFromServer();else if(row.source)row.source->requestInterruption();else finish(id);
        return;
    }
    if(!model_.beginConnection(id)){model_.setEnabled(id,false);return;}
    const auto config=model_.connection(id);const auto kind=config["resourceKind"].toString();
    if(kind=="usb"){model_.setEnabled(id,false);model_.setState(id,State::failed,"USB sharing is not available in this Linux build.");return;}
    auto row=std::make_unique<Row>();row->kind=kind;row->input=input;row->generation=++nextGeneration_;row->ticketId=id+"/"+QString::number(row->generation);rows_[id]=std::move(row);
    const auto target=config["effectiveTarget"].toMap();const auto key=targetKey(target);
    if(admissions_.contains(key)&&(!admissions_[key]||admissions_[key]->isInterruptionRequested()||admissions_[key]->isFinished())){
        finish(id,"Server control connection ended. Disconnect this server before reconnecting.");return;
    }
    if(!code.isEmpty()||admissions_.contains(key)){
        if(!admissions_.contains(key)){
            if(!dm::access::validCode(code.toStdString())){finish(id,"Access code must contain six digits.");return;}
            auto* broker=new LinuxAdmission(target,code,this);admissions_[key]=broker;
            connect(broker,&LinuxAdmission::ticket,this,[this,key,broker](QString ticketId,QString credential){const auto rowId=ticketId.section('/',0,0);auto r=rows_.find(rowId);if(admissions_.contains(key)&&admissions_[key]==broker&&r!=rows_.end()&&!r->second->stopping&&r->second->admission==key&&r->second->ticketId==ticketId)launch(rowId,credential);credential.fill(QChar(0));});
            connect(broker,&LinuxAdmission::failed,this,[this,key,guard=QPointer<LinuxAdmission>(broker)](const QString& reason){
                auto* broker=guard.data();if(!broker||!admissions_.contains(key)||admissions_[key]!=broker)return;
                broker->stop();
                QStringList ids;for(const auto& [rowId,row]:rows_)if(row->admission==key)ids.push_back(rowId);
                for(const auto& rowId:ids){auto& row=*rows_.at(rowId);row.error=reason;if(row.display)row.display->disconnectFromServer();else if(row.source)row.source->requestInterruption();else finish(rowId,reason);}
            });
            broker->start();
        }
        rows_[id]->admission=key;if(!admissions_[key]->request(rows_[id]->ticketId,kindNumber(kind)))finish(id,"Too many pending resource requests. Try again shortly.");
    }else launch(id,{});
    code.fill(QChar(0));emit changed();
}
void LinuxConnectionRuntime::launch(QString id,QString credential){
    struct Wipe{QString& text;~Wipe(){text.fill(QChar(0));}} wipe{credential};
    const auto found=rows_.find(id);if(found==rows_.end())return;auto& row=*found->second;const auto generation=row.generation;
    const auto config=model_.connection(id);const auto target=config["effectiveTarget"].toMap();
    if(row.kind=="display"){
        if(!engine_){finish(id,"The display interface is not ready.");return;}
        row.display=std::make_unique<Controller>(availability_,nullptr,false);row.context=std::make_unique<QQmlContext>(engine_->rootContext());
        row.context->setContextProperty("receiver",row.display.get());
        QQmlComponent component(engine_,QUrl("qrc:/ReceiverWindow.qml"));
        auto* object=component.create(row.context.get());auto* window=qobject_cast<QQuickWindow*>(object);
        if(!window){delete object;finish(id,"Could not create display window: "+component.errorString());return;}
        row.window=window;row.display->setWindow(window);const auto mode=config["displayMode"].toMap();row.display->setDisplayMode(mode);
        if(mode["mode"]=="native"){
            QScreen* selected=nullptr;for(auto* screen:QGuiApplication::screens())if(screen->name()==mode["monitorId"].toString()){selected=screen;break;}
            if(!selected){finish(id,"The selected monitor is no longer connected.");return;}
            window->setScreen(selected);window->setPosition(selected->geometry().topLeft()+QPoint(40,40));
        }
        connect(row.display.get(),&Controller::changed,this,[this,id,generation]{
            const auto f=rows_.find(id);if(f==rows_.end()||f->second->generation!=generation||!f->second->display)return;auto& current=*f->second;
            if(!current.display->active()){finish(id,current.stopping?QString{}:current.error.isEmpty()&&current.display->status()!="Disconnected"?current.display->status():current.error);return;}
            if(!current.stopping)model_.setState(id,current.display->status()=="Connected"?State::active:State::connecting,current.display->status());emit changed();
        });
        row.display->connectTo(target["address"].toString(),target["localAddress"].toString(),row.input,0,target["interfaceIndex"].toUInt(),target["port"].toUInt(),credential);
        if(!row.display->active())finish(id,row.display->status());
    }else{
        const auto type=row.kind=="camera"?Source::Kind::Camera:Source::Kind::Microphone;
        const auto unavailable=Source::unavailable(type);if(!unavailable.isEmpty()){finish(id,unavailable);return;}
        Source::Options options;options.kind=type;options.device=config["sourceId"].toString();options.server=target["address"].toString();options.local=target["localAddress"].toString();options.interfaceIndex=target["interfaceIndex"].toUInt();options.port=target["port"].toUInt();options.code=credential;
        for(const auto& item:devices_.value(row.kind).toList()){const auto source=item.toMap();if(source["id"]==options.device){if(source.contains("width"))options.width=source["width"].toUInt();if(source.contains("height"))options.height=source["height"].toUInt();if(source.contains("fps"))options.fps=source["fps"].toUInt();break;}}
        row.source=new Source(options,this);auto* source=row.source.data();
        connect(source,&Source::state,this,[this,id,generation](const QString& text){if(rows_.contains(id)&&rows_[id]->generation==generation&&!rows_[id]->stopping)model_.setState(id,model_.connection(id)["phase"]=="active"?State::active:State::connecting,text);});
        connect(source,&Source::connected,this,[this,id,generation](unsigned,unsigned,unsigned){if(rows_.contains(id)&&rows_[id]->generation==generation&&!rows_[id]->stopping)model_.setState(id,State::active,"Connected");emit changed();});
        connect(source,&Source::failed,this,[this,id,generation](const QString& error){if(rows_.contains(id)&&rows_[id]->generation==generation)rows_[id]->error=error;});
        connect(source,&QThread::finished,this,[this,id,generation]{const auto f=rows_.find(id);if(f!=rows_.end()&&f->second->generation==generation)finish(id,f->second->error);});source->start();
    }
    credential.fill(QChar(0));
}
void LinuxConnectionRuntime::finish(QString id,QString error){
    const auto found=rows_.find(id);if(found==rows_.end())return;
    if(error.isEmpty())error=found->second->error;
    found->second->stopping=true;
    model_.setEnabled(id,false);model_.setState(id,State::returning,"Finishing resource cleanup...");
    // Keep ownership reserved until destruction, then expose editable controls.
    const auto generation=found->second->generation;
    QTimer::singleShot(0,this,[this,id,generation,error]{auto f=rows_.find(id);if(f==rows_.end()||f->second->generation!=generation)return;const auto key=f->second->admission;rows_.erase(f);closeAdmission(key);model_.setState(id,error.isEmpty()?State::idle:State::failed,error);emit changed();});
}
void LinuxConnectionRuntime::closeAdmission(QString key){
    if(key.isEmpty())return;for(const auto& [id,row]:rows_)if(row->admission==key)return;
    const auto found=admissions_.find(key);if(found==admissions_.end())return;
    auto* broker=found->second.data();admissions_.erase(found);if(broker){broker->stop();connect(broker,&QThread::finished,broker,&QObject::deleteLater);if(broker->isFinished())broker->deleteLater();}
}
void LinuxConnectionRuntime::disconnectAll(){QStringList ids;for(const auto& [id,row]:rows_)ids.push_back(id);for(const auto& id:ids)toggle(id,false);}
void LinuxConnectionRuntime::releaseInput(){for(auto& [id,row]:rows_)if(row->display)row->display->releaseInput();}
void LinuxConnectionRuntime::setMirror(QString id,bool enabled){const auto c=model_.connection(id);const auto source=c["sourceId"].toString();for(auto& [key,row]:rows_)if(row->kind=="camera"&&row->source&&model_.connection(key)["sourceId"]==source)row->source->setMirrored(enabled);mirrors_[source]=enabled;emit changed();}
void LinuxConnectionRuntime::setMuted(QString id,bool enabled){const auto f=rows_.find(id);if(f!=rows_.end()&&f->second->kind=="microphone"&&f->second->source)f->second->source->setMuted(enabled);}
void LinuxConnectionRuntime::shutdown(){
    if(shuttingDown_)return;shuttingDown_=true;
    const auto brokers=findChildren<LinuxAdmission*>();
    for(auto* broker:brokers)broker->stop();
    for(auto& [id,row]:rows_)if(row->source)row->source->requestInterruption();else if(row->display)row->display->disconnectFromServer();
    rows_.clear();
    for(auto* broker:brokers){broker->wait();delete broker;}admissions_.clear();
    for(auto* worker:findChildren<QThread*>(QString{},Qt::FindDirectChildrenOnly)){worker->requestInterruption();worker->wait();delete worker;}
}
