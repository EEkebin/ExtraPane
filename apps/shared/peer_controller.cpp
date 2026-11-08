#include "peer_controller.hpp"
#include <QSettings>
#include <QHostAddress>
#include <QSysInfo>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <algorithm>
#include <stdexcept>
using namespace extrapane::peers;
namespace {
QString hex(const DeviceId& id){return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(id.data()),32).toHex());}
DeviceId identity(QString value){
    if(value.size()!=64)throw std::invalid_argument("Invalid device identity");
    for(const auto c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))throw std::invalid_argument("Invalid device identity");
    const auto data=QByteArray::fromHex(value.toLatin1());DeviceId id{};std::copy(data.begin(),data.end(),id.begin());if(!valid_id(id))throw std::invalid_argument("Invalid device identity");return id;
}
QString address(QString value){value=value.trimmed();if(value.compare("localhost",Qt::CaseInsensitive)==0)return "127.0.0.1";QHostAddress parsed(value);if(parsed.protocol()!=QAbstractSocket::IPv4Protocol||parsed.isMulticast()||(parsed.toIPv4Address()>>24)==0||(parsed.toIPv4Address()>>24)>=224)throw std::invalid_argument("Enter a reachable IPv4 address or localhost");return parsed.toString();}
QString kindName(Kind kind){switch(kind){case Kind::display_destination:return "Extend desktop";case Kind::existing_screen:return "Existing screen";case Kind::camera:return "Camera";case Kind::microphone:return "Microphone";case Kind::speaker:return "Speakers";case Kind::usb:return "USB storage";}return {};}
QVariantMap offerMap(const Offer& offer){return {{"id",QString::fromStdString(offer.id)},{"label",QString::fromStdString(offer.label)},{"kind",kindName(offer.kind)},{"kindId",static_cast<int>(offer.kind)},{"available",offer.available},{"inputAllowed",offer.input_allowed},{"reason",QString::fromStdString(offer.unavailable_reason)},{"width",offer.width},{"height",offer.height},{"refresh",offer.refresh}};}
QString token(std::uint64_t value){return QString::number(value);}
}
PeerController::PeerController(Service::Hooks hooks,QString path,QObject* parent):QObject(parent),directory_(this),timer_(this){
    announcementInstance_=QRandomGenerator::global()->generate64()|1;
    name_=QSysInfo::machineHostName();if(name_.isEmpty())name_="ExtraPane device";
    QSettings settings;pairingRequired_=settings.value("peers/pairingRequired",true).toBool(); // Offers are opt-in on every app startup.
    // TrustStore creates and verifies the leaf with its own user-private ACL /
    // mode. Qt's default directory permissions are not suitable for that leaf.
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))throw std::runtime_error("Cannot create device settings parent directory");
    #ifdef _WIN32
    const auto privatePath=std::filesystem::path(path.toStdWString());
#else
    const auto privatePath=std::filesystem::u8path(path.toUtf8().toStdString());
#endif
    service_=std::make_unique<Service>(Service::Options{privatePath,name_.toStdString(),{},pairingRequired_},std::move(hooks));
    connect(&timer_,&QTimer::timeout,this,&PeerController::poll);timer_.start(25);
}
PeerController::~PeerController(){shutdown();}
QString PeerController::pairingCode()const{return service_&&pairingRequired_?QString::fromStdString(service_->pairing_code()):QString{};}
void PeerController::error(const QString& message){status_=message;emit changed();emit toast(message);}
std::vector<Offer> PeerController::selectedOffers()const{std::vector<Offer> result;for(const auto& offer:inventory_)if(enabled_.contains(QString::fromStdString(offer.id)))result.push_back(offer);return result;}
QVariantList PeerController::networkAddresses()const{
    QVariantList result;for(const auto& nic:QNetworkInterface::allInterfaces()){
        const auto flags=nic.flags();if(!(flags&QNetworkInterface::IsUp)||!(flags&QNetworkInterface::IsRunning)||(flags&QNetworkInterface::IsLoopBack))continue;
        for(const auto& entry:nic.addressEntries())if(entry.ip().protocol()==QAbstractSocket::IPv4Protocol&&!entry.ip().isLoopback()&&!entry.ip().isNull())result.push_back(QVariantMap{{"label",nic.humanReadableName()+" ? "+entry.ip().toString()},{"address",entry.ip().toString()},{"interfaceIndex",nic.index()}});
    }return result;
}
QVariantList PeerController::offers()const{QVariantList result;for(const auto& offer:inventory_){
    auto item=offerMap(offer);const auto id=QString::fromStdString(offer.id);item["enabled"]=enabled_.contains(id);
    unsigned count=0;QStringList users;
    for(const auto& value:connections_){const auto row=value.toMap();if(!row.value("incoming").toBool()||row.value("resource").toString()!=id||row.value("owner").toString()!=hex(service_->identity()))continue;
        ++count;for(const auto& device:devices_){const auto peer=device.toMap();if(peer.value("connection")==row.value("connection")){const auto name=peer.value("name").toString();if(!users.contains(name))users.append(name);}}
    }
    item["inUse"]=count>0;item["sessionCount"]=count;item["users"]=users;result.push_back(item);
}return result;}
QVariantList PeerController::remembered()const{QVariantList result;if(!service_)return result;for(const auto& device:service_->remembered())result.push_back(QVariantMap{{"id",hex(device.id)},{"name",device.name.empty()?hex(device.id).left(12):QString::fromStdString(device.name)},{"address",QString::fromStdString(device.address_hint)},{"port",device.port_hint}});return result;}
void PeerController::setLocalOffers(std::vector<Offer> offers){
    inventory_=std::move(offers);
    if(automaticAvailability_)for(const auto& offer:inventory_)if(offer.available)enabled_.insert(QString::fromStdString(offer.id));
    for(auto it=enabled_.begin();it!=enabled_.end();) {
        const bool available=std::any_of(inventory_.begin(),inventory_.end(),[&](const auto& offer){return QString::fromStdString(offer.id)==*it&&offer.available;});
        if(!available)it=enabled_.erase(it);else ++it;
    }
    if(service_)service_->offers(selectedOffers());
    if(!automaticAvailability_&&enabled_.isEmpty()&&!gatewayEnabled_&&(sharing_||sharingStarting_)){stopSharing();return;}
    if(sharing_)announce(listenPort_);emit changed();
}
void PeerController::refreshOffers(){emit refreshRequested();}
void PeerController::startDiscovery(){if(!directory_.scanning())directory_.start(interfaceIndex_);}
void PeerController::startAvailableSharing(QString bind,unsigned port){
    automaticAvailability_=true;
    try{
        if(bind.trimmed().isEmpty()){const auto addresses=networkAddresses();if(addresses.empty())throw std::invalid_argument("No connected local network address. Connect a network and refresh availability.");bind=addresses.first().toMap().value("address").toString();}
        // Validate the requested endpoint before withdrawing an existing listener.
        bind=address(bind);if(!port||port>65535)throw std::invalid_argument("Enter a port between 1 and 65535");
        if(!QHostAddress(bind).isLoopback()){bool found=false;for(const auto& nic:networkAddresses())if(nic.toMap().value("address").toString()==bind){found=true;break;}if(!found)throw std::invalid_argument("Select a connected local network address");}
        if(sharing_||sharingStarting_){
            if(bind==bindAddress_&&port==configuredPort_)return;
            if(!connections_.empty()||!pending_.empty())throw std::invalid_argument("Disconnect active resources before changing the listening address");
            stopSharing();
        }
        enabled_.clear();for(const auto& offer:inventory_)if(offer.available)enabled_.insert(QString::fromStdString(offer.id));
        service_->offers(selectedOffers());beginSharing(bind,port);emit changed();
    }catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}
void PeerController::stopIncomingResource(QString id){
    for(const auto& value:connections_){const auto row=value.toMap();if(row.value("incoming").toBool()&&row.value("resource").toString()==id&&row.value("owner").toString()==hex(service_->identity()))stopResource(row.value("connection").toString(),row.value("grant").toString());}
}
void PeerController::disconnectIncomingAll(){
    for(const auto& value:connections_){const auto row=value.toMap();if(row.value("incoming").toBool())stopResource(row.value("connection").toString(),row.value("grant").toString());}
}
void PeerController::enableOffer(QString id,bool enabled){setOfferSharing(id,enabled,bindAddress_,configuredPort_);}
void PeerController::setOfferSharing(QString id,bool enabled,QString bind,unsigned port){
    const auto found=std::find_if(inventory_.begin(),inventory_.end(),[&](const auto& offer){return QString::fromStdString(offer.id)==id;});
    if(found==inventory_.end()||(enabled&&!found->available)){error("This resource is unavailable");return;}
    if(enabled_.contains(id)==enabled)return;
    const bool starting=enabled&&!sharing_&&!sharingStarting_;
    try{
        if(starting)beginSharing(bind,port);
        if(enabled)enabled_.insert(id);else enabled_.remove(id);
        service_->offers(selectedOffers());
        if(enabled_.isEmpty()&&!gatewayEnabled_){stopSharing();return;}
        if(sharing_)announce(listenPort_);emit changed();
    }catch(const std::exception& e){
        if(enabled)enabled_.remove(id);else enabled_.insert(id);
        if(starting)stopSharing();error(QString::fromUtf8(e.what()));
    }
}
void PeerController::setGatewayEnabled(bool enabled){setGatewaySharing(enabled,bindAddress_,configuredPort_);}
void PeerController::setGatewaySharing(bool enabled,QString bind,unsigned port){
    if(gatewayEnabled_==enabled)return;
    const bool starting=enabled&&!sharing_&&!sharingStarting_;
    try{
        if(starting)beginSharing(bind,port);
        service_->gateway(enabled);gatewayEnabled_=enabled;
        if(!automaticAvailability_&&!enabled&&enabled_.isEmpty()){stopSharing();return;}
        if(sharing_)announce(listenPort_);emit changed();
    }catch(const std::exception& e){if(starting)stopSharing();error(QString::fromUtf8(e.what()));}
}
void PeerController::setPairingRequired(bool value){try{service_->pairing_required(value);pairingRequired_=value;QSettings().setValue("peers/pairingRequired",value);if(sharing_)announce(listenPort_);emit changed();}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::beginSharing(QString bind,unsigned port,unsigned interfaceIndex){
    if(!port||port>65535)throw std::invalid_argument("Enter a port between 1 and 65535");
    const auto local=address(bind);const bool loopback=QHostAddress(local).isLoopback();
    if(!loopback){bool found=false;for(const auto& item:networkAddresses()){const auto nic=item.toMap();if(nic.value("address").toString()==local&&(!interfaceIndex||nic.value("interfaceIndex").toUInt()==interfaceIndex)){interfaceIndex=nic.value("interfaceIndex").toUInt();found=true;break;}}if(!found)throw std::invalid_argument("Select a connected local network address");}
    const auto generation=service_->start(local.toStdString(),static_cast<std::uint16_t>(port));
    sharingGeneration_=generation;sharingStarting_=true;bindAddress_=local;configuredPort_=port;interfaceIndex_=interfaceIndex;loopbackSharing_=loopback;
    announcementInstance_=QRandomGenerator::global()->generate64()|1;announcementSequence_=0;status_="Starting sharing...";
}
void PeerController::startSharing(QString bind,unsigned port,unsigned interfaceIndex){try{if(enabled_.isEmpty()&&!gatewayEnabled_)throw std::invalid_argument("Choose a resource to share first");if(!sharing_&&!sharingStarting_)beginSharing(bind,port,interfaceIndex);emit changed();}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::stopSharing(){
    directory_.withdraw();sharingGeneration_=0;sharing_=false;sharingStarting_=false;listenPort_=0;enabled_.clear();gatewayEnabled_=false;
    service_->offers({});service_->gateway(false);service_->stop();status_="Sharing stopped";emit changed();
}
void PeerController::connectDevice(QString host,unsigned port,QString code,QString expected,QString local,bool open){try{
    const auto destination=address(host);if(!port||port>65535)throw std::invalid_argument("Enter a port between 1 and 65535");if(!code.isEmpty()&&(code.size()!=6||std::any_of(code.begin(),code.end(),[](auto c){return c<'0'||c>'9';})))throw std::invalid_argument("Enter the six-digit pairing code");
    Service::ConnectOptions options{destination.toStdString(),static_cast<std::uint16_t>(port),local.isEmpty()?std::string{}:address(local).toStdString(),expected.isEmpty()?DeviceId{}:identity(expected),code.toStdString(),open};
    service_->connect(std::move(options));status_="Connecting to "+destination+":"+QString::number(port);emit changed();
}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::disconnectDevice(QString connection){try{service_->disconnect(connection.toULongLong());}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::forgetDevice(QString id){try{service_->forget(identity(id));}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::useResource(QString connection,QString owner,QString resource,bool input,unsigned width,unsigned height,unsigned refresh){try{
    rpc::Open request{identity(owner),resource.toStdString(),input,width,height,refresh};rpc::encode(request);
    QVariantMap pending{{"connection",connection},{"owner",owner},{"resource",resource},{"width",width},{"height",height},{"refresh",refresh},{"phase","connecting"}};
    for(const auto& device:devices_)if(device.toMap().value("connection")==connection)for(const auto& candidate:device.toMap().value("resources").toList()){
        const auto offer=candidate.toMap();if(offer.value("id")==resource&&offer.value("owner")==owner){pending["label"]=offer.value("label");pending["kind"]=offer.value("kind");pending["kindId"]=offer.value("kindId");}
    }
    const auto id=service_->open(connection.toULongLong(),request);pending_.insert(token(id),pending);emit changed();
}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
QVariantList PeerController::connections()const{auto rows=connections_;for(auto it=pending_.cbegin();it!=pending_.cend();++it){auto row=it.value();row["request"]=it.key();row["grant"]="0";if(!row.contains("label"))row["label"]="Resource";for(const auto& device:devices_)if(device.toMap().value("connection")==row.value("connection"))for(const auto& candidate:device.toMap().value("resources").toList()){const auto offer=candidate.toMap();if(offer.value("id")==row.value("resource")&&offer.value("owner")==row.value("owner")){row["label"]=offer.value("label");row["kind"]=offer.value("kind");row["kindId"]=offer.value("kindId");}}rows.push_back(row);}return rows;}
void PeerController::cancelResource(QString connection,QString request){try{if(pending_.value(request).value("connection").toString()!=connection)return;service_->cancel(connection.toULongLong(),request.toULongLong());pending_.remove(request);emit changed();}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::stopResource(QString connection,QString grant){try{service_->close(connection.toULongLong(),grant.toULongLong());
    connections_.removeIf([&](const QVariant& value){const auto row=value.toMap();return row.value("connection").toString()==connection&&row.value("grant").toString()==grant&&row.value("phase")=="reconnecting";});emit changed();}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::disconnectAll(){for(const auto& item:devices_)disconnectDevice(item.toMap().value("connection").toString());}
void PeerController::announce(unsigned port){
    if(loopbackSharing_){directory_.withdraw();return;}
    if(!directory_.scanning())directory_.start(interfaceIndex_);Announcement announcement;announcement.device=service_->identity();announcement.instance=announcementInstance_;announcement.sequence=++announcementSequence_;announcement.name=name_.toStdString();announcement.port=static_cast<std::uint16_t>(port);announcement.pairing_required=pairingRequired_;for(const auto& offer:selectedOffers())if(offer.available)announcement.offered_kinds|=1u<<static_cast<unsigned>(offer.kind);directory_.advertise(announcement);
}
void PeerController::poll(){if(!service_)return;for(auto& event:service_->poll())applyEvent(event);}
void PeerController::applyEvent(Service::Event& event){
    const auto connection=token(event.context.connection);const auto grant=token(event.context.grant);
    switch(event.kind){
    case Service::EventKind::listening:if(!sharingGeneration_||event.sharing_generation!=sharingGeneration_||(!automaticAvailability_&&!gatewayEnabled_&&enabled_.isEmpty()))return;sharingStarting_=false;sharing_=true;listenPort_=event.port;status_="Available to paired devices";if(!loopbackSharing_)directory_.start(interfaceIndex_);announce(event.port);break;
    case Service::EventKind::connected:{QString peerName=hex(event.context.peer).left(12);for(const auto& saved:service_->remembered())if(saved.id==event.context.peer&&!saved.name.empty()){peerName=QString::fromStdString(saved.name);break;}devices_.removeIf([&](const QVariant& value){return value.toMap().value("connection").toString()==connection;});QVariantMap item{{"connection",connection},{"identity",hex(event.context.peer)},{"incoming",event.context.incoming},{"verified",event.verified},{"name",peerName},{"resources",QVariantList{}}};devices_.push_back(item);if(!event.context.incoming)service_->catalog(event.context.connection);status_="Device connected";break;}
    case Service::EventKind::reconnecting:for(auto& value:connections_){auto row=value.toMap();if(row.value("connection").toString()==connection&&row.value("kind").toString()!=kindName(Kind::usb)){row["phase"]="reconnecting";value=row;}}status_="Connection interrupted. Reconnecting...";break;
    case Service::EventKind::catalog:for(auto& item:devices_){auto device=item.toMap();if(device.value("connection").toString()!=connection)continue;QVariantList resources;for(const auto& catalog:event.catalogs){if(catalog.device==event.context.peer)device["name"]=QString::fromStdString(catalog.name);for(const auto& offer:catalog.offers){auto row=offerMap(offer);row["owner"]=hex(catalog.device);row["ownerName"]=QString::fromStdString(catalog.name);row["delegated"]=catalog.device!=event.context.peer;resources.push_back(row);}}device["resources"]=resources;item=device;}break;
    case Service::EventKind::granted:{QVariantMap item;const auto request=token(event.context.request);if(!event.context.incoming&&pending_.value(request).value("connection").toString()==connection)item=pending_.take(request);if(event.resource){item["resource"]=QString::fromStdString(event.resource->resource);item["owner"]=hex(event.resource->owner);item["width"]=event.resource->width;item["height"]=event.resource->height;item["refresh"]=event.resource->refresh;}item["kind"]=kindName(event.context.kind);item["kindId"]=static_cast<int>(event.context.kind);item["label"]=item["kind"];
        if(event.resource){for(const auto& offer:inventory_)if(event.context.incoming&&offer.id==event.resource->resource)item["label"]=QString::fromStdString(offer.label);
        for(const auto& device:devices_)if(device.toMap().value("connection").toString()==connection)for(const auto& candidate:device.toMap().value("resources").toList()){const auto row=candidate.toMap();if(row.value("id").toString()==QString::fromStdString(event.resource->resource)&&row.value("owner").toString()==hex(event.resource->owner))item["label"]=row.value("label");}}
        for(auto it=connections_.begin();it!=connections_.end();++it){const auto row=it->toMap();if(row.value("connection").toString()==connection&&row.value("phase")=="reconnecting"&&row.value("grant").toString()==token(event.replaces_grant)){connections_.erase(it);break;}}
        item["connection"]=connection;item["grant"]=grant;item["phase"]="active";item["incoming"]=event.context.incoming;connections_.push_back(item);emit toast("Resource connected");break;}
    case Service::EventKind::stopped:if(!event.context.incoming&&pending_.value(token(event.context.request)).value("connection").toString()==connection)pending_.remove(token(event.context.request));connections_.removeIf([&](const QVariant& value){auto row=value.toMap();return row.value("connection").toString()==connection&&row.value("grant").toString()==grant&&(row.value("phase")!="reconnecting"||event.detail=="Reconnect ended"||event.detail=="Reconnect cancelled");});break;
    case Service::EventKind::disconnected:for(auto it=pending_.begin();it!=pending_.end();)if(it.value().value("connection").toString()==connection)it=pending_.erase(it);else ++it;devices_.removeIf([&](const QVariant& value){return value.toMap().value("connection").toString()==connection;});connections_.removeIf([&](const QVariant& value){return value.toMap().value("connection").toString()==connection;});break;
    case Service::EventKind::error:{if(event.sharing_generation){if(event.sharing_generation!=sharingGeneration_)return;stopSharing();}if(event.context.grant)connections_.removeIf([&](const QVariant& value){const auto row=value.toMap();return row.value("connection").toString()==connection&&row.value("grant").toString()==grant&&row.value("phase")=="reconnecting";});const auto request=token(event.context.request);if(!event.context.incoming&&pending_.value(request).value("connection").toString()==connection)pending_.remove(request);error(QString::fromStdString(event.detail));break;}
    case Service::EventKind::trust_changed:break;
    }emit changed();
}
void PeerController::shutdown(){timer_.stop();directory_.stop();service_.reset();sharing_=false;sharingStarting_=false;sharingGeneration_=0;gatewayEnabled_=false;enabled_.clear();devices_.clear();connections_.clear();pending_.clear();}
