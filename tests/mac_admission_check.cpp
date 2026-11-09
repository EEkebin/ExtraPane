#define EXTRAPANE_MAC_ADMISSION_TEST
#include "../platform/macos/admission.mm"
#include <iostream>
using namespace dm::mac;
void check(bool value){if(!value)throw std::runtime_error("Mac admission lifecycle invariant failed");}
int main(){try{
    ResourceTarget target{"10.1.1.1","",43824};auto cancelled=[]{return false;};
    auto direct=acquireAdmission(target,"",0,cancelled);check(direct->alive()&&direct->credential.empty()&&Transport::created==0);direct.reset();
    auto camera=acquireAdmission(target,"123456",1,cancelled);auto mic=acquireAdmission(target,"",2,cancelled);
    check(Transport::created==1&&camera->alive()&&mic->alive());
    check(dm::access::channelCredential(camera->credential)->kind==1&&dm::access::channelCredential(mic->credential)->kind==2);
    camera.reset();check(Transport::closed==0&&mic->alive());
    auto other=acquireAdmission({"10.1.1.1","10.1.1.2",43824},"123456",0,cancelled);check(Transport::created==2);
    other.reset();mic.reset();check(Transport::closed==2);
    bool rejected=false;try{acquireAdmission(target,"123456",0,[]{return true;});}catch(const std::runtime_error&){rejected=true;}check(rejected);
    rejected=false;try{acquireAdmission(target,"999999",0,cancelled);}catch(const std::runtime_error&){rejected=true;}check(rejected);
    Transport::malformed=true;rejected=false;try{acquireAdmission(target,"123456",0,cancelled);}catch(const std::invalid_argument&){rejected=true;}catch(const std::runtime_error&){rejected=true;}check(rejected);
    check(Transport::created==Transport::closed);
    std::cout<<"Mac admission reuse, route isolation, cleanup and rejection checks passed (fake transport).\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
