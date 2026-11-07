#include "resource_model.hpp"

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
    case Selected:return row.selected; default:return {};
    }
}
QHash<int,QByteArray> ResourceModel::roleNames() const {
    return {{Key,"resourceKey"},{Name,"resourceName"},{Description,"resourceDescription"},
            {Available,"resourceAvailable"},{Reason,"resourceReason"},{Selected,"resourceSelected"}};
}
void ResourceModel::setCapability(extrapane::ResourceKind kind,extrapane::Availability availability){
    for(int i=0;i<rowCount();++i)if(rows_[i].kind==kind){
        rows_[i].availability=std::move(availability);
        emit dataChanged(index(i),index(i),{Available,Reason});emit selectionChanged();return;
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
    for(const auto& row:rows_)if(row.selected){if(!row.availability.available)return false;any=true;}
    return any;
}
void ResourceModel::setLocked(bool value){if(locked_==value)return;locked_=value;emit lockedChanged();emit selectionChanged();}
