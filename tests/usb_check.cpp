#include <extrapane/usb.hpp>
#include <stdexcept>
#include <iostream>
#include <vector>
using namespace extrapane;
using namespace extrapane::usb;
void require(bool ok){if(!ok)throw std::runtime_error("USB safety check failed");}
struct Fake final:Operations {
    Device device{"local-device",1,true,true};
    std::vector<std::string> calls;
    bool busy=false, export_fail=false, eject_busy=false, change=false;
    int inspections=0;
    Device inspect() override{calls.push_back("inspect");auto copy=device;if(change&&++inspections>1)++copy.generation;return copy;}
    Availability claim(const Device&)override{calls.push_back("claim");return {true,{}};}
    Availability lock_and_dismount(const Device&)override{calls.push_back("lock");return {!busy,"Volume busy"};}
    Availability export_admitted(const Device&)override{calls.push_back("export");return {!export_fail,"Export failed"};}
    Availability flush_and_eject_remote()override{calls.push_back("eject");return {!eject_busy,"Remote volume busy"};}
    void close_transport()noexcept override{calls.push_back("close");}
    void stop_export()noexcept override{calls.push_back("unexport");}
    void release_claim()noexcept override{calls.push_back("release");}
};
int main(){try{
    for(int field=0;field<8;++field){Fake f;switch(field){case 0:f.device.inventory_complete=false;break;case 1:f.device.removable_storage=false;break;case 2:f.device.composite=true;break;case 3:f.device.system_disk=true;break;case 4:f.device.boot_disk=true;break;case 5:f.device.paging_disk=true;break;case 6:f.device.owned_elsewhere=true;break;default:f.device.generation=0;}
        Transfer t(f);require(!t.start("local-device",1,true).available);require(f.calls==std::vector<std::string>{"inspect"});}
    {Fake f;Transfer t(f);require(!t.start("local-device",1,false).available);require(f.calls.empty());}
    {Fake f;f.change=true;Transfer t(f);require(!t.start("local-device",1,true).available);require(t.state()==State::idle);require(f.calls==std::vector<std::string>({"inspect","claim","inspect","close","unexport","release"}));}
    {Fake f;f.busy=true;Transfer t(f);require(!t.start("local-device",1,true).available);require(t.state()==State::recovery_required);require(f.calls==std::vector<std::string>({"inspect","claim","inspect","lock","close","unexport","release"}));require(!t.start("local-device",1,true).available);}
    {Fake f;f.export_fail=true;Transfer t(f);require(!t.start("local-device",1,true).available);require(t.state()==State::recovery_required);require(f.calls.back()=="release");}
    {Fake f;Transfer t(f);require(t.start("local-device",1,true).available);f.eject_busy=true;require(!t.eject().available);require(t.state()==State::shared);require(f.calls.back()=="eject");f.eject_busy=false;require(t.eject().available);require(t.state()==State::recovery_required);require(!t.start("local-device",1,true).available);}
    {Fake f;Transfer t(f);require(t.start("local-device",1,true).available);t.transport_lost();auto count=f.calls.size();t.transport_lost();require(f.calls.size()==count);require(t.state()==State::recovery_required);}
    std::cout<<"USB eligibility, ownership, busy eject and lost-session checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
