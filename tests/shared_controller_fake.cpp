#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>

// Test-only native children: no network, capture, USB, or device APIs.
int main(int argc,char** argv){
    std::string args;for(int i=1;i<argc;++i)args+=std::string(argv[i])+" ";
    if(args.find("--session")!=args.npos){
        std::string line;std::getline(std::cin,line);
        std::cout<<"SESSION_READY"<<std::endl;
        while(std::getline(std::cin,line)){
            if(line=="Q")break;
            if(line.size()!=5||line.substr(0,4)!="ADD "||line[4]<'0'||line[4]>'3')return 2;
            if(std::getenv("EXTRAPANE_TEST_BAD_TICKET")){std::cout<<"SESSION_TICKET_V1\tcamera\tinvalid"<<std::endl;Sleep(INFINITE);return 0;}
            constexpr const char* names[]={"display","camera","microphone","usb"};
            std::cout<<"SESSION_TICKET_V1\t"<<names[line[4]-'0']<<"\t-"<<std::endl;
            if(std::getenv("EXTRAPANE_TEST_SESSION_EXIT")){Sleep(300);std::cerr<<"error: simulated session loss"<<std::endl;return 1;}
        }
        return 0;
    }
    if(args.find("list-storage")!=args.npos){std::cout<<"USB_DEVICE_V1\t757362\tFake returned disk\tavailable\nUSB_LIST_DONE"<<std::endl;return 0;}
    if(args.find("receive")!=args.npos){std::cout<<"VIDEO_RECEIVING"<<std::endl;Sleep(INFINITE);return 0;}
    std::string line;std::getline(std::cin,line);
    if(args.find("send-microphone")!=args.npos&&std::getenv("EXTRAPANE_TEST_MIC_FAIL")){
        std::cout<<"error: simulated microphone rejection"<<std::endl;return 1;
    }
    if(args.find("send-usb")!=args.npos){
        bool veto=std::getenv("EXTRAPANE_TEST_USB_BUSY")!=nullptr;
        std::cout<<"USB_STARTED"<<std::endl;
        while(std::getline(std::cin,line)){
            if(line=="X"){std::cout<<"USB_RECOVERY_REQUIRED simulated forced session loss"<<std::endl;return 0;}
            if(line!="Q")return 3;
            std::cout<<"USB_RETURNING"<<std::endl;Sleep(200);
            if(veto){veto=false;std::cout<<"USB_STOP_BLOCKED receiver-busy"<<std::endl;continue;}
            std::cout<<"USB_RETURNED"<<std::endl;return 0;
        }
        return 0;
    }
    std::cout<<(args.find("send-camera")!=args.npos?"CAMERA_STARTED":"MICROPHONE_STARTED")<<std::endl;
    while(std::getline(std::cin,line))if(line=="Q"){Sleep(150);return 0;}
    return 0;
}
