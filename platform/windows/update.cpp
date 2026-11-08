#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winrt/Windows.Data.Json.h>
#include <dm/update.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <tuple>

namespace dm::update {
namespace {
constexpr std::uint64_t max_installer=256ull*1024*1024;
constexpr wchar_t api_base[]=L"https://api.github.com/repos/EEkebin/ExtraPane/releases";
struct Internet {HINTERNET handle=nullptr;explicit Internet(HINTERNET h):handle(h){if(!h)throw std::runtime_error("Cannot open the update connection");}~Internet(){WinHttpCloseHandle(handle);}operator HINTERNET()const{return handle;}};
struct File {HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
struct Version {
    unsigned year=0,month=0,day=0,revision=0;bool stable=false;std::uint64_t run=0,attempt=0;
    auto key()const{return std::tuple(year,month,day,revision,stable,run,attempt);}
};
std::uint64_t number(const std::string& text){std::uint64_t value=0;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);if(error!=std::errc{}||end!=text.data()+text.size())throw std::runtime_error("Invalid release version");return value;}
Version parse(std::string text){
    if(text.starts_with('v'))text.erase(0,1);std::smatch m;
    if(!std::regex_match(text,m,std::regex(R"((\d{4})\.(\d{2})\.(\d{2})(?:\.([1-9]\d*))?(?:-pre\.(\d+)(?:\.(\d+))?)?)")))throw std::runtime_error("Unsupported release version");
    Version v;v.year=static_cast<unsigned>(number(m[1]));v.month=static_cast<unsigned>(number(m[2]));v.day=static_cast<unsigned>(number(m[3]));
    if(!std::chrono::year_month_day(std::chrono::year(static_cast<int>(v.year)),std::chrono::month(v.month),std::chrono::day(v.day)).ok())throw std::runtime_error("Invalid release date");
    if(m[4].matched){const auto revision=number(m[4]);if(revision<2||revision>65535)throw std::runtime_error("Invalid release revision");v.revision=static_cast<unsigned>(revision);}
    v.stable=!m[5].matched;if(!v.stable){v.run=number(m[5]);if(m[6].matched){v.attempt=number(m[6]);if(!v.attempt)throw std::runtime_error("Invalid release attempt");}}
    return v;
}
bool digest_valid(const std::string& value){return value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::wstring widen(const std::string& s){return {s.begin(),s.end()};}
struct Url {std::wstring host,path;};
Url split_url(const std::wstring& text){
    URL_COMPONENTS c{};c.dwStructSize=sizeof(c);c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=c.dwUserNameLength=c.dwPasswordLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(text.c_str(),static_cast<DWORD>(text.size()),0,&c)||c.nScheme!=INTERNET_SCHEME_HTTPS||c.nPort!=443||c.dwUserNameLength||c.dwPasswordLength)throw std::runtime_error("Update URL is not a permitted HTTPS address");
    Url result{{c.lpszHostName,c.dwHostNameLength},{c.lpszUrlPath,c.dwUrlPathLength}};if(c.dwExtraInfoLength)result.path.append(c.lpszExtraInfo,c.dwExtraInfoLength);
    std::transform(result.host.begin(),result.host.end(),result.host.begin(),[](wchar_t ch){return static_cast<wchar_t>(towlower(ch));});return result;
}
void cancelled(const std::atomic<bool>& cancel){if(cancel.load())throw std::runtime_error("Update cancelled");}
std::vector<unsigned char> get(std::wstring address,const wchar_t* accept,std::uint64_t limit,const std::atomic<bool>& cancel,std::atomic<unsigned>* progress=nullptr,std::uint64_t expected=0){
    Internet session(WinHttpOpen(L"ExtraPane-Updater",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session,10000,10000,15000,15000);const auto until=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    for(unsigned redirect=0;redirect<5;++redirect){
        cancelled(cancel);if(!allowed_download_url(address))throw std::runtime_error("Update download redirected outside GitHub's release hosts");const auto url=split_url(address);
        Internet connection(WinHttpConnect(session,url.host.c_str(),443,0));Internet request(WinHttpOpenRequest(connection,L"GET",url.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)))throw std::runtime_error("Cannot secure update redirects");
        std::wstring headers=L"Accept: "+std::wstring(accept)+L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if(!WinHttpSendRequest(request,headers.c_str(),static_cast<DWORD>(headers.size()),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("Cannot reach GitHub. Check your connection and try again.");
        DWORD status=0,size=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("Invalid update response");
        if(status==301||status==302||status==303||status==307||status==308){std::array<wchar_t,8192> location{};size=static_cast<DWORD>(sizeof(location));if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,location.data(),&size,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("Invalid update redirect");address=location.data();continue;}
        if(status==404)throw std::runtime_error("Releases are unavailable. This repository may require GitHub access.");
        if(status==401||status==403)throw std::runtime_error("GitHub access was denied or its request limit was reached. Try again later.");
        if(status!=200)throw std::runtime_error("GitHub returned HTTP "+std::to_string(status));
        std::vector<unsigned char> bytes;std::array<unsigned char,65536> chunk{};
        for(;;){cancelled(cancel);if(std::chrono::steady_clock::now()>=until)throw std::runtime_error("Update download timed out");DWORD count=0;if(!WinHttpReadData(request,chunk.data(),static_cast<DWORD>(chunk.size()),&count))throw std::runtime_error("Update download was interrupted");if(!count)break;if(bytes.size()+count>limit)throw std::runtime_error("Update download exceeds its size limit");bytes.insert(bytes.end(),chunk.begin(),chunk.begin()+count);if(progress&&expected)progress->store(static_cast<unsigned>(std::min<std::uint64_t>(99,bytes.size()*100/expected)));}
        return bytes;
    }
    throw std::runtime_error("Too many update redirects");
}
std::filesystem::path cache_root(){PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path)))throw std::runtime_error("Cannot locate the update cache");std::filesystem::path root(path);CoTaskMemFree(path);return root/L"ExtraPane"/L"Updates";}
}
int compare_versions(const std::string& left,const std::string& right){const auto a=parse(left).key(),b=parse(right).key();return a<b?-1:a>b?1:0;}
bool allowed_download_url(const std::wstring& address){try{const auto url=split_url(address);return (url.host==L"api.github.com"&&url.path.starts_with(L"/repos/EEkebin/ExtraPane/releases"))||url.host==L"release-assets.githubusercontent.com"||url.host==L"objects.githubusercontent.com"||url.host==L"github-releases.githubusercontent.com";}catch(...){return false;}}
std::optional<Release> select_release(const std::string& text,Channel channel){
    using namespace winrt::Windows::Data::Json;const auto releases=JsonArray::Parse(winrt::to_hstring(text));std::optional<Release> selected;
    for(const auto& entry:releases){
        const auto release=entry.GetObject();if(release.GetNamedBoolean(L"draft",false))continue;const bool pre=release.GetNamedBoolean(L"prerelease",false);if(pre!=(channel==Channel::prerelease))continue;
        const auto tag=winrt::to_string(release.GetNamedString(L"tag_name",L""));Version parsed;try{parsed=parse(tag);}catch(...){continue;}if(parsed.stable==pre||!tag.starts_with('v'))continue;
        const std::string version=tag.substr(1),filename="ExtraPane-"+version+"-Setup-win-x64.exe",legacy_filename="SideScreen-"+version+"-Setup-win-x64.exe";
        for(const auto& item:release.GetNamedArray(L"assets",JsonArray{})){
            const auto asset=item.GetObject();const auto asset_name=winrt::to_string(asset.GetNamedString(L"name",L""));if((asset_name!=filename&&asset_name!=legacy_filename)||asset.GetNamedString(L"state",L"")!=L"uploaded")continue;
            auto digest=winrt::to_string(asset.GetNamedString(L"digest",L""));if(!digest.starts_with("sha256:"))continue;digest.erase(0,7);if(!digest_valid(digest))continue;
            const double size=asset.GetNamedNumber(L"size",0),id=asset.GetNamedNumber(L"id",0);if(size<1024||size>max_installer||id<1||id>9007199254740991.0||size!=static_cast<std::uint64_t>(size)||id!=static_cast<std::uint64_t>(id))continue;
            Release candidate{version,tag,std::to_string(static_cast<std::uint64_t>(id)),digest,static_cast<std::uint64_t>(size),pre};if(!selected||compare_versions(candidate.version,selected->version)>0)selected=std::move(candidate);
        }
    }return selected;
}
std::optional<Release> check(Channel channel,const std::atomic<bool>& cancel){
    std::optional<Release> selected;
    for(unsigned page=1;page<=10;++page){const auto bytes=get(std::wstring(api_base)+L"?per_page=100&page="+std::to_wstring(page),L"application/vnd.github+json",8*1024*1024,cancel);const std::string text(bytes.begin(),bytes.end());auto candidate=select_release(text,channel);if(candidate&&(!selected||compare_versions(candidate->version,selected->version)>0))selected=std::move(candidate);if(winrt::Windows::Data::Json::JsonArray::Parse(winrt::to_hstring(text)).Size()<100)return selected;}
    throw std::runtime_error("Release history is too large to check completely; use the GitHub releases link.");
}
std::string sha256(const std::vector<unsigned char>& bytes){
    std::array<unsigned char,32> hash{};if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),hash.data(),static_cast<ULONG>(hash.size()))<0)throw std::runtime_error("Cannot verify the update checksum");std::string out;for(auto byte:hash){out+="0123456789abcdef"[byte>>4];out+="0123456789abcdef"[byte&15];}return out;
}
std::filesystem::path download(const Release& release,const std::atomic<bool>& cancel,std::atomic<unsigned>& progress){
    (void)parse(release.version);if(release.size<1024||release.size>max_installer||!digest_valid(release.digest)||release.asset_id.empty()||!std::all_of(release.asset_id.begin(),release.asset_id.end(),[](char c){return c>='0'&&c<='9';}))throw std::runtime_error("Invalid update metadata");
    const auto bytes=get(std::wstring(api_base)+L"/assets/"+widen(release.asset_id),L"application/octet-stream",release.size,cancel,&progress,release.size);
    if(bytes.size()!=release.size||sha256(bytes)!=release.digest)throw std::runtime_error("The installer checksum did not match. Nothing was installed.");cancelled(cancel);
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create update cache");wchar_t id[40]{};StringFromGUID2(guid,id,40);const auto folder=cache_root()/id;std::filesystem::create_directories(folder);const auto target=folder/(L"ExtraPane-"+widen(release.version)+L"-Setup-win-x64.exe");
    std::ofstream file(target,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));file.close();if(!file){std::filesystem::remove(target);std::filesystem::remove(folder);throw std::runtime_error("Could not save the update installer");}progress.store(100);return target;
}
void launch_installer_after_exit(unsigned parent,const std::filesystem::path& installer,const std::string& digest){
    const auto full=std::filesystem::absolute(installer).lexically_normal();const auto root=cache_root().lexically_normal();const auto relative=full.lexically_relative(root);if(relative.empty()||*relative.begin()==L".."||full.extension()!=L".exe"||!full.filename().wstring().starts_with(L"ExtraPane-")||!digest_valid(digest))throw std::runtime_error("Invalid installer cache path");
    File process;process.handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent);if(process.handle==nullptr){process.handle=INVALID_HANDLE_VALUE;throw std::runtime_error("ExtraPane is not running");}
    wchar_t source[32768]{},self[32768]{};DWORD count=32768;GetModuleFileNameW(nullptr,self,32768);if(!QueryFullProcessImageNameW(process.handle,0,source,&count)||_wcsicmp(source,(std::filesystem::path(self).parent_path()/L"ExtraPane.exe").c_str())!=0)throw std::runtime_error("Unexpected update launcher");
    if(WaitForSingleObject(process.handle,60000)!=WAIT_OBJECT_0)throw std::runtime_error("ExtraPane did not exit; installer was not started");
    File lock;lock.handle=CreateFileW(full.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);if(lock.handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Downloaded installer is unavailable");LARGE_INTEGER length{};if(!GetFileSizeEx(lock.handle,&length)||length.QuadPart<1024||length.QuadPart>static_cast<LONGLONG>(max_installer))throw std::runtime_error("Invalid installer size");std::vector<unsigned char> bytes(static_cast<size_t>(length.QuadPart));DWORD read=0;if(!ReadFile(lock.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=bytes.size()||sha256(bytes)!=digest)throw std::runtime_error("Installer checksum changed; nothing was installed");
    SHELLEXECUTEINFOW launch{sizeof(launch)};launch.fMask=SEE_MASK_NOCLOSEPROCESS;launch.lpFile=full.c_str();launch.nShow=SW_SHOWNORMAL;if(!ShellExecuteExW(&launch))throw std::runtime_error("Could not start the update installer");if(launch.hProcess){WaitForSingleObject(launch.hProcess,INFINITE);CloseHandle(launch.hProcess);}CloseHandle(lock.handle);lock.handle=INVALID_HANDLE_VALUE;std::error_code ignored;std::filesystem::remove(full,ignored);std::filesystem::remove(full.parent_path(),ignored);
}
}
