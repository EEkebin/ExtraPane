#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <dm/camera.hpp>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <atomic>
#include <algorithm>
#include <tuple>

namespace dm::camera {
using Microsoft::WRL::ComPtr;
namespace {
void checked(HRESULT hr,const char* operation){if(FAILED(hr))throw std::runtime_error(std::string(operation)+" (HRESULT "+std::to_string(static_cast<unsigned long>(hr))+")");}
struct Runtime {
    Runtime(){checked(MFStartup(MF_VERSION),"Initialize camera media runtime");}
    ~Runtime(){MFShutdown();}
};
std::wstring attribute(IMFAttributes* attributes,REFGUID key){
    wchar_t* value=nullptr;UINT32 length=0;checked(attributes->GetAllocatedString(key,&value,&length),"Read camera identity");
    struct Free{wchar_t* value;~Free(){CoTaskMemFree(value);}} free{value};
    return {value,length};
}
struct Pending {
    std::mutex mutex;std::condition_variable changed;
    bool running=true,pending=false,ready=false;
    HRESULT status=S_OK;Sample sample;
};
class Callback final:public IMFSourceReaderCallback {
public:
    explicit Callback(std::shared_ptr<Pending> pending):pending_(std::move(pending)){}
    STDMETHODIMP QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IMFSourceReaderCallback)){*out=static_cast<IMFSourceReaderCallback*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()override{return ++refs_;}
    STDMETHODIMP_(ULONG) Release()override{const auto refs=--refs_;if(!refs)delete this;return refs;}
    STDMETHODIMP OnReadSample(HRESULT status,DWORD,DWORD flags,LONGLONG timestamp,IMFSample* sample)override{
        std::lock_guard lock(pending_->mutex);
        if(!pending_->running||FAILED(pending_->status))return S_OK;
        pending_->pending=false;pending_->ready=true;pending_->status=status;
        if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)pending_->status=MF_E_INVALIDMEDIATYPE;
        if(flags&MF_SOURCE_READERF_ERROR)pending_->status=E_FAIL;
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)pending_->status=MF_E_END_OF_STREAM;
        pending_->sample={};pending_->sample.native=sample;pending_->sample.flags=flags;pending_->sample.timestamp=timestamp;
        pending_->changed.notify_all();return S_OK;
    }
    STDMETHODIMP OnFlush(DWORD)override{return S_OK;}
    STDMETHODIMP OnEvent(DWORD,IMFMediaEvent* event)override{
        if(!event)return S_OK;HRESULT status=S_OK;
        if(SUCCEEDED(event->GetStatus(&status))&&FAILED(status)){
            std::lock_guard lock(pending_->mutex);if(!pending_->running||FAILED(pending_->status))return S_OK;
            pending_->pending=false;pending_->ready=true;pending_->status=status;pending_->sample={};pending_->changed.notify_all();
        }return S_OK;
    }
private:
    std::atomic_ulong refs_{1};std::shared_ptr<Pending> pending_;
};
}
std::vector<Device> enumerate(){
    Runtime runtime;ComPtr<IMFAttributes> attributes;checked(MFCreateAttributes(&attributes,1),"Create camera query");
    checked(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),"Set camera query");
    struct Devices{IMFActivate** items=nullptr;UINT32 size=0;~Devices(){for(UINT32 i=0;i<size;++i)items[i]->Release();CoTaskMemFree(items);}} devices;
    checked(MFEnumDeviceSources(attributes.Get(),&devices.items,&devices.size),"Enumerate cameras");
    std::vector<Device> result;
    for(UINT32 i=0;i<devices.size;++i)result.push_back({attribute(devices.items[i],MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK),attribute(devices.items[i],MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME)});
    return result;
}
struct Capture::State {
    Runtime runtime;
    std::shared_ptr<Pending> pending=std::make_shared<Pending>();
    ComPtr<IMFMediaSource> source;ComPtr<IMFSourceReader> reader;Format format;
    ~State(){stop();}
    void stop()noexcept{
        ComPtr<IMFSourceReader> closingReader;ComPtr<IMFMediaSource> closingSource;
        {std::lock_guard lock(pending->mutex);pending->running=false;pending->sample={};pending->ready=false;closingReader=std::move(reader);closingSource=std::move(source);pending->changed.notify_all();}
        if(closingReader)closingReader->Flush(MF_SOURCE_READER_ALL_STREAMS);
        if(closingSource)closingSource->Shutdown();
    }
};
Capture::Capture(const std::wstring& device,std::optional<Format> requested):state_(std::make_unique<State>()){
    if(requested)validate_format(*requested);
    if(device.empty())throw std::invalid_argument("Select a local camera");
    // Re-resolve against current local inventory. Never open an arbitrary path.
    bool found=false;for(const auto& local:enumerate())if(local.id==device)found=true;
    if(!found)throw std::invalid_argument("Selected camera is no longer connected");
    ComPtr<IMFAttributes> attributes;checked(MFCreateAttributes(&attributes,2),"Create camera attributes");
    checked(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),"Set camera type");
    checked(attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,device.c_str()),"Select camera");
    checked(MFCreateDeviceSource(attributes.Get(),&state_->source),"Open selected camera");
    ComPtr<IMFAttributes> options;checked(MFCreateAttributes(&options,2),"Create reader options");
    ComPtr<Callback> callback;callback.Attach(new Callback(state_->pending));
    checked(options->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,callback.Get()),"Set camera callback");
    checked(options->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE),"Enable camera format conversion");
    checked(MFCreateSourceReaderFromMediaSource(state_->source.Get(),options.Get(),&state_->reader),"Create camera reader");
    checked(state_->reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),"Deselect camera streams");
    checked(state_->reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE),"Select camera video");
    struct Mode { Format format;ComPtr<IMFMediaType> native; };
    std::vector<Mode> modes;
    for(DWORD index=0;index<1024;++index){
        ComPtr<IMFMediaType> native;
        const auto hr=state_->reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,index,&native);
        if(hr==MF_E_NO_MORE_TYPES)break;
        checked(hr,"Enumerate camera modes");
        UINT32 width=0,height=0,num=0,den=0;
        if(FAILED(MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&width,&height))||
           FAILED(MFGetAttributeRatio(native.Get(),MF_MT_FRAME_RATE,&num,&den))||!den||num%den)continue;
        const Format mode{width,height,num/den};
        try{validate_format(mode);}catch(const std::invalid_argument&){continue;}
        if(requested&&(mode.width!=requested->width||mode.height!=requested->height||mode.fps!=requested->fps))continue;
        modes.push_back({mode,std::move(native)});
    }
    // Favor normal-motion modes, then the largest native image, then frame rate.
    // No resizing or invented frame rates: the offered mode must exist on the camera.
    std::stable_sort(modes.begin(),modes.end(),[](const auto& a,const auto& b){
        const auto rank=[](Format f){return std::tuple{f.fps>=30,std::uint64_t(f.width)*f.height,f.fps};};
        return rank(a.format)>rank(b.format);
    });
    for(const auto& candidate:modes){
        const auto wanted=candidate.format;
        if(FAILED(state_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,candidate.native.Get())))continue;
        ComPtr<IMFMediaType> type;checked(MFCreateMediaType(&type),"Create camera format");
        checked(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"Set camera media type");
        checked(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12),"Set camera pixel format");
        checked(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,wanted.width,wanted.height),"Set camera dimensions");
        checked(MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,wanted.fps,1),"Set camera rate");
        if(FAILED(state_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type.Get())))continue;
        ComPtr<IMFMediaType> actual;
        if(FAILED(state_->reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual)))continue;
        UINT32 width=0,height=0,num=0,den=0;GUID subtype{};
        if(FAILED(actual->GetGUID(MF_MT_SUBTYPE,&subtype))||
           FAILED(MFGetAttributeSize(actual.Get(),MF_MT_FRAME_SIZE,&width,&height))||
           FAILED(MFGetAttributeRatio(actual.Get(),MF_MT_FRAME_RATE,&num,&den)))continue;
        if(subtype!=MFVideoFormat_NV12||width!=wanted.width||height!=wanted.height||!den||std::uint64_t(num)!=std::uint64_t(wanted.fps)*den)continue;
        state_->format=wanted;return;
    }
    throw std::runtime_error(requested?"Camera does not provide the requested native mode with NV12 conversion":
        "Camera has no usable native mode: this build supports even dimensions up to 3840x2160 and whole-number rates up to 60 FPS with NV12 conversion");
}
Capture::~Capture()=default;
Format Capture::format()const{return state_->format;}
void Capture::stop()noexcept{state_->stop();}
Sample Capture::read(std::chrono::milliseconds timeout){
    if(timeout.count()<=0)throw std::invalid_argument("Camera read needs a positive timeout");
    auto pending=state_->pending;ComPtr<IMFSourceReader> reader;bool request=false;
    {std::lock_guard lock(pending->mutex);if(!pending->running)throw std::runtime_error("Camera stopped");checked(pending->status,"Camera previously failed");reader=state_->reader;if(!pending->pending&&!pending->ready){pending->pending=true;request=true;}}
    if(request){const auto hr=reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,nullptr,nullptr,nullptr);if(FAILED(hr)){stop();checked(hr,"Request camera sample");}}
    std::unique_lock lock(pending->mutex);
    if(!pending->changed.wait_for(lock,timeout,[&]{return !pending->running||pending->ready;}))throw std::runtime_error("Camera sample timed out");
    if(!pending->running)throw std::runtime_error("Camera stopped");
    pending->ready=false;checked(pending->status,"Capture camera sample");
    return std::move(pending->sample);
}
}
