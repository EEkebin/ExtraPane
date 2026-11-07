#include "connection_model.hpp"
#include <QHostAddress>
#include <QUuid>
#include <QSet>
#include <cmath>

namespace {
bool integer(const QVariant& value,int low,int high){
    bool ok=false;const double number=value.toDouble(&ok);
    return ok&&std::isfinite(number)&&std::floor(number)==number&&number>=low&&number<=high;
}
bool keys(const QVariantMap& map,const QSet<QString>& allowed){
    for(auto it=map.begin();it!=map.end();++it)if(!allowed.contains(it.key()))return false;
    return true;
}
bool ipv4(const QString& text,bool local){
    QHostAddress address;if(!address.setAddress(text)||address.protocol()!=QAbstractSocket::IPv4Protocol)return false;
    const auto number=address.toIPv4Address();
    return (local&&number==0)||(number!=0&&number!=0xffffffffu&&!address.isMulticast()&&!address.isLoopback());
}
}
ConnectionModel::ConnectionModel(QObject* parent):QAbstractListModel(parent){}
int ConnectionModel::rowCount(const QModelIndex& parent)const{return parent.isValid()?0:static_cast<int>(rows_.size());}
int ConnectionModel::activeConnectionCount()const{int count=0;for(const auto& row:rows_)if(row.state==State::connecting||row.state==State::active||row.state==State::returning)++count;return count;}
bool ConnectionModel::hasReservedResources()const{for(const auto& row:rows_)if(busy(row.state))return true;return false;}
bool ConnectionModel::busy(State state){return state==State::connecting||state==State::active||state==State::returning||state==State::recovery;}
QString ConnectionModel::phase(State state){switch(state){case State::idle:return "idle";case State::connecting:return "connecting";case State::active:return "active";case State::returning:return "returning";case State::recovery:return "recovery";case State::failed:return "failed";}return {};}
int ConnectionModel::find(const QString& id)const{for(int i=0;i<rowCount();++i)if(rows_[i].id==id)return i;return -1;}
QVariantMap ConnectionModel::effective(const Row& row)const{return busy(row.state)?row.heldTarget:singleServer_?sharedTarget_:row.target;}
QVariant ConnectionModel::data(const QModelIndex& index,int role)const{
    if(!index.isValid()||index.row()<0||index.row()>=rowCount()||index.column()!=0)return {};
    const auto& row=rows_[index.row()];
    switch(role){case Id:return row.id;case Kind:return row.kind;case Enabled:return row.enabled;case Source:return row.source;
    case Target:return row.target;case EffectiveTarget:return effective(row);case DisplayMode:return row.display;case Phase:return phase(row.state);
    case Status:return row.status;case Busy:return busy(row.state);case DestinationVisible:return !singleServer_;default:return {};}
}
QHash<int,QByteArray> ConnectionModel::roleNames()const{return {{Id,"connectionId"},{Kind,"resourceKind"},{Enabled,"connectionEnabled"},{Source,"sourceId"},{Target,"target"},{EffectiveTarget,"effectiveTarget"},{DisplayMode,"displayMode"},{Phase,"phase"},{Status,"status"},{Busy,"busy"},{DestinationVisible,"destinationVisible"}};}
void ConnectionModel::changed(int row){emit dataChanged(index(row),index(row));emit configurationChanged(rows_[row].id);}
void ConnectionModel::setSingleServer(bool value){if(singleServer_==value)return;singleServer_=value;if(rowCount())emit dataChanged(index(0),index(rowCount()-1),{EffectiveTarget,DestinationVisible});emit destinationModeChanged();}
bool ConnectionModel::validTarget(const QVariantMap& target,bool complete){
    if(!keys(target,{"address","port","localAddress","interfaceIndex"})||target.size()!=4)return false;
    const auto address=target.value("address").toString();
    return ((!complete&&address.isEmpty())||ipv4(address,false))&&ipv4(target.value("localAddress").toString(),true)
        &&integer(target.value("port"),1,65535)&&integer(target.value("interfaceIndex"),0,2147483647);
}
bool ConnectionModel::setSharedTarget(QVariantMap target){if(!validTarget(target,false))return false;sharedTarget_=std::move(target);if(rowCount())emit dataChanged(index(0),index(rowCount()-1),{EffectiveTarget});emit sharedTargetChanged();return true;}
QString ConnectionModel::addConnection(QString kind){
    if(rows_.size()>=128||(kind!="display"&&kind!="camera"&&kind!="microphone"&&kind!="usb"))return {};
    Row row;row.id=QUuid::createUuid().toString(QUuid::WithoutBraces);row.kind=std::move(kind);row.target=sharedTarget_;
    row.display={{"mode","native"},{"monitorId",""},{"width",1920},{"height",1080},{"fps",60}};
    const auto id=row.id;beginInsertRows({},rowCount(),rowCount());rows_.push_back(std::move(row));endInsertRows();return id;
}
bool ConnectionModel::removeConnection(QString id){const auto i=find(id);if(i<0||busy(rows_[i].state))return false;beginRemoveRows({},i,i);rows_.erase(rows_.begin()+i);endRemoveRows();return true;}
bool ConnectionModel::setEnabled(QString id,bool value){const auto i=find(id);if(i<0)return false;auto& row=rows_[i];if(row.enabled==value)return true;row.enabled=value;changed(i);emit enabledChanged(id,value);return true;}
bool ConnectionModel::setSource(QString id,QString source){const auto i=find(id);if(i<0||busy(rows_[i].state)||source.size()>8192||source.contains(QChar(0))||source.contains('\n')||source.contains('\r'))return false;rows_[i].source=std::move(source);changed(i);return true;}
bool ConnectionModel::setTarget(QString id,QVariantMap target){const auto i=find(id);if(i<0||busy(rows_[i].state)||!validTarget(target,false))return false;rows_[i].target=std::move(target);changed(i);return true;}
bool ConnectionModel::validDisplay(const QVariantMap& mode){
    if(!keys(mode,{"mode","monitorId","width","height","fps"})||mode.size()!=5)return false;
    const auto type=mode.value("mode").toString();const auto monitor=mode.value("monitorId").toString();
    return (type=="native"||type=="custom")&&monitor.size()<=8192&&!monitor.contains(QChar(0))&&!monitor.contains('\n')&&!monitor.contains('\r')
        &&integer(mode.value("width"),2,4096)&&integer(mode.value("height"),2,4096)&&mode.value("width").toInt()%2==0&&mode.value("height").toInt()%2==0&&mode.value("width").toLongLong()*mode.value("height").toLongLong()<=8847360&&integer(mode.value("fps"),1,60);
}
bool ConnectionModel::setDisplayMode(QString id,QVariantMap mode){const auto i=find(id);if(i<0||rows_[i].kind!="display"||busy(rows_[i].state)||!validDisplay(mode))return false;rows_[i].display=std::move(mode);changed(i);return true;}
QVariantMap ConnectionModel::connection(QString id)const{const auto i=find(id);if(i<0)return {};QVariantMap result;const auto roles=roleNames();for(auto it=roles.begin();it!=roles.end();++it)result.insert(QString::fromLatin1(it.value()),data(index(i),it.key()));return result;}
bool ConnectionModel::beginConnection(QString id){
    const auto i=find(id);if(i<0||busy(rows_[i].state))return false;auto& row=rows_[i];QString error;
    if(!row.enabled)error="Enable this connection first.";
    else if(!validTarget(effective(row),true))error="Enter a reachable IPv4 server address and valid port/interface.";
    else if(row.kind!="display"&&row.source.isEmpty())error="Choose a source device.";
    else if(row.kind=="display"&&row.display.value("mode")=="native"&&row.display.value("monitorId").toString().isEmpty())error="Choose a local monitor for native display mode.";
    if(error.isEmpty()&&row.kind=="usb")for(const auto& other:rows_)if(other.id!=id&&other.kind=="usb"&&other.source==row.source&&busy(other.state)){error="This USB device is already reserved by another connection.";break;}
    if(!error.isEmpty()){row.status=error;changed(i);return false;}
    row.heldTarget=effective(row);row.state=State::connecting;row.status="Connecting...";changed(i);emit activityChanged();return true;
}
bool ConnectionModel::setState(QString id,State state,QString status){
    const auto i=find(id);if(i<0)return false;auto& row=rows_[i];
    if(!busy(row.state)&&busy(state))return false; // Must acquire validated snapshot through beginConnection.
    if(state==State::recovery&&row.kind!="usb")return false;
    row.state=state;row.status=std::move(status);if(!busy(state))row.heldTarget.clear();changed(i);emit activityChanged();return true;
}
