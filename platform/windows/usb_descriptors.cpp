#include <dm/usb_descriptors.hpp>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winioctl.h>
#include <usbioctl.h>
#include <array>
#include <algorithm>
#include <cwctype>
namespace dm::usb { namespace {
void require(bool good,const char* why){if(!good)throw std::runtime_error(why);}
bool equal(const std::wstring& a,const std::wstring& b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](wchar_t x,wchar_t y){return towupper(x)==towupper(y);});}
struct Handle{HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Set{HDEVINFO value;~Set(){SetupDiDestroyDeviceInfoList(value);}};
std::wstring id(DEVINST node){wchar_t value[MAX_DEVICE_ID_LEN]{};require(CM_Get_Device_IDW(node,value,MAX_DEVICE_ID_LEN,0)==CR_SUCCESS,"USB device was removed during inspection");return value;}
std::wstring driver_key(DEVINST node){
    DWORD type=0,size=0;require(CM_Get_DevNode_Registry_PropertyW(node,CM_DRP_DRIVER,&type,nullptr,&size,0)==CR_BUFFER_SMALL&&size>=2&&size<=65536&&size%2==0,"USB device driver is not ready");
    std::vector<wchar_t> value(size/2);const auto expected=size;
    require(CM_Get_DevNode_Registry_PropertyW(node,CM_DRP_DRIVER,&type,value.data(),&size,0)==CR_SUCCESS&&size==expected&&type==REG_SZ&&value.back()==0,"USB driver identity changed during inspection");
    require(std::find(value.begin(),value.end()-1,L'\0')==value.end()-1,"Invalid USB driver key");return {value.data(),value.size()-1};
}
std::wstring port_key(HANDLE hub,unsigned port){
    USB_NODE_CONNECTION_DRIVERKEY_NAME initial{};initial.ConnectionIndex=port;DWORD returned=0;
    require(DeviceIoControl(hub,IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,&initial,sizeof(initial),&initial,sizeof(initial),&returned,nullptr),"Cannot identify the selected USB port");
    require(initial.ActualLength>=offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName)+2&&initial.ActualLength<=65536,"Invalid USB port driver-key size");
    std::vector<std::uint8_t> data(initial.ActualLength+2);auto* key=reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(data.data());key->ConnectionIndex=port;
    require(DeviceIoControl(hub,IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,key,DWORD(data.size()),key,DWORD(data.size()),&returned,nullptr),"Cannot read USB port identity");
    require(key->ActualLength>=offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName)+2&&key->ActualLength<=returned&&key->ActualLength<=data.size()-2&&(key->ActualLength - offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName))%2==0,"USB port identity changed during inspection");
    const auto count=(key->ActualLength - offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName))/2;
    const auto end=std::find(key->DriverKeyName,key->DriverKeyName+count,L'\0');require(end!=key->DriverKeyName+count&&end!=key->DriverKeyName,"Invalid USB port identity");return {key->DriverKeyName,end};
}
std::vector<std::uint8_t> descriptor(HANDLE hub,unsigned port,unsigned index,unsigned length){
    require(length>=9&&length<=65535&&index<=255,"Invalid USB descriptor request");
    std::vector<std::uint8_t> data(sizeof(USB_DESCRIPTOR_REQUEST)+length);auto* request=reinterpret_cast<USB_DESCRIPTOR_REQUEST*>(data.data());
    request->ConnectionIndex=port;request->SetupPacket.bmRequest=0x80;request->SetupPacket.bRequest=6;request->SetupPacket.wValue=USHORT((2<<8)|index);request->SetupPacket.wLength=USHORT(length);
    DWORD returned=0;require(DeviceIoControl(hub,IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,request,DWORD(data.size()),request,DWORD(data.size()),&returned,nullptr),"Cannot read the USB configuration descriptor");
    require(returned==sizeof(USB_DESCRIPTOR_REQUEST)+length,"USB configuration descriptor was truncated");
    return {data.begin()+sizeof(USB_DESCRIPTOR_REQUEST),data.end()};
}
}
DeviceDescriptors read_device_descriptors(const std::wstring& instance,unsigned connection_hint){
    DEVINST node=0;auto mutable_id=instance;require(CM_Locate_DevNodeW(&node,mutable_id.data(),CM_LOCATE_DEVNODE_NORMAL)==CR_SUCCESS,"USB device is no longer present");
    require(equal(id(node),instance),"USB device identity changed");DEVINST parent=0;require(CM_Get_Parent(&parent,node,0)==CR_SUCCESS,"USB device has no physical hub");const auto parent_id=id(parent);const auto selected_key=driver_key(node);
    static constexpr GUID hub_guid{0xf18a0e88,0xc30c,0x11d0,{0x88,0x15,0x00,0xa0,0xc9,0x06,0xbe,0xd8}};
    Set hubs{SetupDiGetClassDevsW(&hub_guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE)};require(hubs.value!=INVALID_HANDLE_VALUE,"Cannot inspect USB hubs");
    DeviceDescriptors result;bool found=false,complete=false;
    for(DWORD i=0;i<256;++i){
        SP_DEVICE_INTERFACE_DATA iface{sizeof(iface)};if(!SetupDiEnumDeviceInterfaces(hubs.value,nullptr,&hub_guid,i,&iface)){require(GetLastError()==ERROR_NO_MORE_ITEMS,"Cannot enumerate USB hubs");complete=true;break;}
        DWORD required=0;SetupDiGetDeviceInterfaceDetailW(hubs.value,&iface,nullptr,0,&required,nullptr);require(required>=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)&&required<=65536,"Invalid USB hub path");
        std::vector<std::uint8_t> bytes(required);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(bytes.data());detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA info{sizeof(info)};
        require(SetupDiGetDeviceInterfaceDetailW(hubs.value,&iface,detail,required,nullptr,&info),"Cannot read USB hub path");if(info.DevInst!=parent)continue;
        Handle hub{CreateFileW(detail->DevicePath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr)};require(hub.value!=INVALID_HANDLE_VALUE,"USB descriptors are inaccessible; check device permissions or its driver");
        USB_NODE_INFORMATION hub_info{};hub_info.NodeType=UsbHub;DWORD returned=0;require(DeviceIoControl(hub.value,IOCTL_USB_GET_NODE_INFORMATION,&hub_info,sizeof(hub_info),&hub_info,sizeof(hub_info),&returned,nullptr)&&returned>=sizeof(hub_info),"Cannot read USB hub port count");
        const unsigned ports=hub_info.u.HubInformation.HubDescriptor.bNumberOfPorts;require(ports>0&&ports<=255,"Invalid USB hub port count");
        require(connection_hint<=255,"Invalid USB connection hint");
        for(unsigned port=connection_hint?connection_hint:1;port<=(connection_hint?connection_hint:ports);++port){
            alignas(USB_NODE_CONNECTION_INFORMATION_EX) std::array<std::uint8_t,sizeof(USB_NODE_CONNECTION_INFORMATION_EX)+32*sizeof(USB_PIPE_INFO)> connection{};auto* data=reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(connection.data());data->ConnectionIndex=port;
            require(DeviceIoControl(hub.value,IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,data,DWORD(connection.size()),data,DWORD(connection.size()),&returned,nullptr)&&returned>=offsetof(USB_NODE_CONNECTION_INFORMATION_EX,PipeList),"Cannot inspect USB hub connection");
            if(data->ConnectionStatus!=DeviceConnected)continue;
            if(!equal(port_key(hub.value,port),selected_key))continue;
            require(!found,"USB device maps to multiple hub ports");found=true;
            const auto* raw=reinterpret_cast<const std::uint8_t*>(&data->DeviceDescriptor);result.device.assign(raw,raw+sizeof(data->DeviceDescriptor));
            require(data->DeviceDescriptor.bNumConfigurations>0&&data->DeviceDescriptor.bNumConfigurations<=16,"USB configuration count exceeds supported bounds");
            for(unsigned config=0;config<data->DeviceDescriptor.bNumConfigurations;++config){
                const auto header=descriptor(hub.value,port,config,9);require(header[0]==9&&header[1]==2,"Invalid USB configuration header");const unsigned total=header[2]|(unsigned(header[3])<<8);require(total>=9,"Invalid USB configuration length");
                auto blob=descriptor(hub.value,port,config,total);require(std::equal(header.begin(),header.end(),blob.begin()),"USB configuration changed during inspection");result.configurations.push_back(std::move(blob));
            }
            require(equal(port_key(hub.value,port),selected_key),"USB device moved during descriptor inspection");
        }
    }
    require(complete&&found,"Selected USB device no longer has a verified hub connection");DEVINST current_parent=0;require(CM_Get_Parent(&current_parent,node,0)==CR_SUCCESS&&equal(id(current_parent),parent_id)&&equal(id(node),instance)&&equal(driver_key(node),selected_key),"USB device changed during descriptor inspection");
    result.classification=extrapane::usb::validate_device_descriptors(result.device,result.configurations);return result;
}
}
