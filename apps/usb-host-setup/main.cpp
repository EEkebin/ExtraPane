#include <windows.h>
#include <winioctl.h>
#include "monitor_policy.hpp"
#include "uninstall_policy.hpp"
#include "install_policy.hpp"
#include <shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <span>
#include <iomanip>
#include <sstream>
#include <cstring>
#include "usb_host_payload.hpp"
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <wintrust.h>
#include <softpub.h>
#include <map>
#include <array>
#include <memory>
#include <algorithm>
#include <string_view>

namespace fs=std::filesystem;
namespace setup=extrapane::usb_host_setup;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;explicit Handle(HANDLE h):value(h){}~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle(const Handle&)=delete;};
struct Key {HKEY value=nullptr;~Key(){if(value)RegCloseKey(value);}};
struct Local {void* value=nullptr;~Local(){if(value)LocalFree(value);}};
std::wstring sha256(std::span<const unsigned char> bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Open SHA256 provider failed");
    struct Algorithm{BCRYPT_ALG_HANDLE value;~Algorithm(){BCryptCloseAlgorithmProvider(value,0);}} cleanup{algorithm};
    BCRYPT_HASH_HANDLE hash=nullptr;check(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"Create SHA256 failed");
    struct Hash{BCRYPT_HASH_HANDLE value;~Hash(){BCryptDestroyHash(value);}} hashCleanup{hash};
    check(bytes.size()<=64*1024*1024,"USB host component exceeds package limit");
    check(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0,"Hash USB host component failed");
    unsigned char digest[32]{};check(BCryptFinishHash(hash,digest,32,0)>=0,"Finish USB host hash failed");
    std::wostringstream text;text<<std::hex<<std::setfill(L'0');for(auto value:digest)text<<std::setw(2)<<unsigned(value);return text.str();
}
fs::path component_root(){
    PWSTR value=nullptr;check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64,0,nullptr,&value)),"Program Files location is unavailable");
    fs::path root(value);CoTaskMemFree(value);return root/L"ExtraPane"/L"Components"/L"UsbHost";
}
fs::path legacy_root(){return component_root().parent_path().parent_path().parent_path()/L"ExtraPane Components"/L"UsbHost";}
bool trusted_writer(PSID sid){
    if(IsWellKnownSid(sid,WinBuiltinAdministratorsSid)||IsWellKnownSid(sid,WinLocalSystemSid))return true;
    Local installer;check(ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",&installer.value),"Resolve TrustedInstaller SID failed");
    return EqualSid(sid,installer.value)!=FALSE;
}
void no_reparse(const fs::path& path){
    for(auto current=path;!current.empty();){
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES)check(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"USB host setup refuses reparse-point paths");
        auto parent=current.parent_path();if(parent==current)break;current=std::move(parent);
    }
}
void verify_security(const fs::path& path,bool system_driver=false){
    no_reparse(path);
    PSID owner=nullptr;PACL acl=nullptr;Local existing;
    check(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        &owner,nullptr,&acl,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&existing.value))==ERROR_SUCCESS,"Inspect USB host directory security failed");
    check((IsWellKnownSid(owner,WinBuiltinAdministratorsSid)||IsWellKnownSid(owner,WinLocalSystemSid)||(system_driver&&trusted_writer(owner)))&&acl,"USB host directory has an unexpected owner or permissions");
    const ACCESS_MASK writes=FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL;
    for(DWORD i=0;i<acl->AceCount;++i){
        void* raw=nullptr;check(GetAce(acl,i,&raw),"Inspect USB host directory ACL failed");const auto* header=static_cast<ACE_HEADER*>(raw);
        if(header->AceFlags&INHERIT_ONLY_ACE)continue;
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        check(header->AceType==ACCESS_ALLOWED_ACE_TYPE,"Unsupported USB host directory ACL");
        auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);auto* principal=&ace->SidStart;
        if(ace->Mask&writes)check(trusted_writer(principal),"USB host directory is writable by an unprivileged principal");
    }
}
void protected_directory(const fs::path& path){
    no_reparse(path);Local security;
    check(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)(A;OICI;GRGX;;;LS)",SDDL_REVISION_1,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&security.value),nullptr),"Directory security failed");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),security.value,FALSE};
    if(!CreateDirectoryW(path.c_str(),&attributes))check(GetLastError()==ERROR_ALREADY_EXISTS,"Create component directory failed");
    verify_security(path);
}
std::vector<unsigned char> read_file(const fs::path& path){
    no_reparse(path);Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    check(file.value!=INVALID_HANDLE_VALUE,"Read installed USB host payload failed");LARGE_INTEGER size{};
    check(GetFileSizeEx(file.value,&size)&&size.QuadPart>0&&size.QuadPart<=64*1024*1024,"Invalid installed USB host payload size");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));DWORD got=0;
    check(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size(),"Read installed USB host payload failed");return bytes;
}
bool write_payload(const fs::path& path,std::span<const unsigned char> bytes){
    no_reparse(path);Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE){check(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS,"Create USB host payload failed");check(sha256(read_file(path))==sha256(bytes),"Existing USB host payload does not match package");return false;}
    DWORD written=0;
    if(!WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(file.value)){
        CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;
        check(DeleteFileW(path.c_str())!=FALSE,"USB host payload write failed and the new partial file could not be removed");
        throw std::runtime_error("Write USB host payload failed; partial file removed");
    }
    return true;
}
void signed_file(const fs::path& path){
    no_reparse(path);WINTRUST_FILE_INFO file{sizeof(file)};file.pcwszFilePath=path.c_str();
    WINTRUST_DATA data{sizeof(data)};data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;// Setup may retrieve missing certificate-chain intermediates on a fresh PC.
    // Trust verification still fails closed; pinned payload hashes remain mandatory.
    data.dwProvFlags=WTD_REVOCATION_CHECK_NONE;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;const auto result=WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE),&action,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE),&action,&data);
    check(result==ERROR_SUCCESS,"USB/IP component has no trusted Authenticode signature; nothing will be executed");
}
constexpr wchar_t manifest_name[]=L"extrapane-usbhost-v1.txt";
constexpr wchar_t host_key[]=L"SOFTWARE\\ExtraPane\\UsbHost";
constexpr wchar_t setup_key[]=L"SOFTWARE\\ExtraPane\\UsbHostSetup";
struct Service {SC_HANDLE value=nullptr;explicit Service(SC_HANDLE h):value(h){}~Service(){if(value)CloseServiceHandle(value);}Service(const Service&)=delete;};
std::span<const unsigned char> payload(const setup::File& item){
    auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(item.resource),RT_RCDATA);check(resource,"USB host package is missing a component");
    const auto size=SizeofResource(nullptr,resource);const auto bytes=static_cast<const unsigned char*>(LockResource(LoadResource(nullptr,resource)));
    check(bytes&&size&&size<=64*1024*1024,"USB host package component exceeds bounds");std::span<const unsigned char> result{bytes,size};check(sha256(result)==item.digest,"USB host package component hash mismatch");return result;
}
std::vector<unsigned char> manifest(){
    std::string text="ExtraPane UsbHost v1\nsource=";for(auto c:std::wstring(setup::source))text+=static_cast<char>(c);text+='\n';
    for(const auto& file:setup::files){for(auto c:std::wstring(file.digest))text+=static_cast<char>(c);text+=' ';for(auto c:std::wstring(file.name))text+=static_cast<char>(c);text+='\n';}return {text.begin(),text.end()};
}
void validate_tree(const fs::path& root,bool complete){
    verify_security(root);std::vector<fs::path> expected;for(const auto& file:setup::files)expected.emplace_back(file.name);expected.emplace_back(manifest_name);
    for(const auto& entry:fs::recursive_directory_iterator(root)){
        no_reparse(entry.path());const auto relative=entry.path().lexically_relative(root);
        if(entry.is_directory()){check(relative==L"Drivers","Unknown directory in managed USB host; no changes made");verify_security(entry.path());continue;}
        check(entry.is_regular_file()&&std::find(expected.begin(),expected.end(),relative)!=expected.end(),"Unowned file in managed USB host; no changes made");verify_security(entry.path());
    }
    for(const auto& file:setup::files){const auto path=root/file.name;if(fs::exists(path))check(sha256(read_file(path))==file.digest,"Managed USB host payload changed; no changes made");else check(!complete,"Managed USB host payload is incomplete");}
    if(fs::exists(root/manifest_name))check(read_file(root/manifest_name)==manifest(),"USB host ownership manifest changed");else check(!complete,"USB host ownership manifest is missing");
}
void trusted_registry(HKEY key){
    DWORD type=0,size=0;check(RegQueryValueExW(key,L"SymbolicLinkValue",nullptr,&type,nullptr,&size)==ERROR_FILE_NOT_FOUND,"USB host registry links are forbidden");
    PSECURITY_DESCRIPTOR sd=nullptr;PSID owner=nullptr;PACL dacl=nullptr;
    check(GetSecurityInfo(key,SE_REGISTRY_KEY,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&dacl,nullptr,&sd)==ERROR_SUCCESS,"Inspect USB host registry security failed");Local free{sd};
    SECURITY_DESCRIPTOR_CONTROL control=0;DWORD revision=0;check(GetSecurityDescriptorControl(sd,&control,&revision)&&dacl&&(control&SE_DACL_PROTECTED)&&(IsWellKnownSid(owner,WinBuiltinAdministratorsSid)||IsWellKnownSid(owner,WinLocalSystemSid)),"USB host registry owner or ACL is not protected");
    constexpr DWORD writes=KEY_SET_VALUE|KEY_CREATE_SUB_KEY|KEY_CREATE_LINK|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_ALL|GENERIC_WRITE;
    for(DWORD i=0;i<dacl->AceCount;++i){void* raw=nullptr;check(GetAce(dacl,i,&raw),"Read USB host registry ACL failed");const auto header=static_cast<ACE_HEADER*>(raw);if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;check(header->AceType==ACCESS_ALLOWED_ACE_TYPE,"Unsupported USB host registry ACL");auto ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);if(ace->Mask&writes)check(IsWellKnownSid(&ace->SidStart,WinBuiltinAdministratorsSid)||IsWellKnownSid(&ace->SidStart,WinLocalSystemSid),"USB host registry is writable by an untrusted account");}
}
void protected_key(HKEY parent,const wchar_t* name,Key& key){
    DWORD disposition=0;auto status=RegOpenKeyExW(parent,name,REG_OPTION_OPEN_LINK,KEY_READ|KEY_WRITE|READ_CONTROL|KEY_WOW64_64KEY,&key.value);
    if(status==ERROR_FILE_NOT_FOUND){Local security;check(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;CI;KA;;;SY)(A;CI;KA;;;BA)(A;CI;KR;;;BU)",SDDL_REVISION_1,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&security.value),nullptr),"USB host registry ACL creation failed");SECURITY_ATTRIBUTES attributes{sizeof(attributes),security.value,FALSE};
        status=RegCreateKeyExW(parent,name,0,nullptr,REG_OPTION_NON_VOLATILE,KEY_READ|KEY_WRITE|READ_CONTROL|KEY_WOW64_64KEY,&attributes,&key.value,&disposition);
        check(status==ERROR_SUCCESS,"Create USB host registry failed");RegCloseKey(key.value);key.value=nullptr;status=RegOpenKeyExW(parent,name,REG_OPTION_OPEN_LINK,KEY_READ|KEY_WRITE|READ_CONTROL|KEY_WOW64_64KEY,&key.value);
    }check(status==ERROR_SUCCESS,"Open USB host registry failed");trusted_registry(key.value);
}
void registration_preflight(const fs::path& root){
    Key host;const auto status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,host_key,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&host.value);
    if(status==ERROR_FILE_NOT_FOUND||status==ERROR_PATH_NOT_FOUND)return;
    check(status==ERROR_SUCCESS,"Cannot inspect private USB host ownership");trusted_registry(host.value);
    DWORD type=0,bytes=0;const auto value=RegQueryValueExW(host.value,L"APPLICATIONFOLDER",nullptr,&type,nullptr,&bytes);
    if(value==ERROR_SUCCESS){check(type==REG_SZ&&bytes>=2&&bytes<=32768&&bytes%2==0,"Invalid private USB host registration");std::wstring folder(bytes/2,L'\0');check(RegQueryValueExW(host.value,L"APPLICATIONFOLDER",nullptr,&type,reinterpret_cast<BYTE*>(folder.data()),&bytes)==ERROR_SUCCESS&&folder.back()==L'\0',"Read private USB host registration failed");folder.pop_back();check(folder.find(L'\0')==std::wstring::npos&&(fs::path(folder)==root||fs::path(folder)==legacy_root()),"Another installation owns private USB host registration");}
    else check(value==ERROR_FILE_NOT_FOUND,"Cannot inspect private USB host folder");
    for(const auto name:{L"Devices",L"Policy"}){Key child;const auto opened=RegOpenKeyExW(host.value,name,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&child.value);if(opened==ERROR_FILE_NOT_FOUND)continue;check(opened==ERROR_SUCCESS,"Cannot inspect private USB ownership");trusted_registry(child.value);DWORD count=0;check(RegQueryInfoKeyW(child.value,nullptr,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&!count,"USB bindings or policies remain; eject/recover before setup");}
}
void no_pending_recovery(){
    Key recovery;const auto status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\ExtraPane\\Components\\UsbRecovery",REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&recovery.value);
    if(status==ERROR_FILE_NOT_FOUND||status==ERROR_PATH_NOT_FOUND)return;
    check(status==ERROR_SUCCESS,"Cannot inspect pending USB recovery journals");trusted_registry(recovery.value);DWORD count=0;
    check(RegQueryInfoKeyW(recovery.value,nullptr,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&!count,"Pending USB quarantine/recovery journal exists; recover storage before removing its backend");
}
std::wstring string_value(HKEY key,const wchar_t* name){
    DWORD type=0,bytes=0;const auto status=RegQueryValueExW(key,name,nullptr,&type,nullptr,&bytes);if(status==ERROR_FILE_NOT_FOUND)return {};
    check(status==ERROR_SUCCESS&&type==REG_SZ&&bytes>=2&&bytes<=32768&&bytes%2==0,"Invalid USB host ownership value");std::wstring result(bytes/2,L'\0');check(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(result.data()),&bytes)==ERROR_SUCCESS&&type==REG_SZ&&result.back()==L'\0',"Read USB host ownership value failed");result.pop_back();check(result.find(L'\0')==std::wstring::npos,"Embedded NUL in USB host ownership value");return result;
}
void set_string(HKEY key,const wchar_t* name,const std::wstring& value){check(RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS,"Write USB host ownership record failed");check(RegFlushKey(key)==ERROR_SUCCESS,"Flush USB host ownership record failed");}
void private_registration(const fs::path& root,bool enable){
    Key software,app,host,devices,policy;check(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE",0,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,&software.value)==ERROR_SUCCESS,"Administrator registry access is required");
    protected_key(software.value,L"ExtraPane",app);protected_key(app.value,L"UsbHost",host);
    const auto folder=string_value(host.value,L"APPLICATIONFOLDER");check(folder.empty()||fs::path(folder)==root||fs::path(folder)==legacy_root(),"Another application owns the private USB host registration");
    protected_key(host.value,L"Devices",devices);protected_key(host.value,L"Policy",policy);
    DWORD count=0;check(RegQueryInfoKeyW(devices.value,nullptr,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&!count,"USB bindings remain; eject and recover them before component setup");
    check(RegQueryInfoKeyW(policy.value,nullptr,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&!count,"Unexpected private USB policy exists; no changes made");
    if(enable)set_string(host.value,L"APPLICATIONFOLDER",root.wstring());
    else {const auto status=RegDeleteValueW(host.value,L"APPLICATIONFOLDER");check(status==ERROR_SUCCESS||status==ERROR_FILE_NOT_FOUND,"Disable USB host registration failed");check(RegFlushKey(host.value)==ERROR_SUCCESS,"Flush disabled USB host registration failed");}
}
fs::path monitor_path(SC_HANDLE service){
    DWORD bytes=0;QueryServiceConfigW(service,nullptr,0,&bytes);check(bytes>=sizeof(QUERY_SERVICE_CONFIGW)&&bytes<=65536,"Invalid existing VBoxUSBMon configuration");std::vector<unsigned char> data(bytes);auto config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data());
    check(QueryServiceConfigW(service,config,bytes,&bytes)&&config->dwServiceType==SERVICE_KERNEL_DRIVER,"Existing VBoxUSBMon is not a compatible kernel driver service");
    std::wstring path=config->lpBinaryPathName;if(path.size()>=2&&path.front()==L'"'&&path.back()==L'"')path=path.substr(1,path.size()-2);
    if(path.starts_with(L"\\??\\"))path.erase(0,4);
    if(path.starts_with(L"\\SystemRoot\\")){wchar_t windows[MAX_PATH]{};check(GetWindowsDirectoryW(windows,MAX_PATH)!=0,"Windows location unavailable");path=std::wstring(windows)+path.substr(11);}
    check(fs::path(path).is_absolute()&&path.find(L'"')==std::wstring::npos,"Existing VBoxUSBMon image path is unsupported");return path;
}
void verify_running_monitor(SC_HANDLE service){
    SERVICE_STATUS_PROCESS status{};DWORD bytes=0;
    check(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes),"Cannot read shared USB monitor state");
    // Do not start or stop another application's service during setup/status.
    // The pinned backend also checks ABI at the start of a real USB session.
    if(status.dwCurrentState==SERVICE_STOPPED)return;
    check(status.dwCurrentState==SERVICE_RUNNING,"Shared USB monitor is changing state; retry setup when it is stable");
    Handle device(CreateFileW(L"\\\\.\\VBoxUSBMon",FILE_READ_DATA|FILE_WRITE_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
    check(device.value!=INVALID_HANDLE_VALUE,"Cannot query running shared USB monitor compatibility");
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));check(event.value,"USB monitor version event failed");
    OVERLAPPED operation{};operation.hEvent=event.value;
    struct Version{DWORD major,minor;} version{};static_assert(sizeof(Version)==8);
    constexpr DWORD get_version=CTL_CODE(FILE_DEVICE_UNKNOWN,0x610,METHOD_BUFFERED,FILE_WRITE_ACCESS);
    DWORD got=0;bool complete=DeviceIoControl(device.value,get_version,nullptr,0,&version,sizeof(version),&got,&operation)!=FALSE;
    if(!complete){
        check(GetLastError()==ERROR_IO_PENDING,"Shared USB monitor version query failed");
        const auto wait=WaitForSingleObject(event.value,2000);
        if(wait!=WAIT_OBJECT_0){CancelIoEx(device.value,&operation);GetOverlappedResult(device.value,&operation,&got,TRUE);throw std::runtime_error("Shared USB monitor version query timed out");}
        check(GetOverlappedResult(device.value,&operation,&got,FALSE),"Shared USB monitor version result failed");
    }
    check(extrapane::usb_monitor::compatible(version.major,version.minor,got),"Shared USB monitor protocol is incompatible; existing service left unchanged");
}
bool monitor(const fs::path& root,bool install){
    Service manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT|(install?SC_MANAGER_CREATE_SERVICE:0)));check(manager.value,"Open service manager failed");Service existing(OpenServiceW(manager.value,L"VBoxUSBMon",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS));
    if(existing.value){const auto path=monitor_path(existing.value);verify_security(path);check(extrapane::usb_monitor::reviewed(sha256(read_file(path)),setup::files[7].digest),"Existing shared VBoxUSBMon build has not been reviewed for reuse; left unchanged");signed_file(path);verify_running_monitor(existing.value);
        const auto previous=legacy_root()/L"Drivers"/L"VBoxUSBMon.sys";
        if(install&&_wcsicmp(path.c_str(),previous.c_str())==0){
            Key ownership;check(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\ExtraPane\\UsbHostSetup",0,KEY_READ|KEY_WOW64_64KEY,&ownership.value)==ERROR_SUCCESS,"Cannot verify legacy monitor ownership");
            check(string_value(ownership.value,L"CreatedMonitor")==L"true","Legacy monitor service has no ExtraPane creation record; preserved");
            const auto destination=root/L"Drivers"/L"VBoxUSBMon.sys";verify_security(destination);signed_file(destination);
            check(sha256(read_file(path))==sha256(read_file(destination)),"Legacy monitor relocation requires identical driver bytes");
            Service writable(OpenServiceW(manager.value,L"VBoxUSBMon",SERVICE_QUERY_CONFIG|SERVICE_CHANGE_CONFIG));check(writable.value,"Cannot relocate the owned monitor service");
            check(_wcsicmp(monitor_path(writable.value).c_str(),path.c_str())==0,"Shared monitor path changed during relocation");
            check(ChangeServiceConfigW(writable.value,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,destination.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr),"Cannot relocate the owned monitor service");
        }
        return false;}
    check(GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST,"Cannot inspect shared VBoxUSBMon service");check(install,"VBoxUSBMon service is not installed");
    const auto path=root/L"Drivers"/L"VBoxUSBMon.sys";signed_file(path);
    Service created(CreateServiceW(manager.value,L"VBoxUSBMon",L"VirtualBox USB Monitor Service",SERVICE_QUERY_CONFIG,SERVICE_KERNEL_DRIVER,SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,path.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr));
    check(created.value,"Create demand-start VBoxUSBMon service failed; existing services were not changed");return true;
}
void package_driver(const fs::path& root,HKEY ownership){
    signed_file(root/L"Drivers"/L"VBoxUSB.cat");signed_file(root/L"Drivers"/L"VBoxUSBMon.sys");
    wchar_t installed[MAX_PATH]{};DWORD required=0;const auto inf=root/L"Drivers"/L"VBoxUSB.inf";
    const auto copied=SetupCopyOEMInfW(inf.c_str(),nullptr,SPOST_PATH,SP_COPY_NOOVERWRITE,installed,MAX_PATH,&required,nullptr);
    const auto error=copied?ERROR_SUCCESS:GetLastError();check(copied||error==ERROR_FILE_EXISTS,"Stage signed USB driver package failed; existing packages left unchanged");
    check(installed[0]&&required<=MAX_PATH,"Staged USB driver package identity unavailable");
    set_string(ownership,L"StagedInf",installed);if(copied)set_string(ownership,L"CreatedInf",installed);
    if(monitor(root,true))set_string(ownership,L"CreatedMonitor",L"true");
}
bool external_usb_consumer(SC_HANDLE manager,bool owned_usb_driver){
    for(const auto* name:{L"usbipd",L"VBoxSup",L"VBoxDrv",L"VBoxUSB"}){
        Service service(OpenServiceW(manager,name,SERVICE_QUERY_CONFIG));
        if(service.value&&!extrapane::usb_uninstall::independent_service(name,owned_usb_driver))continue;
        if(service.value)return true;
        check(GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST,"Cannot rule out another USB driver consumer");
    }
    for(const auto* name:{L"SOFTWARE\\usbipd-win",L"SOFTWARE\\Oracle\\VirtualBox",L"SOFTWARE\\WOW6432Node\\Oracle\\VirtualBox"}){
        Key key;const auto result=RegOpenKeyExW(HKEY_LOCAL_MACHINE,name,REG_OPTION_OPEN_LINK,KEY_READ|KEY_WOW64_64KEY,&key.value);
        if(result==ERROR_SUCCESS)return true;
        check(result==ERROR_FILE_NOT_FOUND||result==ERROR_PATH_NOT_FOUND,"Cannot inspect shared USB ownership");
    }
    return false;
}
void known_registry_contents(HKEY key,std::initializer_list<std::wstring_view> subkeys,std::initializer_list<std::wstring_view> values){
    for(DWORD index=0;;++index){wchar_t name[256]{};DWORD length=256;const auto result=RegEnumKeyExW(key,index,name,&length,nullptr,nullptr,nullptr,nullptr);if(result==ERROR_NO_MORE_ITEMS)break;
        check(result==ERROR_SUCCESS&&std::find(subkeys.begin(),subkeys.end(),std::wstring_view(name,length))!=subkeys.end(),"Unknown USB host registry child; preserved");}
    for(DWORD index=0;;++index){wchar_t name[256]{};DWORD length=256;const auto result=RegEnumValueW(key,index,name,&length,nullptr,nullptr,nullptr,nullptr);if(result==ERROR_NO_MORE_ITEMS)break;
        check(result==ERROR_SUCCESS&&std::find(values.begin(),values.end(),std::wstring_view(name,length))!=values.end(),"Unknown USB host registry value; preserved");}
}
void removal_registration_preflight(HKEY ownership){
    known_registry_contents(ownership,{}, {L"Source",L"State",L"StagedInf",L"CreatedInf",L"CreatedMonitor"});
    Key host;const auto result=RegOpenKeyExW(HKEY_LOCAL_MACHINE,host_key,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&host.value);
    if(result==ERROR_FILE_NOT_FOUND||result==ERROR_PATH_NOT_FOUND)return;
    check(result==ERROR_SUCCESS,"Cannot verify USB host removal registry");trusted_registry(host.value);
    known_registry_contents(host.value,{L"Devices",L"Policy"},{L"APPLICATIONFOLDER"});
    for(const auto* name:{L"Devices",L"Policy"}){Key child;const auto opened=RegOpenKeyExW(host.value,name,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&child.value);if(opened==ERROR_FILE_NOT_FOUND)continue;check(opened==ERROR_SUCCESS,"Cannot verify private USB bindings");trusted_registry(child.value);known_registry_contents(child.value,{},{});}
}
fs::path removable_inf(HKEY ownership){
    const auto recorded=string_value(ownership,L"CreatedInf");if(recorded.empty())return {};
    wchar_t windows[MAX_PATH]{};check(GetWindowsDirectoryW(windows,MAX_PATH)!=0,"Windows location unavailable");
    const fs::path path(recorded);const auto expected=fs::path(windows)/L"INF"/path.filename();
    check(extrapane::usb_uninstall::published_inf(path.filename().wstring())&&_wcsicmp(path.c_str(),expected.c_str())==0,"Owned USB INF record is not an exact published INF path");
    no_reparse(path);if(!fs::exists(path))return {}; // successful earlier removal can be retried
    check(sha256(read_file(path))==setup::files[4].digest,"Published USB INF changed; preserved");
    wchar_t stored[32768]{};DWORD needed=0;
    check(SetupGetInfDriverStoreLocationW(path.c_str(),nullptr,nullptr,stored,32768,&needed),"Cannot verify installed USB driver package");
    const fs::path store(stored);no_reparse(store);
    check(sha256(read_file(store))==setup::files[4].digest&&sha256(read_file(store.parent_path()/L"VBoxUSB.cat"))==setup::files[5].digest,"Installed USB package identity differs; preserved");
    signed_file(store.parent_path()/L"VBoxUSB.cat");return path;
}
void remove_owned_phantom_stubs(const fs::path& inf){
    const auto devices=SetupDiGetClassDevsW(nullptr,L"USB",nullptr,DIGCF_ALLCLASSES);
    check(devices!=INVALID_HANDLE_VALUE,"Cannot inspect USB package device references");
    struct Cleanup { HDEVINFO value; ~Cleanup(){SetupDiDestroyDeviceInfoList(value);} } cleanup{devices};
    auto property=[&](SP_DEVINFO_DATA& device,const DEVPROPKEY& key){
        wchar_t text[1024]{};DEVPROPTYPE type=0;DWORD size=0;
        if(!SetupDiGetDevicePropertyW(devices,&device,&key,&type,reinterpret_cast<PBYTE>(text),sizeof(text),&size,0))return std::wstring{};
        if(type!=DEVPROP_TYPE_STRING||size<sizeof(wchar_t)||size>sizeof(text)||text[size/sizeof(wchar_t)-1]!=0)return std::wstring{};
        return std::wstring(text);
    };
    auto eligible=[&](SP_DEVINFO_DATA& device){
        wchar_t id[1024]{};
        if(!SetupDiGetDeviceInstanceIdW(devices,&device,id,1024,nullptr))return false;
        DEVPROPTYPE type=0;DEVPROP_BOOLEAN present=DEVPROP_TRUE;DWORD size=0;
        const bool known=SetupDiGetDevicePropertyW(devices,&device,&DEVPKEY_Device_IsPresent,&type,reinterpret_cast<PBYTE>(&present),sizeof(present),&size,0)&&type==DEVPROP_TYPE_BOOLEAN&&size==sizeof(present);
        return extrapane::usb_uninstall::removable_stub(
            _wcsnicmp(id,L"USB\\VID_80EE&PID_CAFE\\",22)==0,
            _wcsicmp(property(device,DEVPKEY_Device_DriverInfPath).c_str(),inf.filename().c_str())==0,
            _wcsicmp(property(device,DEVPKEY_Device_Service).c_str(),L"VBoxUSB")==0,
            known,present!=DEVPROP_FALSE);
    };
    for(DWORD i=0;;++i){
        SP_DEVINFO_DATA device{sizeof(device)};
        if(!SetupDiEnumDeviceInfo(devices,i,&device)){check(GetLastError()==ERROR_NO_MORE_ITEMS,"Cannot enumerate USB package references");break;}
        if(!eligible(device))continue;
        SP_REMOVEDEVICE_PARAMS parameters{};parameters.ClassInstallHeader.cbSize=sizeof(SP_CLASSINSTALL_HEADER);parameters.ClassInstallHeader.InstallFunction=DIF_REMOVE;parameters.Scope=DI_REMOVEDEVICE_GLOBAL;
        check(SetupDiSetClassInstallParamsW(devices,&device,&parameters.ClassInstallHeader,sizeof(parameters)),"Cannot prepare absent USB stub cleanup");
        check(eligible(device),"USB stub changed before cleanup; preserved");
        check(SetupDiCallClassInstaller(DIF_REMOVE,devices,&device),"Cannot remove absent owned USB stub");
        std::cout<<"Removed disconnected stub referencing the exact owned USB driver package.\n";
    }
}
void remove_owned_driver(const fs::path& root,HKEY ownership,bool inspect=false){
    Service manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));check(manager.value,"Open service manager failed");
    Service service(OpenServiceW(manager.value,L"VBoxUSBMon",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS));
    if(!service.value)check(GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST,"Cannot inspect USB monitor for removal");
    const auto created=string_value(ownership,L"CreatedMonitor");check(created.empty()||created==L"true","Unknown USB monitor ownership record");
    const auto path=service.value?monitor_path(service.value):fs::path{};
    const bool managed=service.value&&(_wcsicmp(path.c_str(),(root/L"Drivers"/L"VBoxUSBMon.sys").c_str())==0||_wcsicmp(path.c_str(),(legacy_root()/L"Drivers"/L"VBoxUSBMon.sys").c_str())==0);
    bool exact=false;if(managed){verify_security(path);exact=sha256(read_file(path))==setup::files[7].digest;signed_file(path);}
    // VBoxUSB is also installed by our own INF. Its existence alone does not
    // establish an independent consumer of our monitor.
    bool owned_usb_driver=false;
    Service usb(OpenServiceW(manager.value,L"VBoxUSB",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS));
    if(usb.value&&!string_value(ownership,L"CreatedInf").empty()){
        const auto owned_inf=removable_inf(ownership);
        wchar_t system[MAX_PATH]{};check(GetSystemDirectoryW(system,MAX_PATH)!=0,"System directory unavailable");
        const auto usb_path=monitor_path(usb.value);
        const bool expected_path=_wcsicmp(usb_path.c_str(),(fs::path(system)/L"drivers"/L"VBoxUSB.sys").c_str())==0;
        if(!owned_inf.empty()&&expected_path){
            verify_security(usb_path,true);signed_file(usb_path);
            owned_usb_driver=sha256(read_file(usb_path))==setup::files[6].digest;
        }
    }else if(!usb.value)check(GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST,"Cannot inspect USB device driver ownership");
    const bool external=external_usb_consumer(manager.value,owned_usb_driver);
    const auto action=extrapane::usb_uninstall::monitor_action(service.value!=nullptr,created==L"true",managed,exact,external);
    check(action!=extrapane::usb_uninstall::MonitorAction::refuse,"USB monitor ownership changed or another application uses it; preserved");
    const bool created_inf=!string_value(ownership,L"CreatedInf").empty();
    const auto inf_action=extrapane::usb_uninstall::inf_action(created_inf,external);
    const auto inf=inf_action==extrapane::usb_uninstall::InfAction::remove?removable_inf(ownership):fs::path{};
    if(inspect){
        std::cout<<"USB_HOST_REMOVAL_READY monitor="<<(action==extrapane::usb_uninstall::MonitorAction::remove?"remove-owned":"preserve")
                 <<" inf="<<(inf.empty()?(created_inf&&external?"preserve-shared":"preserve-or-absent"):"remove-unused-owned")
                 <<" private-payload=remove-if-not-busy\n";return;
    }
    if(created_inf&&external)std::cout<<"Preserving USB DriverStore package used by an independent USB installation.\n";
    if(action==extrapane::usb_uninstall::MonitorAction::remove){
        Service writable(OpenServiceW(manager.value,L"VBoxUSBMon",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_STOP|DELETE));check(writable.value,"Cannot open owned USB monitor for removal");
        check(_wcsicmp(monitor_path(writable.value).c_str(),path.c_str())==0,"USB monitor changed before removal");
        SERVICE_STATUS status{};check(QueryServiceStatus(writable.value,&status),"Cannot inspect owned USB monitor state");
        if(status.dwCurrentState==SERVICE_RUNNING)check(ControlService(writable.value,SERVICE_CONTROL_STOP,&status),"Owned USB monitor is busy; eject USB devices and retry uninstall");
        for(unsigned i=0;status.dwCurrentState==SERVICE_STOP_PENDING&&i<50;++i){Sleep(100);check(QueryServiceStatus(writable.value,&status),"Cannot wait for owned USB monitor to stop");}
        check(status.dwCurrentState==SERVICE_STOPPED,"Owned USB monitor is busy; retry uninstall after stopping USB sessions");
        check(DeleteService(writable.value),"Remove owned USB monitor failed");
    }
    if(!inf.empty()){
        remove_owned_phantom_stubs(inf);
        check(SetupUninstallOEMInfW(inf.filename().c_str(),0,nullptr),"USB driver package still has active or unrecognized device references; preserved");
    }
}
int check_removal(){
    const auto root=component_root();
    Key ownership;const auto status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\ExtraPane\\UsbHostSetup",REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&ownership.value);
    if(status==ERROR_FILE_NOT_FOUND||status==ERROR_PATH_NOT_FOUND){check(!fs::exists(root)||fs::is_empty(root),"Unowned USB host directory is not empty; no changes made");std::cout<<"not-installed\n";return 0;}
    check(status==ERROR_SUCCESS,"Cannot read USB host ownership");trusted_registry(ownership.value);
    const auto source=string_value(ownership.value,L"Source");
    if(source.empty()){
        known_registry_contents(ownership.value,{},{});
        check(extrapane::usb_uninstall::empty_removal_ready(true,!fs::exists(root)||fs::is_empty(root),!fs::exists(legacy_root())||fs::is_empty(legacy_root())),"Unowned USB host directory is not empty; preserved");
        std::cout<<"not-installed\n";return 0;
    }
    check(source==setup::source,"USB host ownership version differs");
    if(fs::exists(root))validate_tree(root,false);if(fs::exists(legacy_root()))validate_tree(legacy_root(),false);
    registration_preflight(root);no_pending_recovery();removal_registration_preflight(ownership.value);
    remove_owned_driver(root,ownership.value,true);return 0;
}
bool reuse_exact_installation(const fs::path& root){
    // App updates do not need to release a missing card or rewrite its backend.
    // Reuse is a read-only path, and is allowed only for a complete, exact owned
    // package. Incompatible/incomplete packages still take the guarded repair path.
    struct Verification {
        const fs::path& root;
        bool has_manifest(){return fs::exists(root/manifest_name);}
        void payload(){validate_tree(root,true);}
        void ownership(){
            Key key;check(RegOpenKeyExW(HKEY_LOCAL_MACHINE,setup_key,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&key.value)==ERROR_SUCCESS,"Installed USB host ownership is missing; repair requires an intact ownership record");
            trusted_registry(key.value);
            known_registry_contents(key.value,{}, {L"Source",L"State",L"StagedInf",L"CreatedInf",L"CreatedMonitor"});
            check(string_value(key.value,L"Source")==setup::source&&string_value(key.value,L"State")==L"installed","Installed USB host ownership/version is not compatible with this package; existing state was preserved");
        }
        void registration(){
            Key host;check(RegOpenKeyExW(HKEY_LOCAL_MACHINE,host_key,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&host.value)==ERROR_SUCCESS,"Installed USB host registration is missing");
            trusted_registry(host.value);known_registry_contents(host.value,{L"Devices",L"Policy"},{L"APPLICATIONFOLDER"});
            check(fs::path(string_value(host.value,L"APPLICATIONFOLDER"))==root,"Installed USB host registration points elsewhere; preserved");
            for(const auto name:{L"Devices",L"Policy"}){Key child;check(RegOpenKeyExW(host.value,name,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&child.value)==ERROR_SUCCESS,"Installed USB host private registration is incomplete");trusted_registry(child.value);}
        }
        void monitor(){::monitor(root,false);}
    } verification{root};
    if(!setup::reuse_exact_backend(verification))return false;
    std::cout<<"USB_HOST_REUSED exact compatible backend retained; existing bindings and recovery records were not changed\n";
    return true;
}
int modify(bool install){
    check(IsUserAnAdmin(),"USB host setup requires administrator permission");SYSTEM_INFO system{};GetNativeSystemInfo(&system);check(system.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64,"USB host setup requires native x64 Windows");
    Handle lock(CreateMutexW(nullptr,FALSE,L"Global\\ExtraPane.UsbHostSetup"));check(lock.value&&lock.value!=INVALID_HANDLE_VALUE,"USB host setup mutex unavailable");const auto acquired=WaitForSingleObject(lock.value,0);check(acquired==WAIT_OBJECT_0||acquired==WAIT_ABANDONED,"USB host setup is already running");struct Unlock{HANDLE h;~Unlock(){ReleaseMutex(h);}}unlock{lock.value};
    const auto root=component_root();for(const auto& item:setup::files)payload(item);
    if(install&&reuse_exact_installation(root))return 0;
    if(fs::exists(legacy_root()))validate_tree(legacy_root(),false);
    registration_preflight(root);
    Key software,app,ownership;check(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE",0,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,&software.value)==ERROR_SUCCESS,"Administrator registry access required");protected_key(software.value,L"ExtraPane",app);protected_key(app.value,L"UsbHostSetup",ownership);
    const auto source=string_value(ownership.value,L"Source");check(source.empty()||source==setup::source,"Existing managed USB host version differs; upgrade requires explicit migration");
    if(fs::exists(root)){if(source.empty())check(fs::is_empty(root),"Unowned USB host directory is not empty; no changes made");else validate_tree(root,false);}
    if(!install){
        if(source.empty()){
            known_registry_contents(ownership.value,{},{});
            check(extrapane::usb_uninstall::empty_removal_ready(true,!fs::exists(root)||fs::is_empty(root),!fs::exists(legacy_root())||fs::is_empty(legacy_root())),"Unowned USB host directory is not empty; preserved");
            RegCloseKey(ownership.value);ownership.value=nullptr;
            check(RegDeleteKeyW(app.value,L"UsbHostSetup")==ERROR_SUCCESS,"Cannot retire empty USB setup record");
            std::cout<<"not-installed\n";return 0;
        }
        // All files were hash/ACL checked above. Pending journals and unknown
        // private registrations block removal before any component is deleted.
        no_pending_recovery();removal_registration_preflight(ownership.value);std::vector<std::unique_ptr<Handle>> files;
        const auto hold=[&](const fs::path& path){if(fs::exists(path)){auto file=std::make_unique<Handle>(CreateFileW(path.c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));check(file->value!=INVALID_HANDLE_VALUE,"Stop USB host sessions before removing their components");files.push_back(std::move(file));}};
        // Check executable use before touching drivers. Driver files are opened
        // only after the owned service stops (the kernel may hold its image).
        for(const auto& location:{root,legacy_root()}){for(const auto& item:setup::files)if(!std::wstring_view(item.name).starts_with(L"Drivers/"))hold(location/item.name);hold(location/manifest_name);}
        remove_owned_driver(root,ownership.value);
        for(const auto& location:{root,legacy_root()})for(const auto& item:setup::files)if(std::wstring_view(item.name).starts_with(L"Drivers/"))hold(location/item.name);
        private_registration(root,false);set_string(ownership.value,L"State",L"removing");
        std::size_t marked=0;try{for(auto& file:files){FILE_DISPOSITION_INFO remove{TRUE};check(SetFileInformationByHandle(file->value,FileDispositionInfo,&remove,sizeof(remove)),"Mark USB host file for removal failed");++marked;}}
        catch(...){for(std::size_t i=0;i<marked;++i){FILE_DISPOSITION_INFO keep{FALSE};SetFileInformationByHandle(files[i]->value,FileDispositionInfo,&keep,sizeof(keep));}throw;}
        files.clear();
        for(const auto& location:{root,legacy_root()})for(const auto& directory:{location/L"Drivers",location})if(fs::exists(directory))check(RemoveDirectoryW(directory.c_str()),"Owned USB host directory is not empty or is busy; retry uninstall");
        // Delete only the two private keys after bindings/recovery checks.
        const auto removedHost=RegDeleteTreeW(app.value,L"UsbHost");
        check(removedHost==ERROR_SUCCESS||removedHost==ERROR_FILE_NOT_FOUND||removedHost==ERROR_PATH_NOT_FOUND,"Remove private USB host registration failed");
        RegCloseKey(ownership.value);ownership.value=nullptr;
        const auto removedSetup=RegDeleteTreeW(app.value,L"UsbHostSetup");
        check(removedSetup==ERROR_SUCCESS||removedSetup==ERROR_FILE_NOT_FOUND||removedSetup==ERROR_PATH_NOT_FOUND,"Remove USB host ownership record failed");
        std::cout<<"removed owned USB host files, registration and unused owned drivers; independent shared drivers preserved\n";return 0;
    }
    protected_directory(root.parent_path().parent_path());protected_directory(root.parent_path());protected_directory(root);protected_directory(root/L"Drivers");
    set_string(ownership.value,L"Source",setup::source);set_string(ownership.value,L"State",L"preparing");
    for(const auto& item:setup::files){write_payload(root/item.name,payload(item));verify_security(root/item.name);}
    // Marker is published last, after signed driver staging and private registry
    // creation succeed. Partial owned files remain hash-checked for explicit retry.
    package_driver(root,ownership.value);private_registration(root,true);write_payload(root/manifest_name,manifest());validate_tree(root,true);set_string(ownership.value,L"State",L"installed");
    bool restart=false;
    if(fs::exists(legacy_root())){
        validate_tree(legacy_root(),false);
        for(const auto& item:setup::files){const auto path=legacy_root()/item.name;if(fs::exists(path)&&!DeleteFileW(path.c_str())){
            check(MoveFileExW(path.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT),"Schedule legacy USB host cleanup failed");restart=true;
        }}
        if(fs::exists(legacy_root()/manifest_name))check(DeleteFileW((legacy_root()/manifest_name).c_str()),"Remove legacy USB host marker failed");
        for(const auto& dir:{legacy_root()/L"Drivers",legacy_root()})if(fs::exists(dir)&&!RemoveDirectoryW(dir.c_str())){
            check(MoveFileExW(dir.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT),"Schedule legacy USB host directory cleanup failed");restart=true;
        }
        RemoveDirectoryW(legacy_root().parent_path().c_str());
    }
    std::cout<<"installed; driver is demand-start and no USB device was bound\n";return restart?3010:0;
}
}
int wmain(int argc,wchar_t** argv){try{
    check(argc==2,"Use --check-package, --check-removal, --install, --status or --remove");const std::wstring mode=argv[1];
    if(mode==L"--check-package"){for(const auto& item:setup::files)payload(item);std::cout<<"USB_HOST_PACKAGE_OK\n";return 0;}
    if(mode==L"--check-removal")return check_removal();
    if(mode==L"--check-monitor"){monitor(component_root(),false);std::cout<<"USB_MONITOR_COMPATIBLE\n";return 0;}
    if(mode==L"--status"){const auto root=component_root();if(!fs::exists(root/manifest_name)){std::cout<<"not-installed\n";return 3;}validate_tree(root,true);monitor(root,false);Key host;check(RegOpenKeyExW(HKEY_LOCAL_MACHINE,host_key,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&host.value)==ERROR_SUCCESS,"USB host registration missing");trusted_registry(host.value);check(fs::path(string_value(host.value,L"APPLICATIONFOLDER"))==root,"USB host registration points elsewhere");std::cout<<"installed\n";return 0;}
    if(mode==L"--install")return modify(true);if(mode==L"--remove")return modify(false);throw std::runtime_error("Unknown USB host setup command");
}catch(const std::exception& error){std::cerr<<"error: "<<error.what()<<'\n';return 1;}}
