#include <dm/usb_host_backend.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace dm::usb;
using extrapane::Availability;
static void require(bool value){if(!value)throw std::runtime_error("USB host backend lifecycle check failed");}
struct State {
    std::vector<std::string> events;
    HostDevice device{L"USB\\VID_LOCAL", "1-2", {}, false, false};
    bool running=false,failQuarantine=false,failJournal=false,failEjectJournal=false,busyEject=false,failHandoff=false,failOfflineVerify=false;
    unsigned reattachMisses=0;
};
class Runner final:public HostRunner {
public:
    explicit Runner(State& state):s(state){}
    Availability ready()override{return {true,{}};}
    std::vector<HostDevice> devices()override{if(s.reattachMisses){--s.reattachMisses;return {};}return {s.device};}
    Availability reserve(const std::wstring&)override{s.events.push_back("reserve");return {true,{}};}
    Availability bind(const std::string& bus)override{
        require(bus==s.device.bus_id);s.events.push_back("bind");
        s.device.binding_id=L"{12345678-1234-1234-1234-123456789abc}";return {true,{}};
    }
    Availability unbind(const std::wstring& guid)override{
        require(guid==s.device.binding_id);s.events.push_back("unbind");s.device.binding_id.clear();return {true,{}};
    }
    Availability start(const std::wstring& instance,const std::string& bus,std::uint16_t port)override{
        require(instance==s.device.instance_id&&bus==s.device.bus_id&&port==43210);
        s.events.push_back("daemon-start");s.running=true;return {true,{}};
    }
    void stop()noexcept override{s.running=false;s.events.push_back("daemon-stop");}
    void release()noexcept override{s.events.push_back("release");}
    bool running()const noexcept override{return s.running;}
private:State& s;
};
class Guard final:public RecoveryGuard {
public:
    explicit Guard(State& state):s(state){}
    Availability quarantine(const Disk&)override{s.events.push_back("quarantine");return {!s.failQuarantine,"Offline unsupported"};}
    Availability verify(const Disk&)override{s.events.push_back("verify-offline");return {!s.failOfflineVerify,"Disk identity changed"};}
    Availability binding_intent(const Disk&,const std::string&)override{s.events.push_back("journal-intent");return {true,{}};}
    Availability binding_acquired(const Disk&,const std::wstring&)override{s.events.push_back("journal-bound");return {!s.failJournal,"Journal write failed"};}
    Availability destination_ejected(const Disk&)override{s.events.push_back("journal-ejected");return {!s.failEjectJournal,"Eject journal write failed"};}
    Availability binding_released(const Disk&,const std::wstring&)override{s.events.push_back("journal-released");return {true,{}};}
private:State& s;
};
static auto backend(State& s){
    HostTransport transport{
        [&](const std::string& bus,std::uint16_t port)->Availability{require(bus=="1-2"&&port==43210);s.events.push_back("tunnel-start");return {true,{}};},
        [&]{s.events.push_back("tunnel-close");},
        [&]()->Availability{s.events.push_back("remote-eject");return {!s.busyEject,"Destination busy"};},
        [&](const Disk&)->Availability{s.events.push_back("handoff");return {!s.failHandoff,"Handoff failed"};}
    };
    return usb_host_backend(std::make_unique<Runner>(s),std::make_unique<Guard>(s),std::move(transport),43210);
}
static bool has(const State& s,const char* value){return std::find(s.events.begin(),s.events.end(),value)!=s.events.end();}
static auto pos(const State& s,const char* value){return std::find(s.events.begin(),s.events.end(),value)-s.events.begin();}
int main(){
    try{
        Disk disk;disk.facts.id="disk-local";disk.facts.generation=1;disk.facts.inventory_complete=true;disk.facts.removable_storage=true;
        disk.usb_instance_id=L"USB\\VID_LOCAL";
        {
            State s;auto host=backend(s);require(host->claim(disk).available);require(host->export_locked(disk).available);
            require(pos(s,"quarantine")<pos(s,"journal-intent")&&pos(s,"journal-intent")<pos(s,"bind")&&
                pos(s,"bind")<pos(s,"journal-bound")&&pos(s,"journal-bound")<pos(s,"daemon-start")&&pos(s,"daemon-start")<pos(s,"tunnel-start"));
            require(pos(s,"journal-intent")<pos(s,"handoff")&&pos(s,"handoff")<pos(s,"bind"));
            s.busyEject=true;require(!host->flush_and_eject_remote().available);require(s.running&&!has(s,"unbind"));
            s.busyEject=false;require(host->flush_and_eject_remote().available);
            host->close_transport();host->stop_export();s.reattachMisses=2;host->release_claim(true);
            require(has(s,"unbind")&&has(s,"journal-released")&&s.device.binding_id.empty());
            require(pos(s,"remote-eject")<pos(s,"journal-ejected")&&pos(s,"journal-ejected")<pos(s,"unbind"));
        }
        {
            State s;auto host=backend(s);require(host->claim(disk).available);require(host->export_locked(disk).available);
            host->close_transport();host->stop_export();host->release_claim(true);
            require(!has(s,"unbind")&&!s.device.binding_id.empty());
        }
        {
            State s;s.failHandoff=true;auto host=backend(s);require(host->claim(disk).available);
            require(!host->export_locked(disk).available&&has(s,"handoff")&&!has(s,"bind"));
        }
        {
            State s;s.failOfflineVerify=true;auto host=backend(s);require(host->claim(disk).available);
            const auto result=host->export_locked(disk);
            require(!result.available&&result.reason=="Verifying disk after taking it offline: Disk identity changed");
            require(!has(s,"journal-intent")&&!has(s,"bind")&&!has(s,"daemon-start"));
            host->release_claim(true);require(!has(s,"unbind"));
        }
        {
            State s;auto host=backend(s);require(host->claim(disk).available);require(host->export_locked(disk).available);
            require(host->flush_and_eject_remote().available);
            s.device.binding_id=L"{ffffffff-1234-1234-1234-123456789abc}";
            host->close_transport();host->stop_export();host->release_claim(true);
            require(!has(s,"unbind")&&has(s,"journal-ejected")&&!has(s,"journal-released"));
        }
        {
            State s;s.failEjectJournal=true;auto host=backend(s);require(host->claim(disk).available);require(host->export_locked(disk).available);
            require(!host->flush_and_eject_remote().available);
            host->close_transport();host->stop_export();host->release_claim(true);
            require(has(s,"journal-ejected")&&!has(s,"unbind")&&!s.device.binding_id.empty());
        }
        {
            State s;s.failQuarantine=true;auto host=backend(s);require(host->claim(disk).available);
            require(!host->export_locked(disk).available&&!has(s,"bind")&&!has(s,"daemon-start"));
        }
        {
            State s;s.failJournal=true;auto host=backend(s);require(host->claim(disk).available);
            require(!host->export_locked(disk).available&&has(s,"bind")&&!has(s,"daemon-start"));
            host->release_claim(true);require(!has(s,"unbind"));
        }
        {
            State s;s.device.binding_id=L"{12345678-1234-1234-1234-123456789abc}";
            auto host=backend(s);require(!host->claim(disk).available&&!has(s,"quarantine")&&!has(s,"bind"));
        }
        {
            State s;auto host=backend(s);require(host->claim(disk).available);s.device.instance_id=L"USB\\REPLACEMENT";
            require(!host->export_locked(disk).available&&!has(s,"quarantine")&&!has(s,"bind"));
        }
        std::cout<<"USB host backend fake lifecycle checks passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
