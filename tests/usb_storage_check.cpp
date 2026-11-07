#include <dm/usb_storage.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

using extrapane::Availability;
using namespace dm::usb;
static void require(bool condition){if(!condition)throw std::runtime_error("USB storage lifecycle check failed");}
struct State {
    std::vector<std::string> events;
    bool invalidate=false,failLock=false,failDismount=false,failExport=false,busyEject=false;
};
class FakeHandles final:public StorageHandles {
public:
    explicit FakeHandles(State& state):s(state){}
    Availability open(const Disk&)override{s.events.push_back("open");return {true,{}};}
    Availability validate(const Disk&)override{s.events.push_back("validate");return {!s.invalidate,"Device removed"};}
    Availability verify_locked(const Disk&)override{return {!s.invalidate,"Device removed"};}
    Availability lock_all()override{s.events.push_back("lock-all");return {!s.failLock,"Volume busy"};}
    Availability dismount_all()override{s.events.push_back("dismount-all");return {!s.failDismount,"Dismount failed"};}
    bool invalidated()const noexcept override{return s.invalidate;}
    void release(bool unlock)noexcept override{s.events.push_back(unlock?"unlock":"retain-locks");}
private:State& s;
};
class FakeBackend final:public StorageBackend {
public:
    explicit FakeBackend(State& state):s(state){}
    Availability claim(const Disk&)override{s.events.push_back("claim");return {true,{}};}
    Availability export_locked(const Disk&)override{s.events.push_back("export");return {!s.failExport,"Export failed"};}
    Availability flush_and_eject_remote()override{s.events.push_back("remote-eject");return {!s.busyEject,"Remote files open"};}
    void close_transport()noexcept override{s.events.push_back("close-transport");}
    void stop_export()noexcept override{s.events.push_back("stop-export");}
    void release_claim(bool recovery)noexcept override{s.events.push_back(recovery?"claim-recovery":"claim-release");}
private:State& s;
};
static Disk selected(){
    Disk disk;disk.facts.id="local-disk";disk.facts.generation=7;disk.facts.inventory_complete=true;disk.facts.removable_storage=true;
    disk.interface_path=L"local-interface";disk.usb_instance_id=L"USB\\VID_TEST";disk.disk_number=5;
    disk.descriptor_bytes={1};disk.layout_bytes={2};disk.volumes={L"local-volume"};return disk;
}
static bool contains(const State& s,const char* event){return std::find(s.events.begin(),s.events.end(),event)!=s.events.end();}
int main(){
    try{
        auto disk=selected();
        const auto inspect=[&]{return Inventory{{disk},{}};};
        {
            State s;
            StorageOperations operations(disk,inspect,std::make_unique<FakeBackend>(s),std::make_unique<FakeHandles>(s));
            extrapane::usb::Transfer transfer(operations);
            require(transfer.start("local-disk",7,true).available);
            const auto lock=std::find(s.events.begin(),s.events.end(),"lock-all");
            const auto dismount=std::find(s.events.begin(),s.events.end(),"dismount-all");
            const auto exportCall=std::find(s.events.begin(),s.events.end(),"export");
            require(lock<dismount&&dismount<exportCall);
            s.busyEject=true;require(!transfer.eject().available);
            require(transfer.state()==extrapane::usb::State::shared&&!contains(s,"stop-export"));
            transfer.transport_lost();
            require(transfer.state()==extrapane::usb::State::recovery_required);
            require(contains(s,"retain-locks")&&contains(s,"claim-recovery")&&!contains(s,"unlock"));
        }
        {
            State s;s.failLock=true;
            StorageOperations operations(disk,inspect,std::make_unique<FakeBackend>(s),std::make_unique<FakeHandles>(s));
            extrapane::usb::Transfer transfer(operations);
            require(!transfer.start("local-disk",7,true).available);
            require(!contains(s,"dismount-all")&&!contains(s,"export")&&contains(s,"unlock"));
        }
        {
            State s;s.failDismount=true;
            StorageOperations operations(disk,inspect,std::make_unique<FakeBackend>(s),std::make_unique<FakeHandles>(s));
            extrapane::usb::Transfer transfer(operations);
            require(!transfer.start("local-disk",7,true).available);
            require(!contains(s,"export")&&contains(s,"retain-locks")&&!contains(s,"unlock"));
        }
        {
            State s;s.failExport=true;
            StorageOperations operations(disk,inspect,std::make_unique<FakeBackend>(s),std::make_unique<FakeHandles>(s));
            extrapane::usb::Transfer transfer(operations);
            require(!transfer.start("local-disk",7,true).available);
            require(contains(s,"retain-locks")&&operations.recovery_required());
        }
        {
            State s;s.invalidate=true;
            StorageOperations operations(disk,inspect,std::make_unique<FakeBackend>(s),std::make_unique<FakeHandles>(s));
            extrapane::usb::Transfer transfer(operations);
            require(!transfer.start("local-disk",7,true).available);
            require(!contains(s,"lock-all")&&!contains(s,"export"));
        }
        {
            State s;
            StorageOperations operations(disk,inspect,nullptr,std::make_unique<FakeHandles>(s));
            extrapane::usb::Transfer transfer(operations);
            require(!transfer.start("local-disk",7,true).available);
            require(!contains(s,"open"));
        }
        std::cout<<"USB storage fake lifecycle checks passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
