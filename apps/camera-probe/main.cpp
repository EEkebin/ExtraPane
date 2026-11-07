#include <windows.h>
#include <dm/camera_receiver.hpp>
#include <iostream>
#include <string_view>

// Explicit device diagnostic, never registered as an automated test. Creates a
// session-only camera, then shuts it down before COM is uninitialized.
int main(int argc,char** argv){
    if(argc!=2||std::string_view(argv[1])!="--probe"){
        std::cout<<"Usage: ExtraPane.CameraProbe --probe\n"
                   "Creates and starts a temporary virtual camera and native decoder.\n"
                   "Run only while the ExtraPane server is stopped.\n";
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
