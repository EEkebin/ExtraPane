#include "peer_controller.hpp"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QFileInfo>
#include <iostream>
#include <stdexcept>

struct PeerControllerEventCheck {
    static void run(PeerController& controller) {
        using namespace extrapane::peers;
        auto require=[](bool good,const char* why){if(!good)throw std::runtime_error(why);};
        DeviceId owner{};owner[0]=7;
        Service::Event event{};event.kind=Service::EventKind::granted;event.context.connection=41;event.context.grant=8;event.context.kind=Kind::camera;event.resource=rpc::Open{owner,"camera-a"};
        controller.applyEvent(event);require(controller.connections_.size()==1,"Grant not shown");require(controller.connections().first().toMap().value("kindId").toInt()==static_cast<int>(Kind::camera),"Granted camera lost resource grouping kind");
        event.kind=Service::EventKind::reconnecting;controller.applyEvent(event);
        require(controller.connections_.first().toMap().value("phase")=="reconnecting","Retry row not retained");
        event.kind=Service::EventKind::stopped;controller.applyEvent(event);require(controller.connections_.size()==1,"Old teardown removed retry row");
        event.kind=Service::EventKind::granted;event.context.grant=9;event.replaces_grant=8;controller.applyEvent(event);event.replaces_grant=0;
        require(controller.connections_.size()==1&&controller.connections_.first().toMap().value("grant")=="9","Retry duplicated resource row");
        event.context.grant=10;controller.applyEvent(event);event.kind=Service::EventKind::reconnecting;controller.applyEvent(event);
        event.kind=Service::EventKind::granted;event.context.grant=11;event.replaces_grant=9;controller.applyEvent(event);event.replaces_grant=0;require(controller.connections_.size()==2,"Replay removed multiple same-resource sessions");
        event.kind=Service::EventKind::stopped;event.detail="Reconnect ended";event.context.grant=10;controller.applyEvent(event);require(controller.connections_.size()==1,"Replay rejection left retry row");
        event.kind=Service::EventKind::reconnecting;event.context.grant=11;event.replaces_grant=9;controller.applyEvent(event);event.replaces_grant=0;controller.stopResource("41","11");require(controller.connections_.empty(),"Cancelled retry row remains");
        controller.pending_.insert("12",{{"connection","41"},{"resource","camera-a"},{"label","Saved camera"},{"kind","Camera"},{"kindId",static_cast<int>(Kind::camera)}});
        require(controller.devices_.empty()&&controller.connections().first().toMap().value("kindId").toInt()==static_cast<int>(Kind::camera)&&controller.connections().first().toMap().value("label")=="Saved camera","Pending resource grouping snapshot lost without catalog");
        require(controller.connections().size()==1&&controller.connections().first().toMap().value("request")=="12","Pending resource absent from UI");
        controller.cancelResource("99","12");require(controller.pending_.contains("12"),"Wrong connection cancelled pending row");
        event.kind=Service::EventKind::error;event.context.request=12;event.context.connection=99;controller.applyEvent(event);require(controller.pending_.contains("12"),"Other connection consumed pending request");
        event.context.connection=41;event.context.incoming=true;controller.applyEvent(event);require(controller.pending_.contains("12"),"Remote request consumed pending request");
        event.context.incoming=false;controller.applyEvent(event);require(controller.pending_.empty(),"Failed request remained pending");
        event.kind=Service::EventKind::granted;controller.applyEvent(event);event.kind=Service::EventKind::disconnected;controller.applyEvent(event);require(controller.connections_.empty(),"Final disconnect retained resource");
    }
};

int main(int argc,char** argv){QCoreApplication app(argc,argv);app.setOrganizationName("ExtraPaneIsolatedChecks");app.setApplicationName("PeerControllerStartup");QTemporaryDir temporary;if(!temporary.isValid())return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    const auto path=temporary.path()+"/app/peers";
    try{
        extrapane::peers::DeviceId identity{};
        {PeerController controller({},path);if(controller.active()||controller.sharing()||controller.pairingCode().size()!=6)throw std::runtime_error("Unexpected startup policy");
#ifdef _WIN32
        extrapane::peers::TrustStore store(std::filesystem::path(path.toStdWString()));
#else
        extrapane::peers::TrustStore store(std::filesystem::u8path(path.toUtf8().toStdString()));
#endif
        identity=store.identity();if(!store.devices().empty())throw std::runtime_error("Unexpected remembered device");}
        {PeerController controller({},path);
#ifdef _WIN32
        extrapane::peers::TrustStore store(std::filesystem::path(path.toStdWString()));
#else
        extrapane::peers::TrustStore store(std::filesystem::u8path(path.toUtf8().toStdString()));
#endif
        if(store.identity()!=identity)throw std::runtime_error("Startup replaced local identity");PeerControllerEventCheck::run(controller);}
        std::cout<<"Qt peer startup creates private store and preserves identity without listeners or media\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
