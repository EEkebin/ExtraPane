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
    if(args.find("receive")!=args.npos){std::cout<<"VIDEO_RECEIVING"<<std::endl;Sleep(INFINITE);return 0;}
    std::string line;std::getline(std::cin,line);
    if(args.find("send-microphone")!=args.npos&&std::getenv("EXTRAPANE_TEST_MIC_FAIL")){
        std::cout<<"error: simulated microphone rejection"<<std::endl;return 1;
    }
    std::cout<<(args.find("send-camera")!=args.npos?"CAMERA_STARTED":"MICROPHONE_STARTED")<<std::endl;
    while(std::getline(std::cin,line))if(line=="Q"){Sleep(150);return 0;}
    return 0;
}
