#include <dm/camera_endpoint.hpp>
#include <dm/camera_media_source.hpp>
#include <mfapi.h>
#include <mferror.h>
#include <future>
#include <iostream>
#include <stdexcept>
#include <source_location>
using Microsoft::WRL::ComPtr;
using namespace dm::camera;
void check(bool ok,const std::source_location where=std::source_location::current()){if(!ok)throw std::runtime_error("Camera endpoint check failed at line "+std::to_string(where.line()));}
struct Registration final:CameraRegistration {
    std::wstring pipe;int prepares=0,starts=0,stops=0;bool reject=false;
    extrapane::Availability prepare(const std::wstring& name,Format,unsigned slot)override{++prepares;pipe=name;check(slot==1);return {true,{}};}
    extrapane::Availability start()override{++starts;return {!reject,reject?"Simulated component failure":""};}
    void stop()noexcept override{++stops;}
};
ComPtr<IMFMediaEvent> next(IMFMediaEventGenerator* source,MediaEventType expected){
    ComPtr<IMFMediaEvent> event;check(SUCCEEDED(source->GetEvent(MF_EVENT_FLAG_NO_WAIT,&event)));
    MediaEventType type{};check(SUCCEEDED(event->GetType(&type))&&type==expected);return event;
}
int main(){try{
    check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));struct Com{~Com(){CoUninitialize();}} com;
    check(SUCCEEDED(MFStartup(MF_VERSION)));struct Mf{~Mf(){MFShutdown();}} mf;
    for(unsigned slot: {5u,255u,65536u,0xffffffffu}){CameraEndpoint capacity({4,4,30},slot,std::make_unique<Registration>());}
    auto backend=std::make_unique<Registration>();auto* recorded=backend.get();
    CameraEndpoint endpoint({4,4,30},1,std::move(backend));
    check(recorded->prepares==0&&recorded->starts==0);check(!endpoint.start().available);
    check(endpoint.prepare().available&&endpoint.start().available);
    MediaSource feed({4,4,30});auto source=feed.source();
    ComPtr<IMFPresentationDescriptor> descriptor;check(SUCCEEDED(source->CreatePresentationDescriptor(&descriptor)));
    PROPVARIANT position{};check(SUCCEEDED(source->Start(descriptor.Get(),nullptr,&position)));
    auto announced=next(source.Get(),MENewStream);PROPVARIANT value{};check(SUCCEEDED(announced->GetValue(&value)));
    ComPtr<IMFMediaStream> stream;check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&stream))));PropVariantClear(&value);
    next(source.Get(),MESourceStarted);next(stream.Get(),MEStreamStarted);check(SUCCEEDED(stream->RequestSample(nullptr)));
    std::promise<void> supplied;auto ready=supplied.get_future();
    FrameSubscriber subscriber(recorded->pipe,{4,4,30},[&](auto pixels,auto timestamp){feed.submit(pixels,timestamp);supplied.set_value();});
    subscriber.start();const auto submitted_at=MFGetSystemTime();endpoint.submit(std::vector<std::uint8_t>(24,83),1234);
    check(ready.wait_for(std::chrono::seconds(2))==std::future_status::ready);ready.get();
    auto event=next(stream.Get(),MEMediaSample);check(SUCCEEDED(event->GetValue(&value)));
    ComPtr<IMFSample> sample;check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));PropVariantClear(&value);
    LONGLONG timestamp=0;check(SUCCEEDED(sample->GetSampleTime(&timestamp))&&timestamp>=submitted_at&&timestamp<=MFGetSystemTime());
    ComPtr<IMFMediaBuffer> buffer;check(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)));BYTE* data=nullptr;DWORD length=0;
    check(SUCCEEDED(buffer->Lock(&data,nullptr,&length)));const bool correct=length==24&&data[0]==83&&data[23]==83;buffer->Unlock();check(correct);
    subscriber.stop();feed.stop();endpoint.stop();endpoint.stop();check(recorded->stops==1);
    auto failing=std::make_unique<Registration>();auto* failure=failing.get();failure->reject=true;
    CameraEndpoint rejected({4,4,30},1,std::move(failing));check(rejected.prepare().available);check(!rejected.start().available);
    check(failure->stops==1);check(!rejected.prepare().available);rejected.stop();check(failure->stops==1);
    std::cout<<"Endpoint-to-pipe-to-native-sample delivery and startup rollback passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
