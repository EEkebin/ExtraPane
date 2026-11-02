#pragma once
#include <memory>
#include <string>
#include <vector>

namespace dm::display {
struct Monitor {
    std::wstring source, friendly_name, device_path;
    long left=0, top=0, right=0, bottom=0;
    bool primary=false, dm_virtual=false;
};
std::vector<Monitor> active_monitors();
struct Mode {
    unsigned width=0,height=0,hz=0,rotation=0;long x=0,y=0;
    bool operator==(const Mode&) const = default;
};
Mode current_mode(const std::wstring& source);
void configure_virtual(const std::wstring& source,const Mode& mode);
void remember_mode(const std::wstring& source,unsigned profile_width,unsigned profile_height);
bool restore_mode(const std::wstring& source,unsigned profile_width,unsigned profile_height);
void set_virtual_mode(const std::wstring& source,unsigned width,unsigned height,unsigned fps,const std::vector<Monitor>& baseline={});
// Requires a separately staged/signed DM driver and an administrator token.
// Device lifetime is the handle lifetime, including process termination.
class VirtualMonitor {
public:
    VirtualMonitor();
    ~VirtualMonitor();
    VirtualMonitor(const VirtualMonitor&)=delete;
    VirtualMonitor& operator=(const VirtualMonitor&)=delete;
    std::wstring wait_for_source();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
