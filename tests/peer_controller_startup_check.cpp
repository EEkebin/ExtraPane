#include "peer_controller.hpp"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QTcpServer>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>

struct DeviceDirectoryCheck {
    static void run(){
        using namespace extrapane::peers;auto require=[](bool good,const char* why){if(!good)throw std::runtime_error(why);};
        DeviceDirectory directory;require(!directory.scanning()&&!directory.transportActive(),"Directory startup opened discovery");directory.table_.interfaces({7});
        Announcement beacon;beacon.device[0]=1;beacon.instance=1;beacon.sequence=1;beacon.name="Synthetic device";beacon.port=43824;
        auto bytes=[](const auto& value){auto encoded=encode(value);return QByteArray(reinterpret_cast<const char*>(encoded.data()),encoded.size());};
        directory.observeMessage(bytes(beacon),"192.0.2.2",7);directory.publish();require(directory.devices().empty(),"Idle discovery collected unsolicited beacon");
        directory.beginSearch();require(directory.scanning()&&directory.searchTimer_.interval()==10000,"Explicit search not bounded");directory.observeMessage(bytes(beacon),"192.0.2.2",7);directory.publish();require(directory.devices().size()==1,"Explicit search ignored beacon");
        Announcement local=beacon;local.device[0]=3;directory.announcement_=local;directory.finishSearch();require(!directory.scanning()&&directory.devices().size()==1&&directory.announcement_.has_value(),"Finishing search stopped advertising or discarded current result");
        ++beacon.sequence;beacon.name="Changed while idle";directory.observeMessage(bytes(beacon),"192.0.2.2",7);directory.publish();require(directory.devices().first().toMap().value("name")=="Synthetic device","Idle beacon refreshed search results");
        auto second=beacon;second.device[0]=2;directory.observeMessage(bytes(second),"192.0.2.3",7);directory.publish();require(directory.devices().size()==1,"Idle beacon added a new result");
        directory.observeMessage(bytes(Withdrawal{beacon.device,beacon.instance,++beacon.sequence}),"192.0.2.2",7);directory.publish();require(directory.devices().empty(),"Idle withdrawal retained obsolete device");
        directory.beginSearch();directory.observeMessage(bytes(second),"192.0.2.3",7);directory.publish();require(directory.devices().size()==1,"Subsequent explicit search not functional");directory.beginSearch();require(directory.devices().empty(),"Refresh retained previous search snapshot");
        directory.stop();require(!directory.scanning()&&!directory.announcement_,"Stop retained discovery or advertisement");
    }
};

struct PeerControllerEventCheck {
    static void sharing(PeerController& controller) {
        using namespace extrapane::peers;
        auto require=[](bool good,const char* why){if(!good)throw std::runtime_error(why);};
        const auto code=controller.pairingCode();
        controller.setLocalOffers({{"camera", "Camera", Kind::camera, true}, {"mic", "Microphone", Kind::microphone, true}});
        require(controller.enabled_.empty(),"Persisted offers exposed on fresh startup");
        controller.setOfferSharing("camera",true,"invalid",43824);
        require(!controller.sharingStarting()&&controller.enabled_.empty(),"Invalid bind enabled sharing");
        controller.setOfferSharing("camera",true,"localhost",0);
        require(!controller.sharingStarting()&&controller.enabled_.empty(),"Invalid port enabled sharing");
        QTcpServer reservation;require(reservation.listen(QHostAddress::LocalHost,0),"Cannot reserve loopback port");
        auto port=reservation.serverPort();
        controller.setOfferSharing("camera",true,"localhost",port);
        require(!controller.sharingStarting()&&controller.enabled_.empty(),"Occupied port left offer enabled");reservation.close();
        controller.setOfferSharing("camera",true,"localhost",port);
        require(controller.sharingStarting()&&controller.enabled_.contains("camera"),"First offer did not start sharing");
        const auto staleGeneration=controller.sharingGeneration_;
        controller.setOfferSharing("camera",false,{},0);
        require(!controller.sharingStarting()&&!controller.sharing()&&controller.enabled_.empty(),"Last offer did not cancel pending start");
        Service::Event stale{Service::EventKind::listening,{},port};stale.sharing_generation=staleGeneration;
        controller.applyEvent(stale);require(!controller.sharing(),"Late listener revived cancelled sharing");
        // A canceled acceptor releases its socket asynchronously. Keep testing
        // the same endpoint, with a bounded wait for that release.
        QElapsedTimer release;release.start();
        do {controller.setGatewaySharing(true,"localhost",port);if(!controller.sharingStarting())QThread::msleep(5);}while(!controller.sharingStarting()&&release.elapsed()<3000);
        if(!controller.gatewayEnabled()||!controller.sharingStarting())throw std::runtime_error(("Gateway-only sharing did not start: "+controller.status()).toStdString());
        controller.applyEvent(stale);require(!controller.sharing(),"Old listener accepted by new start");
        QElapsedTimer deadline;deadline.start();while(!controller.sharing()&&deadline.elapsed()<3000){QThread::msleep(5);controller.poll();}
        require(controller.sharing()&&!controller.sharingStarting(),"Listener acknowledgement not applied");
        controller.setOfferSharing("mic",true,{},0);
        controller.setOfferSharing("mic",false,{},0);
        require(controller.sharing()&&controller.gatewayEnabled(),"Last local offer stopped explicit gateway");
        controller.setGatewaySharing(false,{},0);require(!controller.sharing(),"Last gateway did not stop sharing");
        release.restart();do {controller.setOfferSharing("mic",true,"localhost",port);if(!controller.sharingStarting())QThread::msleep(5);}while(!controller.sharingStarting()&&release.elapsed()<3000);
        require(controller.sharingStarting(),"Same-port listener did not release");controller.setGatewaySharing(true,{},0);
        controller.connections_.push_back(QVariantMap{{"incoming",false},{"connection","555"}});
        controller.stopSharing();
        require(controller.enabled_.empty()&&!controller.gatewayEnabled()&&!controller.sharingStarting(),"Stop all retained selected sharing");
        require(controller.connections_.size()==1,"Stop sharing removed independent outgoing use");controller.connections_.clear();
        controller.poll();require(!controller.sharing(),"Queued acknowledgement revived stop all");
        require(controller.pairingCode()==code,"Sharing toggles rotated pairing code");
    }
    static void availability(PeerController& controller){
        using namespace extrapane::peers;auto require=[](bool good,const char* why){if(!good)throw std::runtime_error(why);};
        controller.setLocalOffers({{"front", "Front camera", Kind::camera, true},{"rear", "Rear camera", Kind::camera, true},{"missing", "Unavailable camera", Kind::camera, false}});
        QTcpServer reservation;require(reservation.listen(QHostAddress::LocalHost,0),"Cannot reserve auto-sharing port");const auto port=reservation.serverPort();reservation.close();
        controller.startAvailableSharing("localhost",port);require(controller.sharingStarting()&&controller.enabled_.size()==2,"Automatic availability did not publish supported resources");
        QElapsedTimer deadline;deadline.start();while(!controller.sharing()&&deadline.elapsed()<3000){QThread::msleep(5);controller.poll();}require(controller.sharing(),"Automatic listener did not acknowledge");
        require(!controller.active(),"Idle availability unnecessarily retains app in tray");
        const auto originalGeneration=controller.sharingGeneration_;controller.startAvailableSharing("not-an-address",port);require(controller.sharing()&&controller.sharingGeneration_==originalGeneration,"Invalid rebind stopped available listener");
        controller.startAvailableSharing("192.0.2.222",port);require(controller.sharing()&&controller.sharingGeneration_==originalGeneration,"Nonlocal rebind stopped available listener");
        controller.devices_.push_back(QVariantMap{{"connection","77"},{"incoming",true}});require(!controller.active(),"Idle authenticated catalog link retains app in tray");controller.devices_.clear();
        auto offer=[&](const char* id){for(const auto& row:controller.offers())if(row.toMap().value("id")==id)return row.toMap();return QVariantMap{};};
        require(!offer("front").value("inUse").toBool(),"Idle offer incorrectly red");
        controller.enableOffer("front",false);
        require(!offer("front").value("enabled").toBool()&&offer("rear").value("enabled").toBool(),"Disabling one offer changed sibling policy");
        const auto inventory=controller.inventory_;controller.setLocalOffers({});controller.setLocalOffers(inventory);
        require(!offer("front").value("enabled").toBool(),"Refresh or reconnecting hardware reset opt-out");
        controller.enableOffer("missing",false);require(!offer("missing").value("shareAllowed").toBool(),"Unavailable resource could not be restricted");
        controller.enableOffer("missing",true);require(offer("missing").value("shareAllowed").toBool()&&!offer("missing").value("enabled").toBool(),"Unavailable resource preference advertised unsupported media");
        controller.enableOffer("rear",false);require(controller.sharing(),"Disabling final offer stopped automatic listener");
        controller.startAvailableSharing("localhost",port);require(controller.enabled_.empty(),"Repeated startup ignored restrictions");
        controller.enableOffer("front",true);controller.enableOffer("rear",true);
        require(controller.enabled_.size()==2&&!QSettings().value("peers/disabledOffers").toStringList().contains("front"),"Explicit re-enable did not clear saved restriction");

        Service::Event event{};event.kind=Service::EventKind::granted;event.context.incoming=true;event.context.connection=21;event.context.grant=1;event.context.kind=Kind::camera;event.resource=rpc::Open{controller.service_->identity(),"front"};controller.applyEvent(event);
        event.context.connection=22;event.context.grant=2;controller.applyEvent(event);
        require(offer("front").value("sessionCount")==2&&!offer("rear").value("inUse").toBool(),"Fanout or same-kind exact resource activity incorrect");
        controller.setLocalOffers({{"rear","Rear camera",Kind::camera,true},{"plugged","New microphone",Kind::microphone,true}});
        require(offer("front").value("enabled").toBool()&&offer("front").value("sessionCount")==2&&offer("plugged").value("enabled").toBool(),"Hotplug revoked in-use resource or failed to add sibling");
        controller.enableOffer("front",false);require(!offer("front").value("enabled").toBool(),"Active snapshot bypassed explicit permission withdrawal");
        controller.enableOffer("front",true);
        event.context.incoming=false;event.context.connection=23;event.context.grant=3;controller.applyEvent(event);require(offer("front").value("sessionCount")==2,"Outgoing use incorrectly counted as incoming");
        const auto generation=controller.sharingGeneration_;controller.disconnectIncomingAll();require(controller.sharingGeneration_==generation&&controller.sharing(),"Disconnect incoming all withdrew availability");
        controller.connections_.clear();controller.devices_.clear();controller.pending_.clear();
        controller.setLocalOffers({{"new", "New camera", Kind::camera, true}});require(controller.enabled_.size()==1&&controller.enabled_.contains("new")&&controller.sharing(),"Updated inventory not automatically available");
        controller.enableOffer("new",false);
        controller.setLocalOffers({});require(controller.sharing(),"Empty inventory stopped discoverable device");controller.setGatewaySharing(true,{},0);controller.setGatewaySharing(false,{},0);require(controller.sharing(),"Disabling gateway withdrew empty automatic availability");
        controller.stopSharing();
    }
    static void run(PeerController& controller) {
        using namespace extrapane::peers;
        auto require=[](bool good,const char* why){if(!good)throw std::runtime_error(why);};
        DeviceId owner{};owner[0]=7;
        controller.connectionEndpoints_.insert("65",{{"address","127.0.0.1"},{"port",43824}});
        Service::Event connected{};connected.kind=Service::EventKind::connected;connected.context.connection=65;connected.context.peer=owner;controller.applyEvent(connected);
        require(controller.devices_.last().toMap().value("address")=="127.0.0.1"&&controller.devices_.last().toMap().value("port")==43824,"Connected device lost normalized endpoint metadata");
        connected.kind=Service::EventKind::disconnected;controller.applyEvent(connected);require(!controller.connectionEndpoints_.contains("65"),"Disconnected endpoint metadata retained");
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
        DeviceDirectoryCheck::run();
        QSettings().setValue("peers/offers",QStringList{"camera"});
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
        if(store.identity()!=identity)throw std::runtime_error("Startup replaced local identity");PeerControllerEventCheck::run(controller);PeerControllerEventCheck::sharing(controller);PeerControllerEventCheck::availability(controller);}
        {PeerController controller({},path);controller.setLocalOffers({{"new","Saved disabled camera",extrapane::peers::Kind::camera,true},{"added","New microphone",extrapane::peers::Kind::microphone,true}});
        QTcpServer reservation;if(!reservation.listen(QHostAddress::LocalHost,0))throw std::runtime_error("Port reservation failed");auto port=reservation.serverPort();reservation.close();controller.startAvailableSharing("localhost",port);
        for(const auto& value:controller.offers()){auto row=value.toMap();if(row.value("enabled").toBool()!=(row.value("id")=="added"))throw std::runtime_error("Restart lost restriction or disabled new resource");}}
        std::cout<<"Qt peer startup and resource sharing lifecycle pass with isolated loopback listeners, no media\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
