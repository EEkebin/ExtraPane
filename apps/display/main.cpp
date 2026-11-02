#include <dm/display.hpp>
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <chrono>
void show_display_pattern(unsigned seconds);
int main(int argc,char** argv) {
    try {
        if(argc==8 && std::string_view(argv[1])=="configure") {
            int values[6]{};
            for(int i=0;i<6;++i){const std::string_view value=argv[i+2];const auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),values[i]);if(ec!=std::errc{} || end!=value.data()+value.size())throw std::invalid_argument("Invalid mode parameter");}
            for(const auto& monitor:dm::display::active_monitors())if(monitor.dm_virtual){
                dm::display::configure_virtual(monitor.source,{static_cast<unsigned>(values[0]),static_cast<unsigned>(values[1]),static_cast<unsigned>(values[5]),static_cast<unsigned>(values[2]),values[3],values[4]});
                std::cout<<"DM_MODE_APPLIED"<<std::endl;return 0;
            }
            throw std::runtime_error("No independent DM display is active");
        }
        if(argc==2 && std::string_view(argv[1])=="list") {
            const auto monitors=dm::display::active_monitors();
            for(const auto& m:monitors) std::wcout<<m.source<<L" name="<<m.friendly_name<<L" bounds="<<m.left<<L','<<m.top<<L','<<m.right<<L','<<m.bottom<<L" primary="<<m.primary<<L" dm_virtual="<<m.dm_virtual<<L'\n';
            std::cout<<"ACTIVE_MONITORS "<<monitors.size()<<'\n'; return 0;
        }
        if(argc==3 && (std::string_view(argv[1])=="attach" || std::string_view(argv[1])=="pattern")) {
            unsigned seconds=0; const std::string_view value=argv[2];
            const auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),seconds);
            if(ec!=std::errc{} || end!=value.data()+value.size() || seconds<1 || seconds>600) throw std::invalid_argument("Duration must be 1..600 seconds");
            if(std::string_view(argv[1])=="pattern") { show_display_pattern(seconds);return 0; }
            dm::display::VirtualMonitor monitor;
            std::wcout<<L"DM_MONITOR_READY "<<monitor.wait_for_source()<<std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            return 0;
        }
        std::cout<<"dm-display list                 Read current active monitor identities\n"
                   "dm-display attach <seconds>     Explicit device activation test; requires staged signed driver and administrator\n"
                   "dm-display pattern <seconds>    Show a diagnostic window on an active DM monitor; waits up to 35 seconds\n";
        return argc==2 && std::string_view(argv[1])=="--help" ? 0 : 1;
    } catch(const std::exception& error) { std::cerr<<"error: "<<error.what()<<'\n';return 1; }
}
