#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <ks.h>
#include <ksmedia.h>
#include <devicetopology.h>
#include <wrl.h>
#include <dm/camera_media_source.hpp>
#include <deque>
#include <mutex>
#include <cstring>
#include <stdexcept>

namespace dm::camera {
using namespace Microsoft::WRL;
namespace {
void checked(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Camera media source operation failed");}
struct Shared {
    std::mutex mutex;
    bool shutdown=false,started=false,announced=false;
    MF_STREAM_STATE streamState=MF_STREAM_STATE_STOPPED;
    Format format;
    ComPtr<IMFMediaEventQueue> sourceEvents,streamEvents;
    ComPtr<IMFAttributes> sourceAttributes,streamAttributes;
    ComPtr<IMFPresentationDescriptor> presentation;
    ComPtr<IMFStreamDescriptor> descriptor;
    ComPtr<IMFMediaType> type;
    ComPtr<IMFMediaSource> parent;
    std::deque<ComPtr<IUnknown>> requests;
    ComPtr<IMFSample> latest;
    // All calls hold mutex. Only decoded raw frames are replaced.
    HRESULT deliver() {
        if(!latest||requests.empty()||!started||streamState!=MF_STREAM_STATE_RUNNING)return S_OK;
        auto token=std::move(requests.front());requests.pop_front();
        auto sample=std::move(latest);
        if(token){const auto hr=sample->SetUnknown(MFSampleExtension_Token,token.Get());if(FAILED(hr))return hr;}
        return streamEvents->QueueEventParamUnk(MEMediaSample,GUID_NULL,S_OK,sample.Get());
    }
};
// GetEvent must never hold the source lock while waiting for an event.
#define CAMERA_EVENTS(member) \
    STDMETHODIMP GetEvent(DWORD flags,IMFMediaEvent** out) override { \
        if(!out)return E_POINTER;*out=nullptr;ComPtr<IMFMediaEventQueue> q; \
        {std::lock_guard lock(s_->mutex);if(s_->shutdown)return MF_E_SHUTDOWN;q=s_->member;} \
        return q->GetEvent(flags,out); } \
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback,IUnknown* state) override { \
        std::lock_guard lock(s_->mutex);if(s_->shutdown)return MF_E_SHUTDOWN; \
        return s_->member->BeginGetEvent(callback,state); } \
    STDMETHODIMP EndGetEvent(IMFAsyncResult* result,IMFMediaEvent** out) override { \
        std::lock_guard lock(s_->mutex);if(s_->shutdown)return MF_E_SHUTDOWN; \
        return s_->member->EndGetEvent(result,out); } \
    STDMETHODIMP QueueEvent(MediaEventType type,REFGUID extended,HRESULT status,const PROPVARIANT* value) override { \
        std::lock_guard lock(s_->mutex);if(s_->shutdown)return MF_E_SHUTDOWN; \
        return s_->member->QueueEventParamVar(type,extended,status,value); }

class Stream final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<IMFMediaStream2,IMFMediaStream,IMFMediaEventGenerator>> {
public:
    explicit Stream(std::shared_ptr<Shared> state):s_(std::move(state)){}
    CAMERA_EVENTS(streamEvents)
    STDMETHODIMP GetMediaSource(IMFMediaSource** out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(s_->mutex);
        if(s_->shutdown)return MF_E_SHUTDOWN;return s_->parent.CopyTo(out);
    }
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(s_->mutex);
        if(s_->shutdown)return MF_E_SHUTDOWN;return s_->descriptor.CopyTo(out);
    }
    STDMETHODIMP RequestSample(IUnknown* token) override {
        std::lock_guard lock(s_->mutex);
        if(s_->shutdown)return MF_E_SHUTDOWN;
        if(!s_->started||s_->streamState!=MF_STREAM_STATE_RUNNING)return MF_E_INVALIDREQUEST;
        if(s_->requests.size()>=4)return MF_E_NOTACCEPTING;
        try{s_->requests.emplace_back(token);}catch(...){return E_OUTOFMEMORY;}
        return s_->deliver();
    }
    STDMETHODIMP SetStreamState(MF_STREAM_STATE value) override {
        std::lock_guard lock(s_->mutex);if(s_->shutdown)return MF_E_SHUTDOWN;
        if(value!=MF_STREAM_STATE_STOPPED&&value!=MF_STREAM_STATE_RUNNING)return MF_E_INVALID_STATE_TRANSITION;
        s_->streamState=value;
        if(value==MF_STREAM_STATE_STOPPED){s_->requests.clear();s_->latest.Reset();}
        return S_OK;
    }
    STDMETHODIMP GetStreamState(MF_STREAM_STATE* value) override {
        if(!value)return E_POINTER;std::lock_guard lock(s_->mutex);
        if(s_->shutdown)return MF_E_SHUTDOWN;*value=s_->streamState;return S_OK;
    }
private:std::shared_ptr<Shared> s_;
};
class Source final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    ChainInterfaces<IMFMediaSourceEx,IMFMediaSource,IMFMediaEventGenerator>,IMFGetService,IKsControl> {
public:
    Source(std::shared_ptr<Shared> state,ComPtr<Stream> stream):s_(std::move(state)),stream_(std::move(stream)){}
    CAMERA_EVENTS(sourceEvents)
    STDMETHODIMP GetCharacteristics(DWORD* value) override {
        if(!value)return E_POINTER;std::lock_guard lock(s_->mutex);
        if(s_->shutdown)return MF_E_SHUTDOWN;*value=MFMEDIASOURCE_IS_LIVE;return S_OK;
    }
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(s_->mutex);
        if(s_->shutdown)return MF_E_SHUTDOWN;return s_->presentation->Clone(out);
    }
    STDMETHODIMP Start(IMFPresentationDescriptor* pd,const GUID* timeFormat,const PROPVARIANT* position) override {
        if(!pd||!position)return E_POINTER;
        if(timeFormat&&*timeFormat!=GUID_NULL)return MF_E_UNSUPPORTED_TIME_FORMAT;
        if(position->vt!=VT_EMPTY && (position->vt!=VT_I8||position->hVal.QuadPart!=0))return MF_E_INVALIDREQUEST;
        std::lock_guard lock(s_->mutex);if(s_->shutdown)return MF_E_SHUTDOWN;
        if(s_->started)return MF_E_INVALID_STATE_TRANSITION;
        DWORD count=0,id=0;BOOL selected=FALSE;ComPtr<IMFStreamDescriptor> descriptor;
        auto hr=pd->GetStreamDescriptorCount(&count);if(FAILED(hr))return hr;
        if(count!=1)return MF_E_INVALIDSTREAMNUMBER;
        hr=pd->GetStreamDescriptorByIndex(0,&selected,&descriptor);if(FAILED(hr))return hr;
        hr=descriptor->GetStreamIdentifier(&id);if(FAILED(hr))return hr;
        if(!selected||id!=0)return MF_E_INVALIDSTREAMNUMBER;
        ComPtr<IMFMediaTypeHandler> handler;ComPtr<IMFMediaType> type;
        hr=descriptor->GetMediaTypeHandler(&handler);if(FAILED(hr))return hr;
        hr=handler->GetCurrentMediaType(&type);if(FAILED(hr))return hr;
        DWORD equal=0;hr=s_->type->IsEqual(type.Get(),&equal);if(hr!=S_OK)return MF_E_INVALIDMEDIATYPE;
        PROPVARIANT start{};start.vt=VT_I8;start.hVal.QuadPart=0;
        hr=s_->sourceEvents->QueueEventParamUnk(s_->announced?MEUpdatedStream:MENewStream,GUID_NULL,S_OK,stream_.Get());
        if(FAILED(hr))return hr;
        s_->announced=true;s_->started=true;s_->streamState=MF_STREAM_STATE_RUNNING;
        hr=s_->streamEvents->QueueEventParamVar(MEStreamStarted,GUID_NULL,S_OK,&start);
        if(FAILED(hr))return hr;
        return s_->sourceEvents->QueueEventParamVar(MESourceStarted,GUID_NULL,S_OK,&start);
    }
    STDMETHODIMP Stop() override {
        std::lock_guard lock(s_->mutex);if(s_->shutdown)return MF_E_SHUTDOWN;
        if(!s_->started)return S_OK;
        s_->started=false;s_->streamState=MF_STREAM_STATE_STOPPED;s_->latest.Reset();s_->requests.clear();
        auto hr=s_->streamEvents->QueueEventParamVar(MEStreamStopped,GUID_NULL,S_OK,nullptr);
        if(FAILED(hr))return hr;
        return s_->sourceEvents->QueueEventParamVar(MESourceStopped,GUID_NULL,S_OK,nullptr);
    }
    STDMETHODIMP Pause() override {
        std::lock_guard lock(s_->mutex);return s_->shutdown?MF_E_SHUTDOWN:MF_E_INVALID_STATE_TRANSITION;
    }
    STDMETHODIMP Shutdown() override {
        ComPtr<IMFMediaSource> parent;
        {std::lock_guard lock(s_->mutex);if(s_->shutdown)return S_OK;
         s_->shutdown=true;s_->started=false;s_->latest.Reset();s_->requests.clear();
         s_->streamEvents->Shutdown();s_->sourceEvents->Shutdown();parent=std::move(s_->parent);}
        return S_OK;
    }
    STDMETHODIMP GetSourceAttributes(IMFAttributes** out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(s_->mutex);
        if(s_->shutdown)return MF_E_SHUTDOWN;return s_->sourceAttributes.CopyTo(out);
    }
    STDMETHODIMP GetStreamAttributes(DWORD id,IMFAttributes** out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(s_->mutex);
        if(s_->shutdown)return MF_E_SHUTDOWN;if(id!=0)return MF_E_INVALIDSTREAMNUMBER;
        return s_->streamAttributes.CopyTo(out);
    }
    STDMETHODIMP SetD3DManager(IUnknown*) override {
        std::lock_guard lock(s_->mutex);return s_->shutdown?MF_E_SHUTDOWN:S_OK;
    }
    STDMETHODIMP GetService(REFGUID,REFIID,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(s_->mutex);
        return s_->shutdown?MF_E_SHUTDOWN:MF_E_UNSUPPORTED_SERVICE;
    }
    STDMETHODIMP KsProperty(PKSPROPERTY,ULONG,void*,ULONG,ULONG* bytes) override {return unsupported(bytes);}
    STDMETHODIMP KsMethod(PKSMETHOD,ULONG,void*,ULONG,ULONG* bytes) override {return unsupported(bytes);}
    STDMETHODIMP KsEvent(PKSEVENT,ULONG,void*,ULONG,ULONG* bytes) override {return unsupported(bytes);}
private:
    HRESULT unsupported(ULONG* bytes){if(bytes)*bytes=0;std::lock_guard lock(s_->mutex);return s_->shutdown?MF_E_SHUTDOWN:HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);}
    std::shared_ptr<Shared> s_;ComPtr<Stream> stream_;
};
#undef CAMERA_EVENTS
}
struct MediaSource::State {std::shared_ptr<Shared> shared;ComPtr<Source> source;};
MediaSource::MediaSource(Format format):state_(std::make_unique<State>()) {
    validate_format(format);auto s=std::make_shared<Shared>();s->format=format;
    checked(MFCreateEventQueue(&s->sourceEvents));checked(MFCreateEventQueue(&s->streamEvents));
    checked(MFCreateAttributes(&s->sourceAttributes,2));checked(MFCreateAttributes(&s->streamAttributes,3));
    checked(s->sourceAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    checked(s->sourceAttributes->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,L"ExtraPane Camera"));
    checked(s->streamAttributes->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY,PINNAME_VIDEO_CAPTURE));
    checked(s->streamAttributes->SetUINT32(MF_DEVICESTREAM_STREAM_ID,0));
    checked(MFCreateMediaType(&s->type));
    checked(s->type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));checked(s->type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12));
    checked(MFSetAttributeSize(s->type.Get(),MF_MT_FRAME_SIZE,format.width,format.height));
    checked(MFSetAttributeRatio(s->type.Get(),MF_MT_FRAME_RATE,format.fps,1));
    checked(MFSetAttributeRatio(s->type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
    checked(s->type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
    checked(s->type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT,TRUE));
    checked(s->type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES,TRUE));
    checked(s->type->SetUINT32(MF_MT_SAMPLE_SIZE,format.width*format.height*3/2));
    checked(s->type->SetUINT32(MF_MT_DEFAULT_STRIDE,format.width));
    IMFMediaType* types[]={s->type.Get()};checked(MFCreateStreamDescriptor(0,1,types,&s->descriptor));
    ComPtr<IMFMediaTypeHandler> handler;checked(s->descriptor->GetMediaTypeHandler(&handler));checked(handler->SetCurrentMediaType(s->type.Get()));
    IMFStreamDescriptor* streams[]={s->descriptor.Get()};checked(MFCreatePresentationDescriptor(1,streams,&s->presentation));
    checked(s->presentation->SelectStream(0));
    auto stream=Make<Stream>(s);if(!stream)throw std::bad_alloc();
    auto source=Make<Source>(s,stream);if(!source)throw std::bad_alloc();
    // The owning facade always calls Shutdown, breaking the stream/source cycle.
    checked(source.As(&s->parent));state_->shared=std::move(s);state_->source=std::move(source);
}
MediaSource::~MediaSource(){stop();}
ComPtr<IMFMediaSourceEx> MediaSource::source()const {ComPtr<IMFMediaSourceEx> result;checked(state_->source.As(&result));return result;}
void MediaSource::stop()noexcept {if(state_&&state_->source)state_->source->Shutdown();}
void MediaSource::submit(std::span<const std::uint8_t> pixels,std::int64_t timestamp) {
    auto s=state_->shared;const auto length=s->format.width*s->format.height*3/2;
    if(pixels.size()!=length)throw std::invalid_argument("Virtual camera NV12 frame size mismatch");
    ComPtr<IMFMediaBuffer> buffer;checked(MFCreateMemoryBuffer(length,&buffer));
    BYTE* bytes=nullptr;checked(buffer->Lock(&bytes,nullptr,nullptr));
    std::memcpy(bytes,pixels.data(),length);checked(buffer->Unlock());checked(buffer->SetCurrentLength(length));
    ComPtr<IMFSample> sample;checked(MFCreateSample(&sample));checked(sample->AddBuffer(buffer.Get()));
    checked(sample->SetSampleTime(timestamp));checked(sample->SetSampleDuration(10000000/s->format.fps));
    std::lock_guard lock(s->mutex);if(s->shutdown)throw std::runtime_error("Virtual camera source shut down");
    if(!s->started)return;
    s->latest=std::move(sample);checked(s->deliver());
}
}
