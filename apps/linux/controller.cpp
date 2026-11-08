#include "controller.hpp"
#include "connection_runtime.hpp"
#include <ExtraPaneVersion.h>
#include <QGuiApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <algorithm>
#include <cmath>

Controller::Controller(QString availability,QObject* parent,bool multiClient):QObject(parent),availability_(std::move(availability)){
    resources_.setCapability(extrapane::ResourceKind::display,{availability_.isEmpty(),availability_.toStdString()});
    // Missing display/endpoint integrations reject only that resource, not the listener.
    hostAvailability_.clear();
    if(multiClient){runtime_=std::make_unique<LinuxConnectionRuntime>(availability_,this);connect(runtime_.get(),&LinuxConnectionRuntime::changed,this,&Controller::changed);}
    refreshAddresses();
    auto* timer=new QTimer(this);timer->setInterval(500);
    connect(timer,&QTimer::timeout,this,[this]{if(!accessGate_)return;auto state=accessGate_->status();if(state==dm::access::Gate::Status::active)return;
        QString text=state==dm::access::Gate::Status::used?"Code used. Generate a new code for another connection.":state==dm::access::Gate::Status::expired?"Code expired. Generate a new code.":"Attempt limit reached. Generate a new code.";
        if(accessCode_!=text){accessCode_=text;emit hostChanged();}});timer->start();
    connect(&media_,&dm::linux_client::Media::failed,this,[this](const QString& reason){
        failed_=true;status_=reason;if(receiver_)receiver_->requestInterruption();emit changed();
    });
    connect(&media_,&dm::linux_client::Media::decoderChanged,this,[this](const QString& name){decoder_=name;emit statisticsChanged();});
    connect(qGuiApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){
        if(state!=Qt::ApplicationActive)releaseInput();
    });
}
Controller::~Controller(){shutdown();}
bool Controller::active()const{return receiver_!=nullptr||(runtime_&&runtime_->active());}
void Controller::setEngine(QQmlEngine* engine){if(runtime_)runtime_->setEngine(engine);}
QString Controller::version()const{return QString::fromUtf8(EXTRAPANE_VERSION);}
QString Controller::savedAddress()const{return QSettings().value("serverAddress").toString();}
void Controller::refreshAddresses(){
    addresses_.clear();
    addresses_.push_back(QVariantMap{{"label","Automatic route"},{"address","0.0.0.0"},{"index",0}});
    for(const auto& interface:QNetworkInterface::allInterfaces()){
        const auto flags=interface.flags();
        if(!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) || flags.testFlag(QNetworkInterface::IsLoopBack))continue;
        if(interface.type()!=QNetworkInterface::Ethernet && interface.type()!=QNetworkInterface::Wifi)continue;
        for(const auto& entry:interface.addressEntries()){
            const auto ip=entry.ip();
            if(ip.protocol()!=QAbstractSocket::IPv4Protocol || ip.isLoopback() || ip.isNull())continue;
            addresses_.push_back(QVariantMap{{"label",interface.humanReadableName()+" — "+ip.toString()},{"address",ip.toString()},{"index",interface.index()}});
        }
    }
    emit addressesChanged();
}
void Controller::attachSurface(QObject* video,QObject* input){
    video_=qobject_cast<QQuickItem*>(video);inputSurface_=qobject_cast<dm::linux_client::InputSurface*>(input);
    if(inputSurface_)inputSurface_->queue=&input_;
}
void Controller::connectTo(QString server,QString local,bool input,unsigned scale,unsigned interfaceIndex,unsigned port,QString code){
    if(receiver_)return;
    if(!resources_.canConnect() || !resources_.selected(extrapane::ResourceKind::display)){
        status_="Select an available resource before connecting.";emit changed();return;
    }
    if(!port||port>65535||(!code.isEmpty()&&!dm::access::validCode(code.toStdString())&&!dm::access::channelCredential(code.toStdString()))){status_="Enter a valid port and six-digit access code";emit changed();return;}
    const QHostAddress address(server.trimmed().compare("localhost",Qt::CaseInsensitive)==0?QStringLiteral("127.0.0.1"):server.trimmed());
    if(address.protocol()!=QAbstractSocket::IPv4Protocol || !address.toIPv4Address() || address.toIPv4Address()==0xffffffffu || address.isMulticast()){
        status_="Enter the server's LAN IPv4 address.";emit changed();return;
    }
    if(!availability_.isEmpty()){status_=availability_;emit changed();return;}
    if(!video_ || !window_ || !window_->screen()){status_="Video surface is not ready.";emit changed();return;}
    if(scale>2){status_="Invalid stream resolution choice.";emit changed();return;}
    if(interfaceIndex){
        const auto interface=QNetworkInterface::interfaceFromIndex(static_cast<int>(interfaceIndex));
        bool ownsAddress=false;
        for(const auto& entry:interface.addressEntries())if(entry.ip().toString()==local)ownsAddress=true;
        if(!ownsAddress || !interface.flags().testFlag(QNetworkInterface::IsUp) || !interface.flags().testFlag(QNetworkInterface::IsRunning)){
            status_="The selected network address is no longer connected. Refresh the address list.";emit changed();return;
        }
    }else if(local!="0.0.0.0") {status_="Select a connected local interface.";emit changed();return;}
    auto* screen=window_->screen();const auto size=screen->geometry().size()*screen->devicePixelRatio();
    const unsigned hz=static_cast<unsigned>(std::clamp(std::lround(screen->refreshRate()),1L,360L));
    dm::linux_client::Receiver::Options options{address.toString(),local,
        {static_cast<unsigned>(size.width()),static_cast<unsigned>(size.height()),hz},scale,input,interfaceIndex};
    if(displayMode_.value("mode")=="custom"||displayMode_.value("mode")=="preset"){
        const auto width=displayMode_.value("width").toUInt(),height=displayMode_.value("height").toUInt(),fps=displayMode_.value("fps").toUInt();
        if(width<2||height<2||width>4096||height>4096||width%2||height%2||static_cast<uint64_t>(width)*height>8847360||fps<1||fps>360){status_="Invalid custom display mode";emit changed();return;}
        options.display={width,height,fps,displayMode_.value("mode")=="custom"};
    }
    options.port=port;options.code=code;options.screen=screenRequest_;options.existingScreenInput=screenInput_;
    QSettings().setValue("serverAddress",options.server);
    releaseInput();input_.grant(0);media_.resetCounters();failed_=false;statistics_.clear();decoder_="Waiting for decoder";
    receiver_=new dm::linux_client::Receiver(options,input_,media_,this);
    auto* worker=receiver_;
    connect(receiver_,&dm::linux_client::Receiver::configure,this,[this](unsigned w,unsigned h,unsigned,unsigned serial){
        if(!receiver_ || receiver_->isInterruptionRequested())return;
        releaseInput();width_=w;height_=h;emit changed();media_.prepare(video_,w,h,serial);
    });
    connect(receiver_,&dm::linux_client::Receiver::state,this,[this](const QString& state){if(!failed_)status_=state;emit changed();});
    connect(receiver_,&dm::linux_client::Receiver::failed,this,[this](const QString& reason){failed_=true;status_=reason;emit changed();});
    connect(receiver_,&dm::linux_client::Receiver::statistics,this,[this](const QString& text){statistics_=text;emit statisticsChanged();});
    connect(receiver_,&QThread::finished,this,[this,worker]{
        if(receiver_!=worker)return;
        receiver_=nullptr;resources_.setLocked(false);media_.stop();releaseInput();worker->deleteLater();emit changed();
    });
    resources_.setLocked(true);status_="Connecting...";receiver_->start();emit changed();emit statisticsChanged();
}
void Controller::disconnectFromServer(){releaseInput();if(runtime_)runtime_->disconnectAll();if(receiver_)receiver_->requestInterruption();}
void Controller::releaseInput(){if(runtime_)runtime_->releaseInput();if(inputSurface_)inputSurface_->resetInput();else input_.reset();}
void Controller::startHost(QString address,unsigned port,bool requireCode){
    if(address.compare("localhost",Qt::CaseInsensitive)==0)address="127.0.0.1";
    if(!port||port>65535){hostStatus_="Enter a port from 1 to 65535";emit hostChanged();return;}
    if(host_)return;
    if(!hostAvailability_.isEmpty()){hostStatus_=hostAvailability_;emit hostChanged();return;}
    if(address=="0.0.0.0"){address="127.0.0.1";for(const auto& entry:addresses_)if(entry.toMap().value("address").toString()!="0.0.0.0"){address=entry.toMap().value("address").toString();break;}}
    bool valid=address=="127.0.0.1";
    for(const auto& entry:addresses_)if(entry.toMap().value("address").toString()==address && address!="0.0.0.0")valid=true;
    if(!valid){hostStatus_="Select a connected server IPv4 address";emit hostChanged();return;}
    accessGate_=requireCode?std::make_shared<dm::access::Gate>():nullptr;accessCode_.clear();if(accessGate_)generateAccessCode();
    host_=new dm::linux_client::Host(address,this,port,accessGate_);auto* worker=host_;
    connect(worker,&dm::linux_client::Host::state,this,[this](const QString& status){hostStatus_=status;emit hostChanged();});
    connect(worker,&QThread::finished,this,[this,worker]{if(host_!=worker)return;host_=nullptr;if(accessGate_)accessGate_->revoke();accessGate_.reset();accessCode_.clear();worker->deleteLater();emit hostChanged();});
    hostStatus_="Starting listener...";worker->start();emit hostChanged();
}
void Controller::stopHost(){if(host_){host_->requestInterruption();hostStatus_="Stopping server...";emit hostChanged();}}
void Controller::shutdown(){
    if(runtime_)runtime_->shutdown();
    if(host_){host_->requestInterruption();host_->wait();QObject::disconnect(host_,nullptr,this,nullptr);delete host_;host_=nullptr;}

    if(receiver_){receiver_->requestInterruption();receiver_->wait();QObject::disconnect(receiver_,nullptr,this,nullptr);delete receiver_;receiver_=nullptr;}
    media_.stop();
}

void Controller::generateAccessCode(){if(!accessGate_)return;try{accessCode_=QString::fromStdString(accessGate_->generate())+" ? one use, expires in 5 minutes";}catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}emit hostChanged();}
