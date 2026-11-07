#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <set>

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
    if(args.find("receive")!=args.npos){
        auto option=[&](const std::string& name){for(int i=1;i+1<argc;++i)if(argv[i]==name)return std::string(argv[i+1]);return std::string{};};
        if(const auto expected=std::getenv("EXTRAPANE_TEST_DISPLAY_MODE")){
            const bool custom=std::string(expected)=="custom";
            const bool valid=custom?(option("--monitor").empty()&&option("--width")=="1920"&&option("--height")=="1080"&&option("--fps")=="60"):
                (option("--monitor")=="FAKE_DISPLAY_1"&&option("--width").empty()&&option("--height").empty()&&option("--fps").empty());
            if(!valid){std::cout<<"error: invalid display launch arguments"<<std::endl;return 9;}
        }
        if(std::getenv("EXTRAPANE_TEST_DISPLAY_EXIT"))return 23;
        std::cout<<"VIDEO_RECEIVING"<<std::endl;Sleep(INFINITE);return 0;
    }
    std::string line;std::getline(std::cin,line);
    if(args.find("broker-camera")!=args.npos||args.find("broker-microphone")!=args.npos){
        const auto device=line;std::set<std::string> rows;std::cout<<"BROKER_READY"<<std::endl;
        while(std::getline(std::cin,line)){
            if(line=="Q")return 0;
            std::istringstream input(line);std::vector<std::string> fields;std::string field;while(std::getline(input,field,'\t'))fields.push_back(field);
            if(fields.size()==6&&fields[0]=="ADD"){
                if(device=="bad"){std::cout<<"RESOURCE_EVENT\t"<<fields[1]<<"\tERROR\tsimulated source failure"<<std::endl;continue;}
                rows.insert(fields[1]);std::cout<<"RESOURCE_EVENT\t"<<fields[1]<<"\tSTARTED\tready"<<std::endl;
            }else if(fields.size()==2&&fields[0]=="REMOVE"){rows.erase(fields[1]);std::cout<<"RESOURCE_EVENT\t"<<fields[1]<<"\tSTOPPED\tdone"<<std::endl;}
            else if(fields.size()==2&&fields[0]=="MIRROR")std::cout<<"CAMERA_MIRROR "<<(fields[1]=="1"?"on":"off")<<std::endl;
            else if(fields.size()!=3||fields[0]!="MUTE")return 3;
        }return 0;
    }
    if(args.find("send-microphone")!=args.npos&&std::getenv("EXTRAPANE_TEST_MIC_FAIL")){
        std::cout<<"error: simulated microphone rejection"<<std::endl;return 1;
    }
    if(args.find("send-usb")!=args.npos){
        bool veto=std::getenv("EXTRAPANE_TEST_USB_BUSY")!=nullptr;
        bool notready=std::getenv("EXTRAPANE_TEST_USB_NOT_READY")!=nullptr;
        if(notready)std::cout<<"USB_WAITING enumerating\nUSB_NOT_READY exact disk missing"<<std::endl;
        else std::cout<<"USB_STARTED"<<std::endl;
        while(std::getline(std::cin,line)){
            if(line=="X"){std::cout<<"USB_RECOVERY_REQUIRED simulated forced session loss"<<std::endl;return 0;}
            if(line!="Q")return 3;
            std::cout<<"USB_RETURNING"<<std::endl;Sleep(200);
            if(notready){notready=false;std::cout<<"USB_RETURN_FAILED not-ready: exact disk missing; access code=123456"<<std::endl;continue;}
            if(veto){veto=false;std::cout<<"USB_STOP_BLOCKED receiver-busy"<<std::endl;continue;}
            if(std::getenv("EXTRAPANE_TEST_USB_INTERRUPTED")){
                std::cout<<"USB_REVOKING Returning after interrupted remote session."<<std::endl;Sleep(200);
                std::cout<<"USB_RETURNED_INTERRUPTED USB returned after interruption; check files for incomplete writes."<<std::endl;return 0;
            }
            std::cout<<"USB_RETURNED"<<std::endl;return 0;
        }
        return 0;
    }
    std::cout<<(args.find("send-camera")!=args.npos?"CAMERA_STARTED":"MICROPHONE_STARTED")<<std::endl;
    while(std::getline(std::cin,line))if(line=="Q"){Sleep(150);return 0;}
    return 0;
}
