#include <dm/usb_inventory.hpp>
#include <windows.h>
#include <winioctl.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool condition){if(!condition)throw std::runtime_error("USB inventory parser check failed");}
int main(){
    try{
        // Synthetic API responses only. Never call inventory() in this check.
        std::vector<unsigned char> bytes(sizeof(STORAGE_DEVICE_DESCRIPTOR)+16,0);
        STORAGE_DEVICE_DESCRIPTOR desc{};desc.Size=static_cast<DWORD>(bytes.size());desc.Version=sizeof desc;
        desc.BusType=BusTypeUsb;desc.RemovableMedia=TRUE;desc.ProductIdOffset=sizeof desc;
        std::memcpy(bytes.data(),&desc,sizeof desc);std::memcpy(bytes.data()+sizeof desc,"Flash storage",14);
        auto parsed=dm::usb::detail::descriptor(bytes);
        require(parsed&&parsed->usb&&parsed->removable&&parsed->label=="Flash storage");
        require(!parsed->stable_serial);
        auto serialBytes=bytes;serialBytes.resize(bytes.size()+16,0);
        auto serialDesc=desc;serialDesc.Size=static_cast<DWORD>(serialBytes.size());serialDesc.SerialNumberOffset=static_cast<DWORD>(bytes.size());
        std::memcpy(serialBytes.data(),&serialDesc,sizeof serialDesc);
        std::memcpy(serialBytes.data()+serialDesc.SerialNumberOffset,"A1234567",9);
        auto withSerial=dm::usb::detail::descriptor(serialBytes);require(withSerial&&withSerial->stable_serial);
        std::memcpy(serialBytes.data()+serialDesc.SerialNumberOffset,"00000000",9);
        withSerial=dm::usb::detail::descriptor(serialBytes);require(withSerial&&!withSerial->stable_serial);
        serialDesc.SerialNumberOffset=serialDesc.Size;std::memcpy(serialBytes.data(),&serialDesc,sizeof serialDesc);
        require(!dm::usb::detail::descriptor(serialBytes));
        desc.ProductIdOffset=desc.Size;std::memcpy(bytes.data(),&desc,sizeof desc);
        require(!dm::usb::detail::descriptor(bytes));
        desc.ProductIdOffset=sizeof desc;std::memcpy(bytes.data(),&desc,sizeof desc);
        std::fill(bytes.begin()+sizeof desc,bytes.end(),'x');require(!dm::usb::detail::descriptor(bytes));
        require(!dm::usb::detail::descriptor({}));
        std::vector<unsigned char> layout(offsetof(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry)+sizeof(PARTITION_INFORMATION_EX),0);
        DRIVE_LAYOUT_INFORMATION_EX head{};head.PartitionStyle=PARTITION_STYLE_MBR;head.PartitionCount=1;
        std::memcpy(layout.data(),&head,offsetof(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry));
        PARTITION_INFORMATION_EX part{};part.PartitionStyle=PARTITION_STYLE_MBR;part.PartitionLength.QuadPart=1024;part.Mbr.PartitionType=7;
        auto write=[&]{std::memcpy(layout.data()+offsetof(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry),&part,sizeof part);};write();
        auto protection=dm::usb::detail::protected_layout(layout);require(protection&&!*protection);
        part.Mbr.BootIndicator=TRUE;write();protection=dm::usb::detail::protected_layout(layout);require(protection&&*protection);
        part.Mbr.BootIndicator=FALSE;part.Mbr.PartitionType=0x42;write();protection=dm::usb::detail::protected_layout(layout);require(protection&&*protection);
        head.PartitionCount=2;std::memcpy(layout.data(),&head,offsetof(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry));
        require(!dm::usb::detail::protected_layout(layout));require(!dm::usb::detail::protected_layout({}));
        std::cout<<"USB inventory synthetic parser checks passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
