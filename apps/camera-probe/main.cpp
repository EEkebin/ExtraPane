#include <windows.h>
#include <dm/camera_receiver.hpp>
#include <iostream>
#include <string_view>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

// Explicit device diagnostic, never registered as an automated test. Creates a
// session-only camera, then shuts it down before COM is uninitialized.
int read_active(){
    using Microsoft::WRL::ComPtr;
    auto check=[](HRESULT hr,const char* step){std::cout<<step<<": 0x"<<std::hex<<static_cast<unsigned long>(hr)<<std::endl;if(FAILED(hr))throw std::runtime_error(step);};
    check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM");check(MFStartup(MF_VERSION),"MFStartup");
    struct Cleanup{~Cleanup(){MFShutdown();CoUninitialize();}} cleanup;
    ComPtr<IMFAttributes> attributes;check(MFCreateAttributes(&attributes,1),"Attributes");
    check(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),"Video source type");
    IMFActivate** devices=nullptr;UINT32 count=0;check(MFEnumDeviceSources(attributes.Get(),&devices,&count),"Enumerate");
    struct Devices{IMFActivate** p;UINT32 n;~Devices(){for(UINT32 i=0;i<n;++i)p[i]->Release();CoTaskMemFree(p);}} owned{devices,count};
    for(UINT32 i=0;i<count;++i){wchar_t* label=nullptr;UINT32 length=0;
        if(FAILED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&label,&length)))continue;
        const bool selected=std::wstring_view(label,length).starts_with(L"ExtraPane Camera 1");CoTaskMemFree(label);if(!selected)continue;
        ComPtr<IMFMediaSource> source;check(devices[i]->ActivateObject(IID_PPV_ARGS(&source)),"Activate existing camera");
        struct Stop{IMFMediaSource* p;~Stop(){p->Shutdown();}} stop{source.Get()};
        ComPtr<IMFSourceReader> reader;check(MFCreateSourceReaderFromMediaSource(source.Get(),nullptr,&reader),"Create reader");
        for(unsigned frame=0;frame<3;++frame){DWORD stream=0,flags=0;LONGLONG timestamp=0;ComPtr<IMFSample> sample;
            check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&stream,&flags,&timestamp,&sample),"Read frame");
            DWORD bytes=0;if(sample)check(sample->GetTotalLength(&bytes),"Sample size");
            std::cout<<"sample="<<(sample?1:0)<<" bytes="<<std::dec<<bytes<<" flags="<<flags<<std::endl;
        }return 0;
    }throw std::runtime_error("No active ExtraPane Camera 1");
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--read-active"){try{return read_active();}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}

    if(argc!=2||std::string_view(argv[1])!="--probe"){
        std::cout<<"Usage: ExtraPane.CameraProbe --probe | --read-active\n"
                   "Creates and starts a temporary virtual camera and native decoder.\n"
                   "Run --probe only while the ExtraPane server is stopped.\n"
                   "--read-active reads three frames from an existing ExtraPane Camera 1.\n";
        return argc==1?0:2;
    }
    const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(hr)){std::cerr<<"CoInitializeEx failed: HRESULT 0x"<<std::hex<<static_cast<unsigned long>(hr)<<'\n';return 1;}
    struct Com{~Com(){CoUninitialize();}} com;
    try{
        dm::camera::Receiver receiver({1280,720,30},1,{1,1});
        const auto prepared=receiver.prepare();
        std::cout<<"prepare: "<<(prepared.available?"OK":prepared.reason)<<std::endl;
        if(!prepared.available)return 1;
        const auto started=receiver.start();
        std::cout<<"start: "<<(started.available?"OK":started.reason)<<std::endl;
        receiver.stop();
        std::cout<<"cleanup: complete"<<std::endl;
        return started.available?0:1;
    }catch(const std::exception& e){std::cerr<<"probe: "<<e.what()<<'\n';return 1;}
}
