#include <dm/usb_storage.hpp>
#include <windows.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <utility>

namespace dm::usb {
namespace {
using extrapane::Availability;
Availability fail(const char* text){return {false,text};}
std::optional<std::vector<unsigned char>> query(HANDLE handle,DWORD control,void* input=nullptr,DWORD inputSize=0){
    for(DWORD capacity=4096;capacity<=1024*1024;capacity*=2){
        std::vector<unsigned char> bytes(capacity);DWORD count=0;
        if(DeviceIoControl(handle,control,input,inputSize,bytes.data(),capacity,&count,nullptr)){
            if(count>capacity)return {};bytes.resize(count);return bytes;
        }
        const auto error=GetLastError();if(error!=ERROR_MORE_DATA&&error!=ERROR_INSUFFICIENT_BUFFER)return {};
    }
    return {};
}
bool control(HANDLE handle,DWORD code){DWORD returned=0;return DeviceIoControl(handle,code,nullptr,0,nullptr,0,&returned,nullptr)!=FALSE;}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
    Handle(Handle&& other)noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
    Handle& operator=(Handle&& other)noexcept{if(this!=&other){close();value=std::exchange(other.value,INVALID_HANDLE_VALUE);}return *this;}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    ~Handle(){close();}
    void close()noexcept{if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}
};
class NativeStorageHandles final:public StorageHandles {
public:
    ~NativeStorageHandles()override{close();}
    Availability open(const Disk& disk)override{
        if(notification_||physical_.value!=INVALID_HANDLE_VALUE)return fail("Storage claim already has open handles");
        if(disk.usb_instance_id.empty()||disk.usb_instance_id.size()>=MAX_DEVICE_ID_LEN||
           disk.descriptor_bytes.empty()||disk.layout_bytes.empty())return fail("Storage identity snapshot is incomplete");
        // Subscribe before opening: observed remove/restart invalidates the claim
        // permanently, even if the same instance name appears again.
        CM_NOTIFY_FILTER filter{};filter.cbSize=sizeof filter;
        filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;
        std::copy(disk.usb_instance_id.begin(),disk.usb_instance_id.end(),filter.u.DeviceInstance.InstanceId);
        invalid_.store(false);
        if(CM_Register_Notification(&filter,this,&notify,&notification_)!=CR_SUCCESS)return fail("Cannot monitor USB device removal");
        physical_=Handle(CreateFileW(disk.interface_path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
        if(physical_.value==INVALID_HANDLE_VALUE){close();return fail("Cannot open the selected storage device");}
        const auto valid=validate(disk);if(!valid.available){close();return valid;}
        for(auto path:disk.volumes){
            if(!path.empty()&&path.back()==L'\\')path.pop_back();
            Handle volume(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
            if(volume.value==INVALID_HANDLE_VALUE){close();return fail("Cannot open every source volume exclusively for preparation");}
            const auto data=query(volume.value,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS);
            if(!data||data->size()<offsetof(VOLUME_DISK_EXTENTS,Extents)+sizeof(DISK_EXTENT)){close();return fail("Source volume extent information is incomplete");}
            DWORD count=0;std::memcpy(&count,data->data(),sizeof count);
            if(!count||count>(data->size()-offsetof(VOLUME_DISK_EXTENTS,Extents))/sizeof(DISK_EXTENT)){close();return fail("Source volume extent information is invalid");}
            for(DWORD i=0;i<count;++i){
                DISK_EXTENT extent{};std::memcpy(&extent,data->data()+offsetof(VOLUME_DISK_EXTENTS,Extents)+i*sizeof extent,sizeof extent);
                if(extent.DiskNumber!=disk.disk_number||extent.ExtentLength.QuadPart<=0){close();return fail("Spanned or changed source volumes cannot be shared");}
            }
            volumes_.push_back(std::move(volume));
        }
        return validate(disk);
    }
    Availability validate(const Disk& disk)override{
        if(invalidated()||physical_.value==INVALID_HANDLE_VALUE)return fail("USB source was removed or restarted");
        const auto number=query(physical_.value,IOCTL_STORAGE_GET_DEVICE_NUMBER);
        if(!number||number->size()<sizeof(STORAGE_DEVICE_NUMBER))return fail("Storage handle no longer identifies a disk");
        STORAGE_DEVICE_NUMBER value{};std::memcpy(&value,number->data(),sizeof value);
        if(value.DeviceType!=FILE_DEVICE_DISK||value.DeviceNumber!=disk.disk_number)return fail("Storage handle identity changed");
        STORAGE_PROPERTY_QUERY request{};request.PropertyId=StorageDeviceProperty;request.QueryType=PropertyStandardQuery;
        const auto descriptor=query(physical_.value,IOCTL_STORAGE_QUERY_PROPERTY,&request,sizeof request);
        const auto layout=query(physical_.value,IOCTL_DISK_GET_DRIVE_LAYOUT_EX);
        if(!descriptor||!layout||*descriptor!=disk.descriptor_bytes||*layout!=disk.layout_bytes)return fail("Storage descriptor or partitions changed");
        if(invalidated())return fail("USB source changed during validation");
        return {true,{}};
    }
    Availability lock_all()override{
        if(invalidated()||physical_.value==INVALID_HANDLE_VALUE)return fail("USB source is unavailable");
        if(locked_)return fail("Source volumes are already locked");
        for(std::size_t i=0;i<volumes_.size();++i){
            if(!control(volumes_[i].value,FSCTL_LOCK_VOLUME)){
                for(std::size_t j=0;j<i;++j)control(volumes_[j].value,FSCTL_UNLOCK_VOLUME);
                return fail("A source volume is busy; close its files and retry");
            }
        }
        locked_=true;
        return invalidated()?fail("USB source changed while locking volumes"):Availability{true,{}};
    }
    Availability verify_locked(const Disk& disk)override{
        if(!locked_)return fail("Source volume locks are not held");
        return validate(disk);
    }
    Availability dismount_all()override{
        if(!locked_||invalidated())return fail("Source volumes must remain locked before dismount");
        for(auto& volume:volumes_){
            if(!FlushFileBuffers(volume.value))return fail("Cannot flush a source volume; recovery is required");
            if(!control(volume.value,FSCTL_DISMOUNT_VOLUME))return fail("Cannot dismount every source volume; recovery is required");
        }
        return invalidated()?fail("USB source changed during dismount"):Availability{true,{}};
    }
    Availability handoff_quarantined(const Disk& disk)override{
        auto result=verify_locked(disk);if(!result.available)return result;
        // The protected journal/offline guard, not these temporary handles,
        // now owns source-access safety. USBIP's selected-device handle owns
        // subsequent removal notifications during the driver handoff.
        close();return invalidated()?fail("USB source changed during quarantine handoff"):Availability{true,{}};
    }
    bool invalidated()const noexcept override{return invalid_.load();}
    void release(bool permitUnlock)noexcept override{if(permitUnlock)close();}
private:
    static DWORD CALLBACK notify(HCMNOTIFICATION,PVOID context,CM_NOTIFY_ACTION action,PCM_NOTIFY_EVENT_DATA,DWORD){
        if(action==CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED||action==CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED||action==CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED)
            static_cast<NativeStorageHandles*>(context)->invalid_.store(true);
        return ERROR_SUCCESS;
    }
    void close()noexcept{
        if(notification_){CM_Unregister_Notification(notification_);notification_=nullptr;}
        if(locked_)for(auto& volume:volumes_)control(volume.value,FSCTL_UNLOCK_VOLUME);
        locked_=false;volumes_.clear();physical_.close();
    }
    std::atomic<bool> invalid_{false};
    HCMNOTIFICATION notification_=nullptr;
    Handle physical_;
    std::vector<Handle> volumes_;
    bool locked_=false;
};
}

std::unique_ptr<StorageHandles> native_storage_handles(){return std::make_unique<NativeStorageHandles>();}
StorageOperations::StorageOperations(Disk selected,Inspect inspect,std::unique_ptr<StorageBackend> backend,std::unique_ptr<StorageHandles> handles)
    :selected_(std::move(selected)),inspect_(std::move(inspect)),backend_(std::move(backend)),handles_(std::move(handles)){}
StorageOperations::~StorageOperations(){close_transport();stop_export();release_claim();}
bool StorageOperations::matches(const extrapane::usb::Device& device)const{
    return device.id==selected_.facts.id&&device.generation==selected_.facts.generation&&extrapane::usb::eligible(device).available;
}
extrapane::usb::Device StorageOperations::inspect(){
    if(!backend_||!handles_||!inspect_)return {};
    const auto current=inspect_();
    if(!current.errors.empty())return {};
    for(const auto& disk:current.disks)if(disk.facts.id==selected_.facts.id){
        if(disk.usb_instance_id!=selected_.usb_instance_id||disk.interface_path!=selected_.interface_path||
           disk.disk_number!=selected_.disk_number||disk.volumes!=selected_.volumes||
           disk.descriptor_bytes!=selected_.descriptor_bytes||disk.layout_bytes!=selected_.layout_bytes)return {};
        if(claimed_&&!handles_->validate(selected_).available)return {};
        return disk.facts;
    }
    return {};
}
Availability StorageOperations::claim(const extrapane::usb::Device& device){
    if(claimed_||dismount_started_||!backend_||!handles_||!matches(device))return fail("USB claim is unavailable or stale");
    Availability opened;
    try{opened=handles_->open(selected_);}catch(...){handles_->release(true);throw;}
    if(!opened.available){handles_->release(true);return opened;}
    Availability result;
    try{result=backend_->claim(selected_);}catch(...){handles_->release(true);throw;}
    if(!result.available){handles_->release(true);return result;}
    claimed_=true;return {true,{}};
}
Availability StorageOperations::lock_and_dismount(const extrapane::usb::Device& device){
    if(!claimed_||!matches(device))return fail("USB source has not been claimed");
    auto result=handles_->validate(selected_);if(!result.available)return result;
    result=handles_->lock_all();if(!result.available)return result;
    locked_=true;
    result=handles_->validate(selected_);if(!result.available)return result;
    // From this point even a partial failure must retain source locks until an
    // explicit recovery flow resolves remote ownership and safe restoration.
    dismount_started_=true;
    result=handles_->dismount_all();if(!result.available)return result;
    dismounted_=true;return {true,{}};
}
Availability StorageOperations::export_admitted(const extrapane::usb::Device& device){
    if(!claimed_||!locked_||!dismounted_||!matches(device)||source_invalidated())return fail("USB source is not ready for exclusive export");
    auto result=handles_->validate(selected_);if(!result.available)return result;
    result=backend_->export_locked(selected_);exported_=result.available;return result;
}
Availability StorageOperations::flush_and_eject_remote(){
    if(!exported_)return fail("No USB device is exported");
    return backend_->flush_and_eject_remote();
}
void StorageOperations::close_transport()noexcept{if(backend_)backend_->close_transport();}
void StorageOperations::stop_export()noexcept{if(backend_)backend_->stop_export();exported_=false;}
void StorageOperations::release_claim()noexcept{
    if(claimed_&&backend_)backend_->release_claim(dismount_started_);
    claimed_=false;
    if(handles_)handles_->release(!dismount_started_);
    if(!dismount_started_)locked_=false;
}
bool StorageOperations::source_invalidated()const noexcept{return !handles_||handles_->invalidated();}
}
