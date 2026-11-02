#include <windows.h>
#include <swdevice.h>
#include <cfgmgr32.h>
#include <dm/display.hpp>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace dm::display {
namespace {
thread_local std::wstring layout_name=L"Layout";
void check(HRESULT result,const char* operation) {
    if(FAILED(result)) throw std::runtime_error(std::string(operation)+" failed, HRESULT="+std::to_string(static_cast<unsigned long>(result)));
}
struct Target { std::wstring source,name,path; };
std::vector<Target> targets() {
    for(unsigned attempt=0;attempt<4;++attempt) {
        UINT32 paths_count=0,modes_count=0;
        auto status=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths_count,&modes_count);
        if(status!=ERROR_SUCCESS) throw std::runtime_error("Cannot read active display topology");
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(paths_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modes_count);
        status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths_count,paths.data(),&modes_count,modes.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER) continue;
        if(status!=ERROR_SUCCESS) throw std::runtime_error("Cannot query active display paths");
        std::vector<Target> result;
        for(UINT32 i=0;i<paths_count;++i) {
            const auto& path=paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS || DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS)
                throw std::runtime_error("Display identity changed during enumeration");
            result.push_back({source.viewGdiDeviceName,target.monitorFriendlyDeviceName,target.monitorDevicePath});
        }
        return result;
    }
    throw std::runtime_error("Display topology kept changing; retry");
}
}
std::vector<Monitor> active_monitors() {
    const auto previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct RestoreDpi{DPI_AWARENESS_CONTEXT context;~RestoreDpi(){if(context)SetThreadDpiAwarenessContext(context);}} restore{previous};
    const auto identities=targets();
    std::vector<Monitor> result;
    if(!EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR handle,HDC,LPRECT,LPARAM param)->BOOL {
        auto& monitors=*reinterpret_cast<std::vector<Monitor>*>(param);
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        if(!GetMonitorInfoW(handle,&info)) return FALSE;
        monitors.push_back({info.szDevice,L"",L"",info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right,info.rcMonitor.bottom,(info.dwFlags&MONITORINFOF_PRIMARY)!=0,false});
        return TRUE;
    },reinterpret_cast<LPARAM>(&result))) throw std::runtime_error("Monitor enumeration failed");
    for(auto& monitor:result) {
        unsigned matches=0;
        for(const auto& identity:identities) if(identity.source==monitor.source) {
            ++matches;
            monitor.friendly_name=identity.name; monitor.device_path=identity.path;
            monitor.dm_virtual=identity.path.find(L"#DMM0001#")!=std::wstring::npos;
        }
        // Never treat a source cloned onto a physical display as independent extension.
        if(matches!=1) monitor.dm_virtual=false;
    }
    return result;
}
Mode current_mode(const std::wstring& source) {
    DEVMODEW value{};value.dmSize=sizeof(value);
    if(!EnumDisplaySettingsW(source.c_str(),ENUM_CURRENT_SETTINGS,&value) || !value.dmPelsWidth || !value.dmPelsHeight)throw std::runtime_error("Display configuration is changing");
    return {value.dmPelsWidth,value.dmPelsHeight,value.dmDisplayFrequency,value.dmDisplayOrientation,value.dmPosition.x,value.dmPosition.y};
}
Mode closest_mode(const std::wstring& source,unsigned width,unsigned height,unsigned hz) {
    Mode best{};double score=std::numeric_limits<double>::max();
    DEVMODEW m{};m.dmSize=sizeof(m);
    for(DWORD i=0;EnumDisplaySettingsW(source.c_str(),i,&m);++i) {
        if(m.dmDisplayOrientation>3 || m.dmPelsWidth>4096 || m.dmPelsHeight>4096 ||
           m.dmPelsWidth%2 || m.dmPelsHeight%2 || m.dmDisplayFrequency>60 || !m.dmDisplayFrequency ||
           static_cast<unsigned long long>(m.dmPelsWidth)*m.dmPelsHeight>8847360)continue;
        const auto native_width=(m.dmDisplayOrientation&1)?m.dmPelsHeight:m.dmPelsWidth;
        const auto native_height=(m.dmDisplayOrientation&1)?m.dmPelsWidth:m.dmPelsHeight;
        for(unsigned rotation=0;rotation<2;++rotation) {
            const auto w=rotation?native_height:native_width,h=rotation?native_width:native_height;
            const double aspect=std::abs(std::log((double(w)/h)/(double(width)/height)));
            const double size=std::abs(std::log((double(w)*h)/(double(width)*height)));
            const double rate=std::abs(double(m.dmDisplayFrequency)-std::min(hz,60u))/60;
            const double candidate=aspect*8+size+rate;
            if(candidate<score){score=candidate;best={w,h,m.dmDisplayFrequency,rotation,0,0};}
        }
    }
    if(!best.width)throw std::runtime_error("No compatible mode in the installed DM display driver");
    return best;
}
void configure_virtual(const std::wstring& source,const Mode& value) {
    bool owned=false;for(const auto& monitor:active_monitors())if(monitor.source==source && monitor.dm_virtual)owned=true;
    if(!owned || value.width<2 || value.height<2 || value.width>4096 || value.height>4096 || value.rotation>3 || value.hz<1 || value.hz>60)throw std::runtime_error("Invalid DM mode configuration");
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    mode.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_DISPLAYFREQUENCY|DM_DISPLAYORIENTATION|DM_POSITION;
    mode.dmPelsWidth=value.width;mode.dmPelsHeight=value.height;mode.dmDisplayFrequency=value.hz;mode.dmDisplayOrientation=value.rotation;mode.dmPosition={value.x,value.y};
    if(ChangeDisplaySettingsExW(source.c_str(),&mode,nullptr,CDS_TEST,nullptr)!=DISP_CHANGE_SUCCESSFUL || ChangeDisplaySettingsExW(source.c_str(),&mode,nullptr,0,nullptr)!=DISP_CHANGE_SUCCESSFUL)throw std::runtime_error("Windows rejected the DM mode");
}
void select_layout_profile(const std::wstring& profile) {
    if(profile.find_first_not_of(L"0123456789.-")!=std::wstring::npos)throw std::invalid_argument("Invalid layout profile");
    layout_name=profile.empty()?L"Layout":L"Layout-"+profile;
}
void remember_mode(const std::wstring& source,unsigned profile_width,unsigned profile_height) {
    const auto mode=current_mode(source);
    const DWORD data[]{1,profile_width,profile_height,mode.width,mode.height,mode.hz,mode.rotation,static_cast<DWORD>(mode.x),static_cast<DWORD>(mode.y)};
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\DM\\Display",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){
        RegSetValueExW(key,layout_name.c_str(),0,REG_BINARY,reinterpret_cast<const BYTE*>(data),sizeof(data));RegCloseKey(key);
    }
}
void remember_last_mode(const std::wstring& source) {
    DWORD data[9]{};DWORD size=sizeof(data);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\DM\\Display",layout_name.c_str(),RRF_RT_REG_BINARY,nullptr,data,&size)==ERROR_SUCCESS && size==sizeof(data) && data[0]==1)
        remember_mode(source,data[1],data[2]);
}
bool restore_last_mode(const std::wstring& source) {
    DWORD data[9]{};DWORD size=sizeof(data);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\DM\\Display",layout_name.c_str(),RRF_RT_REG_BINARY,nullptr,data,&size)!=ERROR_SUCCESS || size!=sizeof(data) || data[0]!=1)return false;
    return restore_mode(source,data[1],data[2]);
}
bool restore_mode(const std::wstring& source,unsigned profile_width,unsigned profile_height,bool preserve_current) {
    DWORD data[9]{};DWORD size=sizeof(data);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\DM\\Display",layout_name.c_str(),RRF_RT_REG_BINARY,nullptr,data,&size)!=ERROR_SUCCESS || size!=sizeof(data) || data[0]!=1 || data[1]!=profile_width || data[2]!=profile_height)return false;
    bool owned=false;for(const auto& monitor:active_monitors())if(monitor.source==source && monitor.dm_virtual)owned=true;
    if(!owned || data[3]<2 || data[4]<2 || data[3]>4096 || data[4]>4096 || data[6]>3)return false;
    const LONG x=static_cast<LONG>(data[7]),y=static_cast<LONG>(data[8]);
    for(const auto& monitor:active_monitors())if(monitor.source!=source && monitor.dm_virtual &&
        static_cast<long long>(x)<monitor.right && static_cast<long long>(x)+data[3]>monitor.left &&
        static_cast<long long>(y)<monitor.bottom && static_cast<long long>(y)+data[4]>monitor.top)return false;
    if(preserve_current)return true; // The running driver already owns the latest Windows layout.
    DEVMODEW mode{};mode.dmSize=sizeof(mode);mode.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_DISPLAYFREQUENCY|DM_DISPLAYORIENTATION|DM_POSITION;
    mode.dmPelsWidth=data[3];mode.dmPelsHeight=data[4];mode.dmDisplayFrequency=data[5];mode.dmDisplayOrientation=data[6];mode.dmPosition={static_cast<LONG>(data[7]),static_cast<LONG>(data[8])};
    return ChangeDisplaySettingsExW(source.c_str(),&mode,nullptr,CDS_TEST,nullptr)==DISP_CHANGE_SUCCESSFUL && ChangeDisplaySettingsExW(source.c_str(),&mode,nullptr,0,nullptr)==DISP_CHANGE_SUCCESSFUL;
}
void set_virtual_mode(const std::wstring& source,unsigned width,unsigned height,unsigned fps,const std::vector<Monitor>& baseline) {
    bool owned=false;
    for(const auto& monitor:active_monitors())if(monitor.source==source && monitor.dm_virtual)owned=true;
    if(!owned)throw std::runtime_error("Mode changes require an independent DM virtual monitor");
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    if(!EnumDisplaySettingsW(source.c_str(),ENUM_CURRENT_SETTINGS,&mode))throw std::runtime_error("Cannot read DM display mode");
    mode.dmPelsWidth=width;mode.dmPelsHeight=height;mode.dmDisplayFrequency=fps;mode.dmDisplayOrientation=DMDO_DEFAULT;
    mode.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_DISPLAYFREQUENCY|DM_DISPLAYORIENTATION;
    if(!baseline.empty()) {
        LONG right=0;for(const auto& monitor:baseline)if(monitor.source!=source)right=std::max(right,monitor.right);
        mode.dmPosition={right,0};mode.dmFields|=DM_POSITION;
    }
    if(ChangeDisplaySettingsExW(source.c_str(),&mode,nullptr,CDS_TEST,nullptr)!=DISP_CHANGE_SUCCESSFUL ||
       ChangeDisplaySettingsExW(source.c_str(),&mode,nullptr,0,nullptr)!=DISP_CHANGE_SUCCESSFUL)
        throw std::runtime_error("Requested DM resolution/refresh is unavailable in the installed driver");
    // Windows can reposition another source while attaching a remembered virtual
    // layout. Restore the original physical origins without changing their modes.
    for(const auto& monitor:baseline)if(monitor.source!=source) {
        DEVMODEW original{};original.dmSize=sizeof(original);
        if(!EnumDisplaySettingsW(monitor.source.c_str(),ENUM_CURRENT_SETTINGS,&original))throw std::runtime_error("Physical display disappeared during extension");
        if(original.dmPosition.x!=monitor.left || original.dmPosition.y!=monitor.top) {
            original.dmFields=DM_POSITION;original.dmPosition={monitor.left,monitor.top};
            if(ChangeDisplaySettingsExW(monitor.source.c_str(),&original,nullptr,0,nullptr)!=DISP_CHANGE_SUCCESSFUL)throw std::runtime_error("Cannot retain physical display position");
        }
    }
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    unsigned stable=0;
    do {
        DEVMODEW actual{};actual.dmSize=sizeof(actual);
        if(EnumDisplaySettingsW(source.c_str(),ENUM_CURRENT_SETTINGS,&actual) && actual.dmPelsWidth==width && actual.dmPelsHeight==height && actual.dmDisplayFrequency==fps) {
            if(++stable>=3)return;
        } else stable=0;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }while(std::chrono::steady_clock::now()<until);
    throw std::runtime_error("DM display mode did not stabilize");
}
struct VirtualMonitor::State {
    std::vector<Monitor> baseline;
    unsigned slot=0;
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HSWDEVICE device=nullptr;
    HRESULT result=E_PENDING;
    ~State() { if(device) SwDeviceClose(device); if(event) CloseHandle(event); }
    static void CALLBACK created(HSWDEVICE,HRESULT result,PVOID context,PCWSTR instance) noexcept {
        auto& state=*static_cast<State*>(context);
        state.result=result;
        // Fixed instance is known to the caller; keep the callback allocation-free.
        (void)instance;
        SetEvent(state.event);
    }
};
VirtualMonitor::VirtualMonitor(unsigned slot):state_(std::make_unique<State>()) {
    auto& s=*state_;s.slot=slot;if(slot)s.baseline=active_monitors();
    if(!s.event) throw std::runtime_error("Cannot create device completion event");
    // Slot zero retains the diagnostic identity; server slots are independent adapters.
    const auto instance=slot?L"SideScreen-LAN-"+std::to_wstring(slot):L"DM-LAN-1";
    SW_DEVICE_CREATE_INFO info{}; info.cbSize=sizeof(info);
    info.pszInstanceId=instance.c_str();
    info.pszzHardwareIds=L"DMIndirectDisplay\0";
    info.pszzCompatibleIds=L"DMIndirectDisplay\0";
    info.pszDeviceDescription=L"DM LAN Virtual Display";
    info.CapabilityFlags=SWDeviceCapabilitiesRemovable|SWDeviceCapabilitiesSilentInstall|SWDeviceCapabilitiesDriverRequired;
    check(SwDeviceCreate(L"DM",L"HTREE\\ROOT\\0",&info,0,nullptr,State::created,&s,&s.device),"Create DM virtual display (requires administrator and staged driver)");
    if(WaitForSingleObject(s.event,10000)!=WAIT_OBJECT_0) throw std::runtime_error("DM device creation timed out");
    check(s.result,"Enumerate DM virtual display");
}
VirtualMonitor::~VirtualMonitor()=default;
std::wstring VirtualMonitor::wait_for_source() {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    std::wstring previous;
    unsigned stable=0;
    do {
        bool found=false;
        for(const auto& monitor:active_monitors()) if(monitor.dm_virtual && (!state_->slot || std::none_of(state_->baseline.begin(),state_->baseline.end(),[&](const auto& old){return old.device_path==monitor.device_path;}))) {
            found=true;
            if(previous==monitor.source)++stable;else {previous=monitor.source;stable=0;}
            if(stable>=2)return monitor.source;
        }
        if(!found){previous.clear();stable=0;}
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while(std::chrono::steady_clock::now()<until);
    throw std::runtime_error("No active independent DM monitor appeared. Check the signed driver/device status and select Extend for DM LAN Display in Windows Display settings.");
}
}
