#include <dm/usb_runner.hpp>
#include <windows.h>
#include <shlobj.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <span>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cwctype>
namespace dm::usb {
namespace {
namespace fs=std::filesystem;
using extrapane::Availability;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;explicit Handle(HANDLE h):value(h){}~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle(const Handle&)=delete;};
struct Key {HKEY value=nullptr;~Key(){if(value)RegCloseKey(value);}};
struct Local {void* value=nullptr;~Local(){if(value)LocalFree(value);}};
std::wstring sha256(std::span<const unsigned char> bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Open SHA256 provider failed");
    struct Algorithm{BCRYPT_ALG_HANDLE value;~Algorithm(){BCryptCloseAlgorithmProvider(value,0);}} cleanup{algorithm};
    BCRYPT_HASH_HANDLE hash=nullptr;check(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"Create SHA256 failed");
    struct Hash{BCRYPT_HASH_HANDLE value;~Hash(){BCryptDestroyHash(value);}} hashCleanup{hash};
    check(bytes.size()<=64*1024*1024,"USB component exceeds package limit");
    check(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0,"Hash USB component failed");
    unsigned char digest[32]{};check(BCryptFinishHash(hash,digest,32,0)>=0,"Finish USB hash failed");
    std::wostringstream text;text<<std::hex<<std::setfill(L'0');for(auto value:digest)text<<std::setw(2)<<unsigned(value);return text.str();
}
void no_reparse(const fs::path& path){
    for(auto current=path;!current.empty();){
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES)check(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"USB setup refuses reparse-point paths");
        auto parent=current.parent_path();if(parent==current)break;current=std::move(parent);
    }
}
void verify_security(const fs::path& path){
    no_reparse(path);
    PSID owner=nullptr;PACL acl=nullptr;Local existing;
    check(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        &owner,nullptr,&acl,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&existing.value))==ERROR_SUCCESS,"Inspect USB directory security failed");
    check((IsWellKnownSid(owner,WinBuiltinAdministratorsSid)||IsWellKnownSid(owner,WinLocalSystemSid))&&acl,"USB directory has an unexpected owner or permissions");
    const ACCESS_MASK writes=FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL;
    for(DWORD i=0;i<acl->AceCount;++i){
        void* raw=nullptr;check(GetAce(acl,i,&raw),"Inspect USB directory ACL failed");const auto* header=static_cast<ACE_HEADER*>(raw);
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        check(header->AceType==ACCESS_ALLOWED_ACE_TYPE,"Unsupported USB directory ACL");
        auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);auto* principal=&ace->SidStart;
        if(ace->Mask&writes)check(IsWellKnownSid(principal,WinBuiltinAdministratorsSid)||IsWellKnownSid(principal,WinLocalSystemSid),"USB directory is writable by an unprivileged principal");
    }
}
std::vector<unsigned char> read_file(const fs::path& path){
    no_reparse(path);Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    check(file.value!=INVALID_HANDLE_VALUE,"Read installed USB payload failed");LARGE_INTEGER size{};
    check(GetFileSizeEx(file.value,&size)&&size.QuadPart>0&&size.QuadPart<=64*1024*1024,"Invalid installed USB payload size");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));DWORD got=0;
    check(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size(),"Read installed USB payload failed");return bytes;
}
fs::path protected_root(){PWSTR value=nullptr;check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64,0,nullptr,&value)),"Program Files unavailable");fs::path root(value);CoTaskMemFree(value);return root/L"ExtraPane"/L"Components"/L"UsbHost";}
bool hex(std::string_view s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool filename(std::string_view s){
    constexpr std::array<std::string_view,8> allowed{"usbipd.exe","usb.ids","EventManifest.xml","COPYING.md","Drivers/VBoxUSB.inf","Drivers/VBoxUSB.cat","Drivers/VBoxUSB.sys","Drivers/VBoxUSBMon.sys"};
    return std::find(allowed.begin(),allowed.end(),s)!=allowed.end();
}
fs::path verified_executable(){
    const auto root=protected_root();verify_security(root.parent_path());verify_security(root);
    const auto manifest_path=root/L"extrapane-usbhost-v1.txt";verify_security(manifest_path);const auto raw=read_file(manifest_path);check(raw.size()<=65536,"USB host manifest is too large");
    std::istringstream input(std::string(raw.begin(),raw.end()));std::string line;check(static_cast<bool>(std::getline(input,line))&&line=="ExtraPane UsbHost v1","USB host ownership marker missing");
    check(static_cast<bool>(std::getline(input,line))&&line=="source=4431eba52a2de9f21e3a4ba1f7dc1e408cd57019","USB host source version not reviewed");std::set<std::string> names;
    while(std::getline(input,line)){
        check(line.size()>65&&line[64]==' '&&hex(line.substr(0,64))&&filename(line.substr(65))&&names.size()<128,"Malformed USB host manifest");const auto name=line.substr(65);check(names.insert(name).second,"Duplicate USB host file");const auto path=root/fs::path(name);verify_security(path);const auto hash=sha256(read_file(path));check(winrt::to_string(hash)==line.substr(0,64),"USB host file hash mismatch");
    }
    check(names.size()==8&&names.contains("usbipd.exe"),"USB host runtime incomplete");
    verify_security(root/L"Drivers");
    for(const auto& entry:fs::recursive_directory_iterator(root)){no_reparse(entry.path());const auto name=fs::relative(entry.path(),root).generic_string();check((name=="Drivers"&&entry.is_directory())||name=="extrapane-usbhost-v1.txt"||(entry.is_regular_file()&&names.contains(name)),"Unexpected USB host runtime entry");}
    return root/L"usbipd.exe";
}
bool registry_key(const wchar_t* path){Key key;const auto code=RegOpenKeyExW(HKEY_LOCAL_MACHINE,path,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key.value);if(code==ERROR_FILE_NOT_FOUND)return false;check(code==ERROR_SUCCESS,"Cannot inspect USB driver state");return true;}
void backend_installed(){check(registry_key(L"SOFTWARE\\ExtraPane\\UsbHost")&&registry_key(L"SYSTEM\\CurrentControlSet\\Services\\VBoxUSBMon"),"The signed usbipd host driver is not installed");}
void start_host_driver(){
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);check(manager!=nullptr,"Cannot open USB host driver manager");struct Service{SC_HANDLE handle;~Service(){if(handle)CloseServiceHandle(handle);}}manager_scope{manager};
    SC_HANDLE service=OpenServiceW(manager,L"VBoxUSBMon",SERVICE_START|SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG);check(service!=nullptr,"USB host driver unavailable");Service service_scope{service};DWORD size=0;QueryServiceConfigW(service,nullptr,0,&size);check(size&&size<=65536,"Invalid USB host driver configuration");std::vector<unsigned char> bytes(size);auto config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(bytes.data());check(QueryServiceConfigW(service,config,size,&size)&&config->dwServiceType==SERVICE_KERNEL_DRIVER,"USB host service is not a kernel driver");
    std::wstring path=config->lpBinaryPathName;if(path.size()>1&&path.front()==L'"'&&path.back()==L'"')path=path.substr(1,path.size()-2);wchar_t windows[32768]{};check(GetWindowsDirectoryW(windows,32768)>0,"Cannot locate Windows");if(path.starts_with(L"\\SystemRoot\\"))path=std::wstring(windows)+path.substr(11);if(path.starts_with(L"%SystemRoot%\\"))path=std::wstring(windows)+path.substr(12);if(path.starts_with(L"\\??\\"))path.erase(0,4);
    check(fs::path(path).is_absolute(),"Unexpected USB host driver path");no_reparse(path);check(sha256(read_file(path))==sha256(read_file(protected_root()/L"Drivers"/L"VBoxUSBMon.sys")),"Installed USB host driver differs from the managed signed component");
    if(!StartServiceW(service,0,nullptr))check(GetLastError()==ERROR_SERVICE_ALREADY_RUNNING,"Cannot start installed USB host driver");
}
std::wstring quote(const std::wstring& value){std::wstring result=L"\"";unsigned slashes=0;for(auto c:value){if(c==L'\\'){++slashes;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;}result.append(slashes*2,L'\\');return result+L"\"";}
bool bus_valid(const std::string& bus){const auto dash=bus.find('-');return bus.size()<16&&dash!=0&&dash!=bus.npos&&dash+1<bus.size()&&std::all_of(bus.begin(),bus.end(),[](char c){return(c>='0'&&c<='9')||c=='-';})&&bus.find('-',dash+1)==bus.npos;}
bool instance_valid(const std::wstring& id){return id.size()>8&&id.size()<4096&&id.starts_with(L"USB\\")&&std::none_of(id.begin(),id.end(),[](wchar_t c){return c<32;});}
std::wstring as_wide(const std::string& s){return {s.begin(),s.end()};}
struct Process {
    HANDLE process=nullptr,output=nullptr,job=nullptr;
    ~Process(){stop();}
    void stop()noexcept{if(job){CloseHandle(job);job=nullptr;}if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,1);WaitForSingleObject(process,3000);}CloseHandle(process);process=nullptr;}if(output){CloseHandle(output);output=nullptr;}}
    bool running()const noexcept{return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    std::string drain(){std::string result;DWORD available=0;while(output&&PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)&&available){char bytes[4096];DWORD got=0;check(ReadFile(output,bytes,std::min<DWORD>(available,sizeof(bytes)),&got,nullptr)&&got,"Read USB host output failed");result.append(bytes,got);check(result.size()<=1024*1024,"USB host output exceeded limit");}return result;}
    void start(const fs::path& exe,const std::vector<std::wstring>& args){
        stop();SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE write=nullptr;check(CreatePipe(&output,&write,&security,0),"Cannot create USB host output pipe");Handle writer(write);check(SetHandleInformation(output,HANDLE_FLAG_INHERIT,0),"Cannot protect USB output pipe");Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));check(input.value!=INVALID_HANDLE_VALUE,"Cannot open USB host input");
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<unsigned char> storage(size);auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());check(InitializeProcThreadAttributeList(list,1,0,&size),"Cannot initialize process handle policy");struct Attributes{LPPROC_THREAD_ATTRIBUTE_LIST value;~Attributes(){DeleteProcThreadAttributeList(value);}}attributes{list};HANDLE handles[]{write,input.value};check(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr),"Cannot restrict process handles");
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdOutput=write;startup.StartupInfo.hStdError=write;startup.StartupInfo.hStdInput=input.value;startup.lpAttributeList=list;
        auto command=quote(exe.wstring());for(const auto& arg:args)command+=L" "+quote(arg);
        wchar_t windows[32768]{};check(GetWindowsDirectoryW(windows,32768)>0,"Windows location unavailable");
        // No caller DOTNET_STARTUP_HOOKS, runtime probing or arbitrary config env.
        std::wstring environment=L"DOTNET_CLI_TELEMETRY_OPTOUT=1";environment.push_back(0);environment+=L"DOTNET_NOLOGO=1";environment.push_back(0);environment+=L"SystemRoot="+std::wstring(windows);environment.push_back(0);environment+=L"WINDIR="+std::wstring(windows);environment.push_back(0);environment.push_back(0);
        job=CreateJobObjectW(nullptr,nullptr);check(job!=nullptr,"Cannot create owned USB process job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Cannot protect USB process lifetime");
        PROCESS_INFORMATION child{};check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED,environment.data(),exe.parent_path().c_str(),&startup.StartupInfo,&child),"Cannot start protected USB host runtime");process=child.hProcess;Handle initial_thread(child.hThread);check(AssignProcessToJobObject(job,process),"Cannot own USB subprocess lifetime");check(ResumeThread(child.hThread)!=static_cast<DWORD>(-1),"Cannot resume USB subprocess");
    }
};
std::string command(const fs::path& exe,const std::vector<std::wstring>& args){Process child;child.start(exe,args);std::string output;const auto end=GetTickCount64()+15000;while(child.running()){output+=child.drain();check(output.size()<=1024*1024,"USB command output exceeded limit");check(GetTickCount64()<end,"USB command timed out");Sleep(10);}output+=child.drain();DWORD code=1;check(GetExitCodeProcess(child.process,&code)&&code==0,"USB command failed; no fallback or global operation was attempted");return output;}
std::wstring text(const winrt::Windows::Data::Json::JsonObject& item,const wchar_t* key){using namespace winrt::Windows::Data::Json;const auto value=item.GetNamedValue(key);if(value.ValueType()==JsonValueType::Null)return {};check(value.ValueType()==JsonValueType::String,"Malformed USB device property");auto result=std::wstring(value.GetString());check(result.size()<=4096,"Oversized USB device property");return result;}
std::wstring canonical_guid(const std::wstring& value){const auto normalized=value.size()==36?L"{"+value+L"}":value;GUID guid{};check(SUCCEEDED(CLSIDFromString(normalized.c_str(),&guid)),"Malformed USB binding GUID");wchar_t text[40]{};check(StringFromGUID2(guid,text,40)>0,"Cannot canonicalize USB GUID");return text;}
bool same(const std::wstring& a,const std::wstring& b){return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;}
std::wstring registry_binding(const std::wstring& instance,bool external=false){
    Key devices;const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,external?L"SOFTWARE\\usbipd-win\\Devices":L"SOFTWARE\\ExtraPane\\UsbHost\\Devices",0,KEY_ENUMERATE_SUB_KEYS|KEY_WOW64_64KEY,&devices.value);if(external&&(opened==ERROR_FILE_NOT_FOUND||opened==ERROR_PATH_NOT_FOUND))return {};check(opened==ERROR_SUCCESS,"USB binding registry unavailable");std::wstring result;
    for(DWORD i=0;i<1024;++i){wchar_t name[128];DWORD size=128;const auto code=RegEnumKeyExW(devices.value,i,name,&size,nullptr,nullptr,nullptr,nullptr);if(code==ERROR_NO_MORE_ITEMS)return result;check(code==ERROR_SUCCESS,"Malformed USB binding registry");Key key;check(RegOpenKeyExW(devices.value,name,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key.value)==ERROR_SUCCESS,"Cannot inspect USB binding");wchar_t value[4096]{};DWORD bytes=sizeof(value);check(RegGetValueW(key.value,nullptr,L"InstanceId",RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS,"Missing USB binding identity");if(same(value,instance)){check(result.empty(),"Duplicate USB binding identity");result=name;}}
    throw std::runtime_error("Too many USB bindings");
}
class NativeRunner final:public HostRunner {
    Process server_;HANDLE claim_=nullptr;std::wstring instance_,owned_;fs::path exe_;
    template<class F>Availability attempt(F&& f){try{f();return {true,{}};}catch(const winrt::hresult_error&){return {false,"Malformed USB backend data or missing Windows JSON runtime"};}catch(const std::exception& e){return {false,e.what()};}}
    void no_server(){Handle mutex(OpenMutexW(SYNCHRONIZE,FALSE,L"Global\\usbipd-{A8256F62-728F-49B0-82BB-E5E48F83D28F}"));if(mutex.value)throw std::runtime_error("An independent USB host service is running; leave it unchanged and stop it explicitly before sharing");check(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot verify USB host service ownership");}
public:
    ~NativeRunner()override{stop();release();}
    Availability ready()override{return attempt([&]{exe_=verified_executable();backend_installed();});}
    std::vector<HostDevice> devices()override{
        const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);check(SUCCEEDED(com)||com==RPC_E_CHANGED_MODE,"Cannot initialize USB state parser");struct Com{HRESULT result;~Com(){if(SUCCEEDED(result))CoUninitialize();}}com_scope{com};
        const auto availability=ready();check(availability.available,availability.reason.c_str());
        const auto raw=command(exe_,{L"state"});check(raw.size()<=1024*1024,"USB state exceeded limit");const auto document=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(raw));const auto list=document.GetNamedArray(L"Devices");check(list.Size()<=1024,"Too many USB devices");std::vector<HostDevice> output;std::set<std::wstring> ids;
        for(const auto& value:list){auto item=value.GetObject();HostDevice device;device.instance_id=text(item,L"InstanceId");device.bus_id=winrt::to_string(text(item,L"BusId"));device.binding_id=text(item,L"PersistedGuid");if(!device.binding_id.empty())device.binding_id=canonical_guid(device.binding_id);device.forced=item.GetNamedBoolean(L"IsForced");device.attached=!text(item,L"ClientIPAddress").empty()||!registry_binding(device.instance_id,true).empty();check(instance_valid(device.instance_id)&&(device.bus_id.empty()||bus_valid(device.bus_id))&&ids.insert(device.instance_id).second,"Malformed or duplicate USB identity");if(!device.binding_id.empty()){GUID guid{};check(SUCCEEDED(CLSIDFromString(device.binding_id.c_str(),&guid)),"Malformed USB binding GUID");}output.push_back(std::move(device));}return output;
    }
    Availability reserve(const std::wstring& instance)override{return attempt([&]{check(!claim_&&instance_valid(instance),"Invalid USB claim");const auto ready_state=ready();check(ready_state.available,ready_state.reason.c_str());no_server();auto normalized=instance;std::transform(normalized.begin(),normalized.end(),normalized.begin(),[](wchar_t c){return static_cast<wchar_t>(towupper(c));});const auto bytes=std::span(reinterpret_cast<const unsigned char*>(normalized.data()),normalized.size()*sizeof(wchar_t));const auto name=L"Global\\ExtraPane.UsbSource."+sha256(bytes);const auto handle=CreateMutexW(nullptr,FALSE,name.c_str());check(handle!=nullptr,"Cannot reserve selected USB source");if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(handle);throw std::runtime_error("Selected USB source is owned by another session");}claim_=handle;instance_=instance;});}
    Availability bind(const std::string& bus)override{return attempt([&]{check(claim_&&bus_valid(bus)&&owned_.empty(),"USB claim missing or already bound");no_server();const auto all=devices();const auto found=std::find_if(all.begin(),all.end(),[&](const HostDevice& d){return same(d.instance_id,instance_)&&d.bus_id==bus;});check(found!=all.end()&&found->binding_id.empty()&&!found->attached&&!found->forced&&registry_binding(instance_).empty(),"Selected USB identity changed or belongs to another binding");command(exe_,{L"extrapane-bind",L"--busid",as_wide(bus),L"--instance-id",instance_});owned_=registry_binding(instance_);check(!owned_.empty(),"USB bind did not produce an owned registry identity");});}
    Availability unbind(const std::wstring& guid)override{return attempt([&]{check(claim_&&!owned_.empty()&&same(canonical_guid(guid),owned_)&&same(registry_binding(instance_),owned_),"Refusing to remove an unowned USB binding");check(!running(),"Stop the owned exporter before unbinding");command(exe_,{L"unbind",L"--guid",owned_});check(registry_binding(instance_).empty(),"Owned USB binding was not removed");owned_.clear();});}
    Availability start(const std::wstring& instance,const std::string& bus,std::uint16_t port)override{auto result=attempt([&]{check(claim_&&same(instance,instance_)&&bus_valid(bus)&&port&&!running()&&!owned_.empty()&&same(registry_binding(instance_),owned_),"USB export has no exact owned binding");no_server();exe_=verified_executable();start_host_driver();server_.start(exe_,{L"server",L"usbipd:Port="+std::to_wstring(port),L"usbipd:AllowedBusId="+as_wide(bus),L"usbipd:AllowedInstanceId="+instance,L"Logging:LogLevel:Default=None"});std::string output;const auto deadline=GetTickCount64()+15000;while(server_.running()&&GetTickCount64()<deadline){output+=server_.drain();check(output.size()<=1024*1024,"USB exporter output exceeded limit");if(output.find("USBIPD_LOOPBACK_READY")!=std::string::npos)return;Sleep(25);}server_.stop();throw std::runtime_error("Private USB exporter did not confirm loopback readiness");});if(!result.available)server_.stop();return result;}
    void stop()noexcept override{server_.stop();}
    void release()noexcept override{if(claim_){CloseHandle(claim_);claim_=nullptr;}instance_.clear();/* Binding deliberately retained for explicit recovery if unbind was not safe. */}
    bool running()const noexcept override{return server_.running();}
};
class NativeImportRunner final:public ImportRunner {
    fs::path exe_;
    unsigned owned_=0;std::uint16_t local_=0;std::string bus_,serial_;
public:
    Availability ready()override{
        try{
            const auto root=protected_root().parent_path()/L"Microphone";verify_security(root);const auto marker=root/L"extrapane-microphone-v1.txt";verify_security(marker);
            const auto data=read_file(marker);std::string expected="ExtraPane Microphone v1\nUSBip 0.9.8.1\n";
            for(auto name:{L"usbip.exe",L"libusbip.dll",L"resources.dll"}){verify_security(root/name);const auto hash=sha256(read_file(root/name));expected+=winrt::to_string(hash)+"\n";}
            check(std::string(data.begin(),data.end())==expected,"Managed USB client hash/ownership mismatch");check(registry_key(L"SYSTEM\\CurrentControlSet\\Services\\usbip2_ude"),"USB/IP client driver missing");exe_=root/L"usbip.exe";return {true,{}};
        }catch(const std::exception& e){return {false,e.what()};}
    }
    unsigned attach(std::uint16_t port,const std::string& bus,const std::string& serial)override{
        check(!owned_&&port&&bus_valid(bus)&&serial.size()==15&&std::all_of(serial.begin(),serial.end(),[](char c){return (c>='0'&&c<='9')||(c>='A'&&c<='Z');}),"Invalid USB attachment scope");const auto state=ready();check(state.available,state.reason.c_str());
        auto output=command(exe_,{L"--tcp-port",std::to_wstring(port),L"attach",L"--remote",L"127.0.0.1",L"--bus-id",as_wide(bus),L"--serial",as_wide(serial),L"--once",L"--terse"});
        while(!output.empty()&&(output.back()=='\r'||output.back()=='\n'))output.pop_back();check(!output.empty()&&output.size()<=3&&std::all_of(output.begin(),output.end(),[](char c){return c>='0'&&c<='9';}),"USB attach returned no exact owned port");const auto value=std::stoul(output);check(value>=1&&value<=255,"Invalid owned USB port");owned_=static_cast<unsigned>(value);local_=port;bus_=bus;serial_=serial;return owned_;
    }
    bool verify_owned(unsigned port,std::uint16_t local,const std::string& bus,const std::string& serial)override{
        if(!owned_||port!=owned_||local!=local_||bus!=bus_||serial!=serial_)return false;
        const auto output=command(exe_,{L"port",std::to_wstring(port)});const auto uri="-> usbip://127.0.0.1:"+std::to_string(local)+"/"+bus;const auto identity="-> serial: "+serial;
        std::istringstream lines(output);std::string line;unsigned headers=0,uris=0,serials=0;const auto heading="Port "+(port<10?std::string("0"):std::string{})+std::to_string(port)+":";
        while(std::getline(lines,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();const auto begin=line.find_first_not_of(' ');if(begin!=line.npos)line.erase(0,begin);if(line.starts_with("Port ")){if(!line.starts_with(heading+" device in use at "))return false;++headers;}if(line.starts_with("-> usbip://")){if(line!=uri)return false;++uris;}if(line.starts_with("-> serial:")){if(line!=identity)return false;++serials;}}
        return headers==1&&uris==1&&serials==1;
    }
    void detach_ejected(unsigned port,std::uint16_t local,const std::string& bus,const std::string& serial)override{
        check(verify_owned(port,local,bus,serial),"USB port no longer belongs to this session");command(exe_,{L"detach",L"--port",std::to_wstring(port)});owned_=0;
    }
};

}
std::unique_ptr<HostRunner> native_host_runner(){return std::make_unique<NativeRunner>();}
std::unique_ptr<ImportRunner> native_import_runner(){return std::make_unique<NativeImportRunner>();}
}
