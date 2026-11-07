#include <windows.h>
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
#include "policy.hpp"
#include <wintrust.h>
#include <softpub.h>
#include <map>
#include <array>
#include <memory>

namespace fs=std::filesystem;
namespace setup=extrapane::usb_setup;
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
    check(bytes.size()<=64*1024*1024,"Microphone component exceeds package limit");
    check(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0,"Hash microphone component failed");
    unsigned char digest[32]{};check(BCryptFinishHash(hash,digest,32,0)>=0,"Finish microphone hash failed");
    std::wostringstream text;text<<std::hex<<std::setfill(L'0');for(auto value:digest)text<<std::setw(2)<<unsigned(value);return text.str();
}
std::span<const unsigned char> payload(){
    auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(101),RT_RCDATA);check(resource!=nullptr,"Embedded backend installer missing");
    auto size=SizeofResource(nullptr,resource);auto bytes=static_cast<const unsigned char*>(LockResource(LoadResource(nullptr,resource)));
    check(bytes&&size>0&&size<=64*1024*1024,"Invalid backend installer size");std::span<const unsigned char> result{bytes,size};
    check(sha256(result)==setup::digest,"Embedded backend installer hash mismatch");return result;
}
fs::path component_root(){
    PWSTR value=nullptr;check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64,0,nullptr,&value)),"Program Files location is unavailable");
    fs::path root(value);CoTaskMemFree(value);return root/L"ExtraPane"/L"Components"/L"Microphone";
}
fs::path legacy_root(){return component_root().parent_path().parent_path().parent_path()/L"ExtraPane Components"/L"Microphone";}
void no_reparse(const fs::path& path){
    for(auto current=path;!current.empty();){
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES)check(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"Microphone setup refuses reparse-point paths");
        auto parent=current.parent_path();if(parent==current)break;current=std::move(parent);
    }
}
bool trusted_writer(PSID sid){
    if(IsWellKnownSid(sid,WinBuiltinAdministratorsSid)||IsWellKnownSid(sid,WinLocalSystemSid))return true;
    Local installer;
    check(ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",&installer.value),"Resolve TrustedInstaller SID failed");
    return EqualSid(sid,installer.value)!=FALSE;
}
void verify_security(const fs::path& path){
    no_reparse(path);
    PSID owner=nullptr;PACL acl=nullptr;Local existing;
    check(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        &owner,nullptr,&acl,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&existing.value))==ERROR_SUCCESS,"Inspect microphone directory security failed");
    check((IsWellKnownSid(owner,WinBuiltinAdministratorsSid)||IsWellKnownSid(owner,WinLocalSystemSid))&&acl,"Microphone directory has an unexpected owner or permissions");
    const ACCESS_MASK writes=FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL;
    for(DWORD i=0;i<acl->AceCount;++i){
        void* raw=nullptr;check(GetAce(acl,i,&raw),"Inspect microphone directory ACL failed");const auto* header=static_cast<ACE_HEADER*>(raw);
        if(header->AceFlags&INHERIT_ONLY_ACE)continue; // Does not grant access to this object; payload files are checked separately.
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        check(header->AceType==ACCESS_ALLOWED_ACE_TYPE,"Unsupported microphone directory ACL");
        auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);auto* principal=&ace->SidStart;
        if(ace->Mask&writes)check(trusted_writer(principal),"Microphone directory is writable by an unprivileged principal");
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
    check(file.value!=INVALID_HANDLE_VALUE,"Read installed microphone payload failed");LARGE_INTEGER size{};
    check(GetFileSizeEx(file.value,&size)&&size.QuadPart>0&&size.QuadPart<=64*1024*1024,"Invalid installed microphone payload size");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));DWORD got=0;
    check(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size(),"Read installed microphone payload failed");return bytes;
}
bool write_payload(const fs::path& path,std::span<const unsigned char> bytes){
    no_reparse(path);Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE){check(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS,"Create microphone payload failed");check(sha256(read_file(path))==sha256(bytes),"Existing microphone payload does not match package");return false;}
    DWORD written=0;
    if(!WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(file.value)){
        CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;
        check(DeleteFileW(path.c_str())!=FALSE,"Microphone payload write failed and the new partial file could not be removed");
        throw std::runtime_error("Write microphone payload failed; partial file removed");
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
void verified_payload(const fs::path& path){
    check(setup::trusted_payload(path.filename().wstring(),sha256(read_file(path))),"USB/IP file does not match the pinned installer payload");
    if(path.filename()==L"usbip.exe")signed_file(path);
}
bool key_exists(HKEY hive,const wchar_t* path,REGSAM view){Key key;const auto code=RegOpenKeyExW(hive,path,0,KEY_QUERY_VALUE|view,&key.value);if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)return false;check(code==ERROR_SUCCESS,"Cannot inspect existing USB/IP registration");return true;}
constexpr wchar_t uninstall_key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{199505b0-b93d-4521-a8c7-897818e0205a}_is1";
bool backend_footprint(const fs::path& standard){
    if(fs::exists(standard))return true;
    for(auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})for(auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})if(key_exists(hive,uninstall_key,view))return true;
    for(auto key:{L"SYSTEM\\CurrentControlSet\\Services\\usbip2_ude",L"SYSTEM\\CurrentControlSet\\Services\\usbip2_filter"})if(key_exists(HKEY_LOCAL_MACHINE,key,KEY_WOW64_64KEY))return true;
    return false;
}
void compatible_version(const fs::path& exe){
    DWORD ignored=0;const auto size=GetFileVersionInfoSizeW(exe.c_str(),&ignored);check(size&&size<1024*1024,"USB/IP version information missing");std::vector<unsigned char> data(size);
    check(GetFileVersionInfoW(exe.c_str(),0,size,data.data()),"Read USB/IP version failed");VS_FIXEDFILEINFO* info=nullptr;UINT length=0;
    check(VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&length)&&length>=sizeof(*info)&&info->dwSignature==0xfeef04bd,"Invalid USB/IP version");
    check(setup::compatible(HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),HIWORD(info->dwFileVersionLS),LOWORD(info->dwFileVersionLS)),"Existing USB/IP version is not the supported 0.9.8.1; shared installation left unchanged");
}
void verify_backend(const fs::path& standard){
    verify_security(standard);compatible_version(standard/L"usbip.exe");
    for(auto name:setup::files){const auto path=standard/name;verify_security(path);verified_payload(path);}
    check(key_exists(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\usbip2_ude",KEY_WOW64_64KEY),"USB/IP client driver is not installed; existing installation left unchanged");
}
constexpr wchar_t manifest_name[]=L"extrapane-microphone-v1.txt";
std::vector<unsigned char> manifest(const std::array<std::wstring,3>& hashes){
    std::string text="ExtraPane Microphone v1\nUSBip 0.9.8.1\n";
    for(const auto& hash:hashes){check(setup::hash(hash),"Invalid component hash");for(auto c:hash)text+=static_cast<char>(c);text+='\n';}return {text.begin(),text.end()};
}
void verify_owned(const fs::path& root){
    verify_security(root);verify_security(root/manifest_name);std::array<std::wstring,3> hashes;
    for(std::size_t i=0;i<setup::files.size();++i){verify_security(root/setup::files[i]);verified_payload(root/setup::files[i]);hashes[i]=sha256(read_file(root/setup::files[i]));}
    check(read_file(root/manifest_name)==manifest(hashes),"Managed microphone files changed or have no valid ownership manifest");
}
void cleanup_legacy(){
    const auto old=legacy_root();if(!fs::exists(old))return;
    if(fs::is_empty(old)){no_reparse(old);RemoveDirectoryW(old.c_str());RemoveDirectoryW(old.parent_path().c_str());return;}
    verify_owned(old);
    for(const auto& entry:fs::directory_iterator(old))check(entry.is_regular_file()&&(setup::allowed(entry.path().filename().wstring())||entry.path().filename()==manifest_name),"Unexpected legacy microphone file; cleanup stopped");
    std::vector<std::unique_ptr<Handle>> handles;
    auto hold=[&](const fs::path& path){auto file=std::make_unique<Handle>(CreateFileW(path.c_str(),DELETE|GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));check(file->value!=INVALID_HANDLE_VALUE,"Stop microphone sessions to finish legacy cleanup");handles.push_back(std::move(file));};
    for(const auto name:setup::files)hold(old/name);hold(old/manifest_name);
    std::size_t marked=0;try{for(auto& file:handles){FILE_DISPOSITION_INFO remove{TRUE};check(SetFileInformationByHandle(file->value,FileDispositionInfo,&remove,sizeof(remove)),"Mark legacy microphone file for removal failed");++marked;}}
    catch(...){for(std::size_t i=0;i<marked;++i){FILE_DISPOSITION_INFO keep{FALSE};SetFileInformationByHandle(handles[i]->value,FileDispositionInfo,&keep,sizeof(keep));}throw;}
    handles.clear();
    RemoveDirectoryW(old.c_str());RemoveDirectoryW(old.parent_path().c_str());
}
DWORD install_backend(const fs::path& installer,const fs::path& standard){
    // Never invoke the upstream installer with an existing footprint: its upgrade
    // path runs the previous uninstaller and can remove other apps' devices.
    check(!backend_footprint(standard),"USB/IP appeared during setup; refusing to replace shared backend");
    signed_file(installer);
    const std::wstring args=L"\""+installer.wstring()+L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /RESTARTEXITCODE=3010 /SP- /COMPONENTS=main,client /DIR=\""+standard.wstring()+L"\"";
    auto mutable_args=args;STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    check(CreateProcessW(installer.c_str(),mutable_args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,installer.parent_path().c_str(),&startup,&process),"Cannot start pinned USB/IP installer");
    Handle thread(process.hThread),child(process.hProcess);check(WaitForSingleObject(child.value,INFINITE)==WAIT_OBJECT_0,"Wait for backend installer failed");DWORD code=1;check(GetExitCodeProcess(child.value,&code),"Read backend installer result failed");
    check(code==0||code==3010,"USB/IP installer failed; shared backend will not be automatically uninstalled");return code;
}
int modify(bool install){
    check(IsUserAnAdmin()!=FALSE,"Microphone setup requires administrator permission");SYSTEM_INFO info{};GetNativeSystemInfo(&info);check(info.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64,"Microphone setup requires native x64 Windows");
    Handle mutex(CreateMutexW(nullptr,FALSE,L"Global\\ExtraPane.MicrophoneSetup"));check(mutex.value&&mutex.value!=INVALID_HANDLE_VALUE,"Setup lock unavailable");const auto wait=WaitForSingleObject(mutex.value,0);check(wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED,"Another microphone setup is running");struct Unlock{HANDLE value;~Unlock(){ReleaseMutex(value);}}unlock{mutex.value};
    const auto root=component_root(),standard=root.parent_path().parent_path().parent_path()/L"USBip";
    const auto installer=root/L"USBip-0.9.8.1-x64.exe";const bool owned=fs::exists(root/manifest_name);
    if(owned)verify_owned(root);
    if(!install){
        if(!owned){check(!fs::exists(root)||fs::is_empty(root),"No valid ownership marker; no files removed");std::cout<<"not-installed\n";return 0;}
        if(fs::exists(installer))check(sha256(read_file(installer))==setup::digest,"Managed installer changed; no files removed");
        std::vector<std::unique_ptr<Handle>> files;
        auto open_delete=[&](const fs::path& path){auto handle=std::make_unique<Handle>(CreateFileW(path.c_str(),DELETE|GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));check(handle->value!=INVALID_HANDLE_VALUE,"Stop microphone sessions before removing their components");files.push_back(std::move(handle));};
        for(auto name:setup::files)open_delete(root/name);if(fs::exists(installer))open_delete(installer);open_delete(root/manifest_name);
        std::size_t marked=0;
        try{for(auto& file:files){FILE_DISPOSITION_INFO disposition{TRUE};check(SetFileInformationByHandle(file->value,FileDispositionInfo,&disposition,sizeof(disposition)),"Cannot mark component for removal");++marked;}}
        catch(...){for(std::size_t i=0;i<marked;++i){FILE_DISPOSITION_INFO keep{FALSE};SetFileInformationByHandle(files[i]->value,FileDispositionInfo,&keep,sizeof(keep));}throw;}
        files.clear();RemoveDirectoryW(root.c_str());RemoveDirectoryW(root.parent_path().c_str());
        std::cout<<"removed; shared USB/IP driver and installation preserved\n";return 0;
    }
    if(owned){verify_backend(standard);cleanup_legacy();std::cout<<"installed\n";return 0;}
    if(fs::exists(legacy_root())&&!fs::is_empty(legacy_root()))verify_owned(legacy_root());
    const bool existing=backend_footprint(standard);if(existing)verify_backend(standard);
    if(fs::exists(root))check(fs::is_empty(root),"Unowned microphone component directory is not empty; no changes made");
    protected_directory(root.parent_path().parent_path());protected_directory(root.parent_path());protected_directory(root);
    DWORD result=0;std::vector<fs::path> created;
    try{
        if(!existing){if(write_payload(installer,payload()))created.push_back(installer);result=install_backend(installer,standard);verify_backend(standard);}
        std::array<std::wstring,3> hashes;
        for(std::size_t i=0;i<setup::files.size();++i){auto bytes=read_file(standard/setup::files[i]);hashes[i]=sha256(bytes);const auto destination=root/setup::files[i];if(write_payload(destination,bytes))created.push_back(destination);verify_security(destination);verified_payload(destination);}
        const auto marker=manifest(hashes);if(write_payload(root/manifest_name,marker))created.push_back(root/manifest_name);verify_owned(root);
        if(fs::exists(installer))check(DeleteFileW(installer.c_str()),"Cannot remove completed backend installer");
    }catch(...){for(auto it=created.rbegin();it!=created.rend();++it)DeleteFileW(it->c_str());RemoveDirectoryW(root.c_str());throw;}
    cleanup_legacy();
    std::cout<<(result==3010?"installed; restart required\n":"installed\n");return static_cast<int>(result);
}
}
int wmain(int argc,wchar_t** argv){try{check(argc==2,"Use --check-package, --install, --status or --remove");const std::wstring mode=argv[1];
    if(mode==L"--check-package"){payload();std::cout<<"USBIP_PACKAGE_OK\n";return 0;}
    if(mode==L"--check-backend"){verify_backend(component_root().parent_path().parent_path().parent_path()/L"USBip");std::cout<<"USBIP_BACKEND_OK\n";return 0;}
    if(mode==L"--status"){const auto root=component_root();if(!fs::exists(root/manifest_name)){std::cout<<"not-installed\n";return 3;}verify_owned(root);verify_backend(root.parent_path().parent_path().parent_path()/L"USBip");std::cout<<"installed\n";return 0;}
    if(mode==L"--install")return modify(true);if(mode==L"--remove")return modify(false);throw std::runtime_error("Unknown microphone setup command");
}catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}}
