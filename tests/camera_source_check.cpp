#include <dm/camera_media_source.hpp>
#include <mfapi.h>
#include <mferror.h>
#include <iostream>
#include <stdexcept>
#include <future>
using Microsoft::WRL::ComPtr;
void check(bool ok){if(!ok)throw std::runtime_error("Camera media source check failed");}
ComPtr<IMFMediaEvent> event(IMFMediaEventGenerator* source,MediaEventType wanted){
    ComPtr<IMFMediaEvent> result;check(SUCCEEDED(source->GetEvent(MF_EVENT_FLAG_NO_WAIT,&result)));
    MediaEventType type{};check(SUCCEEDED(result->GetType(&type))&&type==wanted);return result;
}
void empty(IMFMediaEventGenerator* source){ComPtr<IMFMediaEvent> result;check(source->GetEvent(MF_EVENT_FLAG_NO_WAIT,&result)==MF_E_NO_EVENTS_AVAILABLE);}
int main(){try{
    check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
    struct ComCleanup{~ComCleanup(){CoUninitialize();}} com;
    check(SUCCEEDED(MFStartup(MF_VERSION)));
    struct MfCleanup{~MfCleanup(){MFShutdown();}} mf;
    dm::camera::MediaSource feed({4,4,30});auto source=feed.source();
    ComPtr<IMFMediaSource> base;check(SUCCEEDED(source.As(&base)));
    ComPtr<IMFPresentationDescriptor> pd;check(SUCCEEDED(source->CreatePresentationDescriptor(&pd)));
    check(source->Pause()==MF_E_INVALID_STATE_TRANSITION);
    PROPVARIANT start{};check(SUCCEEDED(source->Start(pd.Get(),nullptr,&start)));
    auto announced=event(source.Get(),MENewStream);PROPVARIANT value{};
    check(SUCCEEDED(announced->GetValue(&value))&&value.vt==VT_UNKNOWN);
    ComPtr<IMFMediaStream2> stream;check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&stream))));PropVariantClear(&value);
    event(source.Get(),MESourceStarted);event(stream.Get(),MEStreamStarted);
    ComPtr<IMFMediaSource> parent;check(SUCCEEDED(stream->GetMediaSource(&parent))&&parent.Get()==base.Get());
    check(SUCCEEDED(stream->RequestSample(nullptr)));empty(stream.Get());
    const std::vector<std::uint8_t> pixels(24,65);feed.submit(pixels,1000);
    auto delivered=event(stream.Get(),MEMediaSample);
    check(SUCCEEDED(delivered->GetValue(&value))&&value.vt==VT_UNKNOWN);
    ComPtr<IMFSample> sample;check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));PropVariantClear(&value);
    LONGLONG timestamp=0;check(SUCCEEDED(sample->GetSampleTime(&timestamp))&&timestamp==1000);
    ComPtr<IMFMediaBuffer> buffer;check(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)));
    BYTE* data=nullptr;DWORD length=0;check(SUCCEEDED(buffer->Lock(&data,nullptr,&length)));
    const bool correct=length==24&&data[0]==65&&data[23]==65;buffer->Unlock();check(correct);
    // Raw-frame replacement keeps the newest image without growing the queue.
    feed.submit(pixels,2000);feed.submit(pixels,3000);
    ComPtr<IMFAttributes> token;check(SUCCEEDED(MFCreateAttributes(&token,0)));
    check(SUCCEEDED(stream->RequestSample(token.Get())));delivered=event(stream.Get(),MEMediaSample);
    check(SUCCEEDED(delivered->GetValue(&value)));sample.Reset();check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));PropVariantClear(&value);
    check(SUCCEEDED(sample->GetSampleTime(&timestamp))&&timestamp==3000);
    ComPtr<IMFAttributes> returnedToken;check(SUCCEEDED(sample->GetUnknown(MFSampleExtension_Token,IID_PPV_ARGS(&returnedToken)))&&returnedToken.Get()==token.Get());
    for(int i=0;i<4;++i)check(SUCCEEDED(stream->RequestSample(nullptr)));
    check(stream->RequestSample(nullptr)==MF_E_NOTACCEPTING);
    check(SUCCEEDED(source->Stop()));event(stream.Get(),MEStreamStopped);event(source.Get(),MESourceStopped);
    check(stream->RequestSample(nullptr)==MF_E_INVALIDREQUEST);
    feed.submit(pixels,4000);empty(stream.Get());
    check(SUCCEEDED(source->Start(pd.Get(),nullptr,&start)));event(source.Get(),MEUpdatedStream);event(source.Get(),MESourceStarted);event(stream.Get(),MEStreamStarted);
    check(SUCCEEDED(stream->RequestSample(nullptr)));empty(stream.Get()); // old requests/frames cleared
    feed.submit(pixels,5000);event(stream.Get(),MEMediaSample);
    auto waiting=std::async(std::launch::async,[stream]{
        ComPtr<IMFMediaEvent> next;return stream->GetEvent(0,&next);
    });
    feed.stop();feed.stop();DWORD characteristics=0;
    check(waiting.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    check(waiting.get()==MF_E_SHUTDOWN);
    check(source->GetCharacteristics(&characteristics)==MF_E_SHUTDOWN);
    check(stream->RequestSample(nullptr)==MF_E_SHUTDOWN);
    std::cout<<"Native camera source sample delivery and lifecycle checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
