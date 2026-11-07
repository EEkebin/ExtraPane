#include <windows.h>
#include <sddl.h>
#include <dm/camera_pipe.hpp>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace dm::camera {
namespace {
constexpr wchar_t prefix[]=L"\\\\.\\pipe\\ExtraPane.Camera.";
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    Handle()=default;explicit Handle(HANDLE v):value(v){if(v==INVALID_HANDLE_VALUE||!v)throw std::runtime_error("Camera pipe handle failed");}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
struct Local {void* value=nullptr;~Local(){if(value)LocalFree(value);}};
std::vector<std::uint8_t> token_user(HANDLE token){
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
    if(!size||size>65536)throw std::runtime_error("Camera pipe identity unavailable");
    std::vector<std::uint8_t> data(size);
    if(!GetTokenInformation(token,TokenUser,data.data(),size,&size))throw std::runtime_error("Camera pipe identity unavailable");
    return data;
}
std::vector<std::uint8_t> current_user(){
    HANDLE raw=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))throw std::runtime_error("Camera pipe owner unavailable");
    Handle token(raw);return token_user(token.value);
}
PSID sid(std::vector<std::uint8_t>& data){return reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;}
std::wstring pipe_name(){
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Camera pipe identity generation failed");
    wchar_t text[40]{};StringFromGUID2(guid,text,40);return std::wstring(prefix)+text;
}
void validate_name(const std::wstring& name){
    const std::wstring beginning(prefix);
    if(name.size()!=beginning.size()+38||name.compare(0,beginning.size(),beginning)!=0)
        throw std::invalid_argument("Invalid local camera pipe");
    const auto tail=name.substr(beginning.size());GUID value{};
    if(tail.front()!=L'{'||tail.back()!=L'}'||FAILED(CLSIDFromString(tail.c_str(),&value)))throw std::invalid_argument("Invalid local camera pipe identity");
}
void wait_io(HANDLE pipe,OVERLAPPED& operation,HANDLE stop,DWORD timeout){
    const HANDLE events[]={stop,operation.hEvent};
    const auto result=WaitForMultipleObjects(2,events,FALSE,timeout);
    if(result!=WAIT_OBJECT_0+1){
        CancelIoEx(pipe,&operation);DWORD ignored=0;GetOverlappedResult(pipe,&operation,&ignored,TRUE);
        throw std::runtime_error("Camera pipe interrupted or timed out");
    }
}
void transfer(HANDLE pipe,HANDLE stop,void* data,DWORD size,bool write){
    auto* cursor=static_cast<BYTE*>(data);
    while(size){
        if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0)throw std::runtime_error("Camera pipe stopped");
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED op{};op.hEvent=event.value;DWORD done=0;
        const auto ok=write?WriteFile(pipe,cursor,size,&done,&op):ReadFile(pipe,cursor,size,&done,&op);
        if(!ok){if(GetLastError()!=ERROR_IO_PENDING)throw std::runtime_error("Camera pipe disconnected");wait_io(pipe,op,stop,3000);}
        if(!GetOverlappedResult(pipe,&op,&done,FALSE)||!done||done>size)throw std::runtime_error("Camera pipe transfer failed");
        cursor+=done;size-=done;
    }
}
void put(std::array<std::uint8_t,32>& bytes,std::size_t offset,std::uint64_t value,unsigned count){
    for(unsigned i=0;i<count;++i)bytes[offset+count-1-i]=static_cast<std::uint8_t>(value>>(i*8));
}
std::uint64_t get(const std::array<std::uint8_t,32>& bytes,std::size_t offset,unsigned count){
    std::uint64_t value=0;for(unsigned i=0;i<count;++i)value=(value<<8)|bytes[offset+i];return value;
}
struct RawFrame {std::vector<std::uint8_t> pixels;std::int64_t timestamp;std::uint64_t sequence;};
}
struct FramePublisher::State {
    Format format;std::wstring name=pipe_name();
    Handle stopEvent{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Handle pipe;
    std::thread worker;std::mutex mutex;std::condition_variable changed;
    std::shared_ptr<const RawFrame> latest;std::uint64_t sequence=0;bool stopped=false;std::string error;
    std::vector<std::uint8_t> owner;
    explicit State(Format f):format(f){validate_format(f);}
    bool canceled(){return WaitForSingleObject(stopEvent.value,0)==WAIT_OBJECT_0;}
    void run(){
        while(!canceled()){
            try{
                Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED op{};op.hEvent=event.value;
                if(!ConnectNamedPipe(pipe.value,&op)){
                    const auto error=GetLastError();
                    if(error==ERROR_IO_PENDING){wait_io(pipe.value,op,stopEvent.value,INFINITE);DWORD ignored=0;if(!GetOverlappedResult(pipe.value,&op,&ignored,FALSE))throw std::runtime_error("Camera pipe accept failed");}
                    else if(error!=ERROR_PIPE_CONNECTED)throw std::runtime_error("Camera pipe accept failed");
                }
                std::optional<std::uint64_t> previous;
                while(!canceled()){
                    BYTE request=0;transfer(pipe.value,stopEvent.value,&request,1,false);
                    if(request!=1)throw std::runtime_error("Invalid camera frame request");
                    // ACLs are the first boundary. Impersonation confirms the
                    // connected local client before any image bytes are sent.
                    if(!ImpersonateNamedPipeClient(pipe.value))throw std::runtime_error("Camera reader identity unavailable");
                    bool allowed=false;
                    {struct Revert{~Revert(){if(!RevertToSelf())std::terminate();}} revert;
                     HANDLE raw=nullptr;if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&raw)){
                        Handle token(raw);auto user=token_user(token.value);
                        allowed=EqualSid(sid(user),sid(owner))||IsWellKnownSid(sid(user),WinLocalServiceSid)||IsWellKnownSid(sid(user),WinLocalSystemSid);
                     }}
                    if(!allowed)throw std::runtime_error("Camera reader denied");
                    std::shared_ptr<const RawFrame> frame;
                    {std::unique_lock lock(mutex);
                     while(!changed.wait_for(lock,std::chrono::milliseconds(100),[&]{return stopped||(latest&&(!previous||latest->sequence!=*previous));})){
                        lock.unlock();const auto connected=PeekNamedPipe(pipe.value,nullptr,0,nullptr,nullptr,nullptr);lock.lock();
                        if(!connected)throw std::runtime_error("Camera reader disconnected while awaiting a frame");
                     }
                     if(stopped)break;frame=latest;}
                    std::array<std::uint8_t,32> header{};header[0]='E';header[1]='P';header[2]='N';header[3]=1;
                    put(header,4,format.width,4);put(header,8,format.height,4);put(header,12,format.fps,4);
                    put(header,16,std::bit_cast<std::uint64_t>(frame->timestamp),8);put(header,24,frame->pixels.size(),4);
                    transfer(pipe.value,stopEvent.value,header.data(),32,true);
                    transfer(pipe.value,stopEvent.value,const_cast<std::uint8_t*>(frame->pixels.data()),static_cast<DWORD>(frame->pixels.size()),true);
                    previous=frame->sequence;
                }
            }catch(const std::exception& e){if(!canceled()){std::lock_guard lock(mutex);error=e.what();}}
            catch(...){if(!canceled()){std::lock_guard lock(mutex);error="Camera publisher failed";}}
            DisconnectNamedPipe(pipe.value);
        }
    }
};
FramePublisher::FramePublisher(Format format):state_(std::make_unique<State>(format)){}
FramePublisher::~FramePublisher(){stop();}
const std::wstring& FramePublisher::name()const{return state_->name;}
std::string FramePublisher::last_error()const{std::lock_guard lock(state_->mutex);return state_->error;}
void FramePublisher::start(){
    auto& s=*state_;if(s.worker.joinable()||s.stopped)throw std::runtime_error("Camera publisher already started or stopped");
    s.owner=current_user();Local text;
    if(!ConvertSidToStringSidW(sid(s.owner),reinterpret_cast<LPWSTR*>(&text.value)))throw std::runtime_error("Camera owner SID unavailable");
    const auto acl=std::wstring(L"D:P(A;;GRGW;;;SY)(A;;GRGW;;;LS)(A;;GRGW;;;")+static_cast<wchar_t*>(text.value)+L")";
    Local descriptor;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value),nullptr))throw std::runtime_error("Camera pipe security failed");
    SECURITY_ATTRIBUTES security{sizeof(security),descriptor.value,FALSE};
    s.pipe.value=CreateNamedPipeW(s.name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&security);
    if(s.pipe.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Create local camera pipe failed");
    try{s.worker=std::thread([&s]{s.run();});}catch(...){CloseHandle(s.pipe.value);s.pipe.value=INVALID_HANDLE_VALUE;throw;}
}
void FramePublisher::submit(std::span<const std::uint8_t> pixels,std::int64_t timestamp){
    auto& s=*state_;if(pixels.size()!=static_cast<std::size_t>(s.format.width)*s.format.height*3/2)throw std::invalid_argument("Invalid pipe NV12 frame length");
    auto frame=std::make_shared<RawFrame>();frame->pixels.assign(pixels.begin(),pixels.end());frame->timestamp=timestamp;
    std::lock_guard lock(s.mutex);if(s.stopped)throw std::runtime_error("Camera publisher stopped");
    frame->sequence=s.sequence++;s.latest=std::move(frame);s.changed.notify_all();
}
void FramePublisher::stop()noexcept{
    auto& s=*state_;{std::lock_guard lock(s.mutex);s.stopped=true;s.latest.reset();SetEvent(s.stopEvent.value);s.changed.notify_all();}
    if(s.worker.joinable())s.worker.join();
    if(s.pipe.value!=INVALID_HANDLE_VALUE){CloseHandle(s.pipe.value);s.pipe.value=INVALID_HANDLE_VALUE;}
}
struct FrameSubscriber::State {
    std::wstring name;Format format;Sink sink;Failure failure;
    Handle stopEvent{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::thread worker;std::atomic_bool failed=false;bool stopped=false;std::mutex mutex;std::string error;
    State(std::wstring n,Format f,Sink callback,Failure onFailure):name(std::move(n)),format(f),sink(std::move(callback)),failure(std::move(onFailure)){
        validate_name(name);validate_format(format);if(!sink)throw std::invalid_argument("Missing camera pipe sink");
    }
    void run(){
        const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(initialized)){failed=true;try{if(failure)failure();}catch(...){}return;}
        struct ComCleanup{~ComCleanup(){CoUninitialize();}} cleanup;
        try{
            // Identification permits the SID check but prevents the pipe server
            // from acting with the client's Windows credentials.
            Handle pipe;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            for(;;){
                pipe.value=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
                if(pipe.value!=INVALID_HANDLE_VALUE)break;
                const auto error=GetLastError();
                if((error!=ERROR_PIPE_BUSY&&error!=ERROR_FILE_NOT_FOUND)||std::chrono::steady_clock::now()>=deadline)
                    throw std::runtime_error("Local camera publisher is unavailable");
                if(WaitForSingleObject(stopEvent.value,25)==WAIT_OBJECT_0)throw std::runtime_error("Camera subscriber stopped");
            }
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(format.width)*format.height*3/2);
            while(WaitForSingleObject(stopEvent.value,0)!=WAIT_OBJECT_0){
                BYTE request=1;transfer(pipe.value,stopEvent.value,&request,1,true);
                std::array<std::uint8_t,32> header{};transfer(pipe.value,stopEvent.value,header.data(),32,false);
                if(header[0]!='E'||header[1]!='P'||header[2]!='N'||header[3]!=1||get(header,4,4)!=format.width||
                   get(header,8,4)!=format.height||get(header,12,4)!=format.fps||get(header,24,4)!=pixels.size()||get(header,28,4)!=0)
                    throw std::runtime_error("Invalid local camera frame header");
                transfer(pipe.value,stopEvent.value,pixels.data(),static_cast<DWORD>(pixels.size()),false);
                sink(pixels,std::bit_cast<std::int64_t>(get(header,16,8)));
            }
        }catch(const std::exception& e){if(WaitForSingleObject(stopEvent.value,0)!=WAIT_OBJECT_0){std::lock_guard lock(mutex);error=e.what();failed=true;}}
        catch(...){if(WaitForSingleObject(stopEvent.value,0)!=WAIT_OBJECT_0){std::lock_guard lock(mutex);error="Camera subscriber failed";failed=true;}}
        if(failed){try{if(failure)failure();}catch(...){}}
    }
};
FrameSubscriber::FrameSubscriber(std::wstring name,Format format,Sink sink,Failure failure):state_(std::make_unique<State>(std::move(name),format,std::move(sink),std::move(failure))){}
FrameSubscriber::~FrameSubscriber(){stop();}
void FrameSubscriber::start(){auto& s=*state_;if(s.worker.joinable()||s.stopped)throw std::runtime_error("Camera subscriber already started or stopped");s.worker=std::thread([&s]{s.run();});}
void FrameSubscriber::stop()noexcept{auto& s=*state_;s.stopped=true;SetEvent(s.stopEvent.value);if(s.worker.joinable())s.worker.join();}
bool FrameSubscriber::failed()const noexcept{return state_->failed;}
std::string FrameSubscriber::last_error()const{std::lock_guard lock(state_->mutex);return state_->error;}
}
