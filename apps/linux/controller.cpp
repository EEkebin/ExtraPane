#include "controller.hpp"
#include <ExtraPaneVersion.h>
#include <QGuiApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QScreen>
#include <QSettings>
#include <algorithm>
#include <cmath>

Controller::Controller(QString availability,QObject* parent):QObject(parent),availability_(std::move(availability)){
    refreshAddresses();
    connect(&media_,&dm::linux_client::Media::failed,this,[this](const QString& reason){
        failed_=true;status_=reason;if(receiver_)receiver_->requestInterruption();emit changed();
    });
    connect(&media_,&dm::linux_client::Media::decoderChanged,this,[this](const QString& name){decoder_=name;emit statisticsChanged();});
    connect(qGuiApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){
        if(state!=Qt::ApplicationActive)releaseInput();
    });
}
Controller::~Controller(){shutdown();}
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
void Controller::connectTo(QString server,QString local,bool input,unsigned scale,unsigned interfaceIndex){
    if(receiver_)return;
    const QHostAddress address(server.trimmed());
    if(address.protocol()!=QAbstractSocket::IPv4Protocol || !address.toIPv4Address() || address.toIPv4Address()==0xffffffffu || address.isMulticast() || address.isLoopback()){
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
    const unsigned hz=static_cast<unsigned>(std::clamp(std::lround(screen->refreshRate()),1L,60L));
    dm::linux_client::Receiver::Options options{address.toString(),local,
        {static_cast<unsigned>(size.width()),static_cast<unsigned>(size.height()),hz},scale,input,interfaceIndex};
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
        receiver_=nullptr;media_.stop();releaseInput();worker->deleteLater();emit changed();
    });
    status_="Connecting...";receiver_->start();emit changed();emit statisticsChanged();
}
void Controller::disconnectFromServer(){releaseInput();if(receiver_)receiver_->requestInterruption();}
void Controller::releaseInput(){if(inputSurface_)inputSurface_->resetInput();else input_.reset();}
void Controller::shutdown(){
    if(receiver_){receiver_->requestInterruption();receiver_->wait();QObject::disconnect(receiver_,nullptr,this,nullptr);delete receiver_;receiver_=nullptr;}
    media_.stop();
}
