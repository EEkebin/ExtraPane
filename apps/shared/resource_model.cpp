#include "resource_model.hpp"
#include <QSet>
#include <algorithm>

namespace {
bool selectable(const QVariantList& devices,const QString& id){
    return !id.isEmpty()&&std::any_of(devices.begin(),devices.end(),[&](const QVariant& value){const auto item=value.toMap();return item.value("id").toString()==id&&item.value("available",true).toBool();});
}
}


ResourceModel::ResourceModel(QObject* parent):QAbstractListModel(parent),rows_{
    {extrapane::ResourceKind::display,"display","Display","Use this screen as a monitor for the server.",{false,"Display receiver unavailable"},true},
    {extrapane::ResourceKind::camera,"camera","Camera","Use this device's camera in apps on the server.",{false,"Camera sharing is not available in this build"}},
    {extrapane::ResourceKind::microphone,"microphone","Microphone","Use this device's microphone in apps on the server.",{false,"Microphone sharing is not available in this build"}},
    {extrapane::ResourceKind::usb,"usb","USB","Connect a selected USB device exclusively to the server.",{false,"USB sharing is not available in this build"}}
} {}
int ResourceModel::rowCount(const QModelIndex& parent) const { return parent.isValid()?0:static_cast<int>(rows_.size()); }
QVariant ResourceModel::data(const QModelIndex& index,int role) const {
    if(!index.isValid() || index.model()!=this || index.column()!=0 || index.row()<0 || index.row()>=rowCount())return {};
    const auto& row=rows_[index.row()];
    switch(role){
    case Key:return row.key; case Name:return row.name; case Description:return row.description;
    case Available:return row.availability.available; case Reason:return QString::fromStdString(row.availability.reason);
    case Selected:return row.selected;case Devices:return row.devices;case DeviceSelected:return row.deviceSelected;
    case Scanning:return row.scanning;case DeviceStatus:return row.deviceStatus;case Running:return row.running;
    case Status:return row.status;case Muted:return row.muted;default:return {};
    }
}
QHash<int,QByteArray> ResourceModel::roleNames() const {
    return {{Key,"resourceKey"},{Name,"resourceName"},{Description,"resourceDescription"},
            {Available,"resourceAvailable"},{Reason,"resourceReason"},{Selected,"resourceSelected"},
            {Devices,"resourceDevices"},{DeviceSelected,"resourceDeviceSelected"},{Scanning,"resourceScanning"},
            {DeviceStatus,"resourceDeviceStatus"},{Running,"resourceRunning"},{Status,"resourceStatus"},{Muted,"resourceMuted"}};
}
void ResourceModel::setCapability(extrapane::ResourceKind kind,extrapane::Availability availability){
    for(int i=0;i<rowCount();++i)if(rows_[i].kind==kind){
        rows_[i].availability=std::move(availability);
        if(!rows_[i].availability.available)rows_[i].selected=false;
        emit dataChanged(index(i),index(i),{Available,Reason,Selected});emit selectionChanged();return;
    }
}
void ResourceModel::select(int row,bool value){
    if(locked_ || row<0 || row>=rowCount() || (value&&!rows_[row].availability.available))return;
    if(rows_[row].selected==value)return;
    rows_[row].selected=value;emit dataChanged(index(row),index(row),{Selected});emit selectionChanged();
}
bool ResourceModel::selected(extrapane::ResourceKind kind)const{
    for(const auto& row:rows_)if(row.kind==kind)return row.selected&&row.availability.available;
    return false;
}
bool ResourceModel::canConnect()const{
    if(locked_)return false;
    bool any=false;
    for(const auto& row:rows_)if(row.selected){
        if(!row.availability.available||row.scanning)return false;
        if(row.kind!=extrapane::ResourceKind::display&&!selectable(row.devices,row.deviceSelected))return false;
        any=true;
    }
    return any;
}
void ResourceModel::setLocked(bool value){if(locked_==value)return;locked_=value;emit lockedChanged();emit selectionChanged();}

void ResourceModel::setDevices(extrapane::ResourceKind kind,QVariantList devices,QString selected,QString status,bool scanning){
    for(int i=0;i<rowCount();++i)if(rows_[i].kind==kind){
        auto& row=rows_[i];
        // Ambiguous identities must never become a selectable source. Input is
        // already bounded by the inventory parser; retain one canonical entry.
        QSet<QString> seen;QVariantList validated;
        for(const auto& value:devices){
            auto item=value.toMap();const auto id=item.value("id").toString();
            if(id.isEmpty()||item.value("label").toString().isEmpty())continue;
            if(seen.contains(id)){
                for(auto& previous:validated)if(previous.toMap().value("id").toString()==id){auto duplicate=previous.toMap();duplicate["available"]=false;previous=duplicate;}
                continue;
            }
            seen.insert(id);validated.push_back(item);
        }
        row.devices=std::move(validated);
        row.deviceSelected=selectable(row.devices,selected)?std::move(selected):QString{};
        row.deviceStatus=std::move(status);row.scanning=scanning;
        emit dataChanged(index(i),index(i),{Devices,DeviceSelected,DeviceStatus,Scanning});emit selectionChanged();return;
    }
}
void ResourceModel::setSelectedDevice(extrapane::ResourceKind kind,QString id){
    if(locked_)return;
    for(int i=0;i<rowCount();++i)if(rows_[i].kind==kind){
        auto& row=rows_[i];if(row.scanning||row.running||(!id.isEmpty()&&!selectable(row.devices,id))||row.deviceSelected==id)return;
        row.deviceSelected=std::move(id);emit dataChanged(index(i),index(i),{DeviceSelected});emit selectionChanged();return;
    }
}
QVariantList ResourceModel::devices(extrapane::ResourceKind kind)const{for(const auto& row:rows_)if(row.kind==kind)return row.devices;return {};}
QString ResourceModel::selectedDevice(extrapane::ResourceKind kind)const{for(const auto& row:rows_)if(row.kind==kind)return row.deviceSelected;return {};}
bool ResourceModel::scanning(extrapane::ResourceKind kind)const{for(const auto& row:rows_)if(row.kind==kind)return row.scanning;return false;}
void ResourceModel::setSessionState(extrapane::ResourceKind kind,bool running,QString status,bool muted){
    for(int i=0;i<rowCount();++i)if(rows_[i].kind==kind){
        auto& row=rows_[i];row.running=running;row.status=std::move(status);row.muted=muted;
        emit dataChanged(index(i),index(i),{Running,Status,Muted});return;
    }
}
