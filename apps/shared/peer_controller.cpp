#include "peer_controller.hpp"
#include <QSettings>
#include <QHostAddress>
#include <QSysInfo>
#include <QDir>
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
    QSettings settings;pairingRequired_=settings.value("peers/pairingRequired",true).toBool();for(const auto& id:settings.value("peers/offers").toStringList())enabled_.insert(id);
    if(!QDir().mkpath(path))throw std::runtime_error("Cannot create private device settings directory");
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
QVariantList PeerController::offers()const{QVariantList result;for(const auto& offer:inventory_){auto item=offerMap(offer);item["enabled"]=enabled_.contains(QString::fromStdString(offer.id));result.push_back(item);}return result;}
QVariantList PeerController::remembered()const{QVariantList result;if(!service_)return result;for(const auto& device:service_->remembered())result.push_back(QVariantMap{{"id",hex(device.id)},{"name",device.name.empty()?hex(device.id).left(12):QString::fromStdString(device.name)},{"address",QString::fromStdString(device.address_hint)},{"port",device.port_hint}});return result;}
void PeerController::closeIfIdle(const QString& connection){
    if(std::any_of(connections_.begin(),connections_.end(),[&](const QVariant& value){return value.toMap().value("connection").toString()==connection;}))return;
    for(const auto& pending:pending_)if(pending.value("connection").toString()==connection)return;
    if(service_)service_->disconnect(connection.toULongLong());
}
void PeerController::setLocalOffers(std::vector<Offer> offers){inventory_=std::move(offers);if(service_)service_->offers(selectedOffers());if(sharing_)announce(listenPort_);emit changed();}
void PeerController::enableOffer(QString id,bool enabled){
    const auto found=std::find_if(inventory_.begin(),inventory_.end(),[&](const auto& offer){return QString::fromStdString(offer.id)==id;});if(found==inventory_.end()||(enabled&&!found->available)){error("This resource is unavailable");return;}
    if(enabled)enabled_.insert(id);else enabled_.remove(id);QSettings().setValue("peers/offers",QStringList(enabled_.begin(),enabled_.end()));try{service_->offers(selectedOffers());if(sharing_)announce(listenPort_);emit changed();}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}
}
void PeerController::setGatewayEnabled(bool enabled){try{service_->gateway(enabled);gatewayEnabled_=enabled;emit changed();}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::setPairingRequired(bool value){try{service_->pairing_required(value);pairingRequired_=value;QSettings().setValue("peers/pairingRequired",value);if(sharing_)announce(listenPort_);emit changed();}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::startSharing(QString bind,unsigned port,unsigned interfaceIndex){try{if(!port||port>65535)throw std::invalid_argument("Enter a port between 1 and 65535");const auto local=address(bind);interfaceIndex_=interfaceIndex;loopbackSharing_=QHostAddress(local).isLoopback();
    if(!loopbackSharing_){bool found=false;for(const auto& item:networkAddresses()){const auto nic=item.toMap();if(nic.value("address").toString()==local&&(!interfaceIndex||nic.value("interfaceIndex").toUInt()==interfaceIndex)){interfaceIndex_=nic.value("interfaceIndex").toUInt();found=true;break;}}if(!found)throw std::invalid_argument("Select a connected local network address");}
    announcementInstance_=QRandomGenerator::global()->generate64()|1;announcementSequence_=0;service_->start(local.toStdString(),static_cast<std::uint16_t>(port));status_="Starting sharing...";emit changed();}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::stopSharing(){directory_.withdraw();service_->stop();sharing_=false;listenPort_=0;status_="Sharing stopped";emit changed();}
void PeerController::connectDevice(QString host,unsigned port,QString code,QString expected,QString local,bool open){try{
    const auto destination=address(host);if(!port||port>65535)throw std::invalid_argument("Enter a port between 1 and 65535");if(!code.isEmpty()&&(code.size()!=6||std::any_of(code.begin(),code.end(),[](auto c){return c<'0'||c>'9';})))throw std::invalid_argument("Enter the six-digit pairing code");
    Service::ConnectOptions options{destination.toStdString(),static_cast<std::uint16_t>(port),local.isEmpty()?std::string{}:address(local).toStdString(),expected.isEmpty()?DeviceId{}:identity(expected),code.toStdString(),open};
    service_->connect(std::move(options));status_="Connecting to "+destination+":"+QString::number(port);emit changed();
}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::disconnectDevice(QString connection){try{service_->disconnect(connection.toULongLong());}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::forgetDevice(QString id){try{service_->forget(identity(id));}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::useResource(QString connection,QString owner,QString resource,bool input,unsigned width,unsigned height,unsigned refresh){try{
    rpc::Open request{identity(owner),resource.toStdString(),input,width,height,refresh};rpc::encode(request);
    const auto id=service_->open(connection.toULongLong(),request);pending_.insert(token(id),{{"connection",connection},{"owner",owner},{"resource",resource},{"width",width},{"height",height},{"refresh",refresh},{"phase","connecting"}});emit changed();
}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::stopResource(QString connection,QString grant){try{service_->close(connection.toULongLong(),grant.toULongLong());}catch(const std::exception& e){error(QString::fromUtf8(e.what()));}}
void PeerController::disconnectAll(){for(const auto& item:devices_)disconnectDevice(item.toMap().value("connection").toString());}
void PeerController::announce(unsigned port){
    if(loopbackSharing_){directory_.withdraw();return;}
    if(!directory_.scanning())directory_.start(interfaceIndex_);Announcement announcement;announcement.device=service_->identity();announcement.instance=announcementInstance_;announcement.sequence=++announcementSequence_;announcement.name=name_.toStdString();announcement.port=static_cast<std::uint16_t>(port);announcement.pairing_required=pairingRequired_;for(const auto& offer:selectedOffers())if(offer.available)announcement.offered_kinds|=1u<<static_cast<unsigned>(offer.kind);directory_.advertise(announcement);
}
void PeerController::poll(){if(!service_)return;for(auto& event:service_->poll()){
    const auto connection=token(event.context.connection);const auto grant=token(event.context.grant);
    switch(event.kind){
    case Service::EventKind::listening:sharing_=true;listenPort_=event.port;status_="Sharing enabled";if(!loopbackSharing_)directory_.start(interfaceIndex_);announce(event.port);break;
    case Service::EventKind::connected:{QVariantMap item{{"connection",connection},{"identity",hex(event.context.peer)},{"incoming",event.context.incoming},{"verified",event.verified},{"name",hex(event.context.peer).left(12)},{"resources",QVariantList{}}};devices_.push_back(item);if(!event.context.incoming)service_->catalog(event.context.connection);status_="Device connected";break;}
    case Service::EventKind::catalog:for(auto& item:devices_){auto device=item.toMap();if(device.value("connection").toString()!=connection)continue;QVariantList resources;for(const auto& catalog:event.catalogs){device["name"]=QString::fromStdString(catalog.name);for(const auto& offer:catalog.offers){auto row=offerMap(offer);row["owner"]=hex(catalog.device);resources.push_back(row);}}device["resources"]=resources;item=device;}break;
    case Service::EventKind::granted:{auto item=pending_.take(token(event.context.request));if(event.resource){item["resource"]=QString::fromStdString(event.resource->resource);item["owner"]=hex(event.resource->owner);item["width"]=event.resource->width;item["height"]=event.resource->height;item["refresh"]=event.resource->refresh;}item["kind"]=kindName(event.context.kind);item["connection"]=connection;item["grant"]=grant;item["phase"]="active";item["incoming"]=event.context.incoming;connections_.push_back(item);emit toast("Resource connected");break;}
    case Service::EventKind::stopped:connections_.removeIf([&](const QVariant& value){auto row=value.toMap();return row.value("connection").toString()==connection&&row.value("grant").toString()==grant;});closeIfIdle(connection);break;
    case Service::EventKind::disconnected:for(auto it=pending_.begin();it!=pending_.end();)if(it.value().value("connection").toString()==connection)it=pending_.erase(it);else ++it;devices_.removeIf([&](const QVariant& value){return value.toMap().value("connection").toString()==connection;});connections_.removeIf([&](const QVariant& value){return value.toMap().value("connection").toString()==connection;});break;
    case Service::EventKind::error:{const bool resourceFailed=pending_.remove(token(event.context.request));error(QString::fromStdString(event.detail));if(resourceFailed)closeIfIdle(connection);break;}
    case Service::EventKind::trust_changed:break;
    }emit changed();
}}
void PeerController::shutdown(){timer_.stop();directory_.stop();service_.reset();sharing_=false;devices_.clear();connections_.clear();pending_.clear();}
