#include "device_directory.hpp"
#include <QNetworkDatagram>
#include <QHostAddress>
#include <QByteArray>
#include <algorithm>
#include <stdexcept>

namespace {
constexpr quint16 discoveryPort=43825;
const QHostAddress group(QStringLiteral("239.255.43.24"));
QVariantList kinds(unsigned mask){
    static const char* names[]{"Extended display","Camera","Microphone","USB","Speakers","Existing screen"};
    QVariantList result;for(unsigned i=0;i<6;++i)if(mask&(1u<<i))result.append(QString::fromLatin1(names[i]));return result;
}
}
DeviceDirectory::DeviceDirectory(QObject* parent):QObject(parent){
    clock_.start();timer_.setInterval(5000);searchTimer_.setSingleShot(true);searchTimer_.setInterval(10000);
    connect(&searchTimer_,&QTimer::timeout,this,&DeviceDirectory::finishSearch);
    connect(&timer_,&QTimer::timeout,this,&DeviceDirectory::refresh);
    connect(&socket_,&QUdpSocket::readyRead,this,&DeviceDirectory::receive);
}
DeviceDirectory::~DeviceDirectory(){stop();}
bool DeviceDirectory::start(unsigned selected){
    stop();std::set<std::uint32_t> indices;
    for(const auto& nic:QNetworkInterface::allInterfaces()){
        const auto flags=nic.flags();if(!(flags&QNetworkInterface::IsUp)||!(flags&QNetworkInterface::IsRunning)||!(flags&QNetworkInterface::CanMulticast)||(flags&QNetworkInterface::IsLoopBack))continue;
        if(selected&&nic.index()!=selected)continue;
        bool ipv4=false;for(const auto& address:nic.addressEntries())if(address.ip().protocol()==QAbstractSocket::IPv4Protocol&&!address.ip().isLoopback()&&!address.ip().isNull()){ipv4=true;break;}
        if(ipv4)interfaces_.append(nic);
    }
    if(interfaces_.isEmpty()){status_="No connected multicast interface. Add a device by address instead.";emit changed();return false;}
    if(!socket_.bind(QHostAddress::AnyIPv4,discoveryPort,QUdpSocket::ShareAddress|QUdpSocket::ReuseAddressHint)){
        status_="Discovery unavailable: "+socket_.errorString()+". Add a device by address instead.";interfaces_.clear();emit changed();return false;
    }
    socket_.setSocketOption(QAbstractSocket::MulticastTtlOption,1);
    socket_.setSocketOption(QAbstractSocket::MulticastLoopbackOption,0);
    QList<QNetworkInterface> joined;
    for(const auto& nic:interfaces_)if(socket_.joinMulticastGroup(group,nic)){indices.insert(nic.index());joined.append(nic);}
    interfaces_=joined;table_.interfaces(std::move(indices));
    if(interfaces_.isEmpty()){socket_.close();status_="Network discovery could not join this interface. Add a device by address instead.";emit changed();return false;}
    status_="Refresh to find nearby devices";timer_.start();refresh();emit changed();return true;
}
bool DeviceDirectory::search(unsigned selected){
    if(!transportActive()&&!start(selected))return false;
    // Drain queued beacons before the new search begins, so an earlier idle
    // observation cannot become a fresh discovery result.
    for(unsigned drained=0;socket_.hasPendingDatagrams()&&drained<64;++drained)socket_.receiveDatagram(1200);
    if(socket_.hasPendingDatagrams()){status_="Discovery traffic is busy. Try Refresh again.";emit changed();return false;}
    beginSearch();return true;
}
void DeviceDirectory::beginSearch(){table_.clear();devices_.clear();searchTimer_.start();status_="Looking for nearby devices...";emit changed();}
void DeviceDirectory::finishSearch(){searchTimer_.stop();publish();status_=devices_.empty()?"No nearby devices found. Refresh to search again.":"Search complete. Refresh to search again.";emit changed();}
void DeviceDirectory::stop(){
    withdraw();timer_.stop();searchTimer_.stop();for(const auto& nic:interfaces_)socket_.leaveMulticastGroup(group,nic);
    socket_.close();interfaces_.clear();table_.clear();devices_.clear();status_="Discovery stopped";emit changed();
}
void DeviceDirectory::advertise(extrapane::peers::Announcement value){
    // Codec validation also bounds name, lifetime, offered kinds and identity.
    extrapane::peers::encode(value);
    if(announcement_&&(announcement_->device!=value.device||announcement_->instance!=value.instance))withdraw();
    if(announcement_){if(announcement_->sequence==UINT64_MAX)throw std::runtime_error("Discovery sequence exhausted; restart sharing");value.sequence=std::max(value.sequence,announcement_->sequence+1);}
    announcement_=std::move(value);refresh();
}
void DeviceDirectory::withdraw(){
    if(announcement_){auto& a=*announcement_;if(a.sequence!=UINT64_MAX)send(extrapane::peers::encode(extrapane::peers::Withdrawal{a.device,a.instance,++a.sequence}));announcement_.reset();}
}
void DeviceDirectory::send(const extrapane::peers::Bytes& bytes){
    if(!transportActive())return;
    for(const auto& nic:interfaces_){socket_.setMulticastInterface(nic);socket_.writeDatagram(reinterpret_cast<const char*>(bytes.data()),static_cast<qint64>(bytes.size()),group,discoveryPort);}
}
void DeviceDirectory::refresh(){
    if(announcement_&&transportActive()){
        if(announcement_->sequence==UINT64_MAX){withdraw();status_="Discovery instance exhausted; restart sharing";}
        else{++announcement_->sequence;send(extrapane::peers::encode(*announcement_));}
    }
    publish();
}
unsigned DeviceDirectory::observedInterface(const QNetworkDatagram& datagram)const{
    for(const auto& nic:interfaces_)if(nic.index()==datagram.interfaceIndex())return nic.index();
    // Some socket backends omit packet-interface metadata. Accept only a unique
    // selected subnet matching the observed source, never an address in the payload.
    if(datagram.interfaceIndex())return 0;
    unsigned result=0;
    for(const auto& nic:interfaces_)for(const auto& entry:nic.addressEntries()){
        if(entry.ip().protocol()!=QAbstractSocket::IPv4Protocol||entry.prefixLength()<0||!datagram.senderAddress().isInSubnet(entry.ip(),entry.prefixLength()))continue;
        if(result&&result!=nic.index())return 0;result=nic.index();
    }
    return result;
}
void DeviceDirectory::receive(){
    // Yield back to UI/connection work even under unsolicited datagram floods.
    unsigned handled=0;
    while(socket_.hasPendingDatagrams()&&handled++<64){
        const auto datagram=socket_.receiveDatagram(1200);
        if(datagram.data().size()>512||datagram.data().size()<8||datagram.senderAddress().protocol()!=QAbstractSocket::IPv4Protocol)continue;
        const auto nic=observedInterface(datagram);if(!nic)continue;
        observeMessage(datagram.data(),datagram.senderAddress().toString(),nic);
    }
    publish();
    if(socket_.hasPendingDatagrams())QTimer::singleShot(10,this,&DeviceDirectory::receive);
}
void DeviceDirectory::observeMessage(const QByteArray& raw,const QString& sender,unsigned nic){
    const std::span bytes(reinterpret_cast<const std::uint8_t*>(raw.constData()),static_cast<std::size_t>(raw.size()));
    try{
        auto value=extrapane::peers::decode_announcement(bytes);
        if(!scanning()||(announcement_&&value.device==announcement_->device))return;
        table_.observe(value,sender.toStdString(),nic,clock_.elapsed());
    }catch(const std::exception&){
        // Withdrawals and normal expiry may remove an earlier search result
        // while idle; no idle announcement adds or refreshes a result.
        try{auto value=extrapane::peers::decode_withdrawal(bytes);table_.withdraw(value,sender.toStdString(),nic);}catch(const std::exception&){}
    }
}
void DeviceDirectory::publish(){
    QVariantList next;
    for(const auto& route:table_.snapshot(clock_.elapsed())){
        const auto& a=route.announcement;
        next.append(QVariantMap{{"id",QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(a.device.data()),a.device.size()).toHex())},
            {"name",QString::fromStdString(a.name)},{"address",QString::fromStdString(route.observed_ipv4)},
            {"port",a.port},{"interfaceIndex",route.interface_index},{"resources",kinds(a.offered_kinds)},
            {"pairingRequired",a.pairing_required},{"verified",false}});
    }
    if(next!=devices_){devices_=std::move(next);emit changed();}
}
