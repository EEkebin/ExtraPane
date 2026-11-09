#include <windows.h>
#include <dm/camera_endpoint.hpp>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <future>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void check(bool ok){if(!ok)throw std::runtime_error("Camera DLL activation check failed");}
ComPtr<IMFMediaEvent> next(IMFMediaEventGenerator* source,MediaEventType expected){
    ComPtr<IMFMediaEvent> event;check(SUCCEEDED(source->GetEvent(MF_EVENT_FLAG_NO_WAIT,&event)));
    MediaEventType type{};check(SUCCEEDED(event->GetType(&type))&&type==expected);return event;
}
int wmain(int argc,wchar_t** argv){try{
    check(argc==2);check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));struct Com{~Com(){CoUninitialize();}} com;
    check(SUCCEEDED(MFStartup(MF_VERSION)));struct Mf{~Mf(){MFShutdown();}} mf;
    HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);check(dll!=nullptr);
    struct Library{HMODULE dll;~Library(){FreeLibrary(dll);}} library{dll};
    auto get=reinterpret_cast<HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,void**)>(GetProcAddress(dll,"DllGetClassObject"));
    auto unload=reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll,"DllCanUnloadNow"));check(get&&unload);check(unload()==S_OK);
    CLSID id{};check(SUCCEEDED(CLSIDFromString(dm::camera::source_class_id,&id)));
    ComPtr<IClassFactory> factory;check(get(GUID_NULL,IID_PPV_ARGS(&factory))==CLASS_E_CLASSNOTAVAILABLE);
    check(SUCCEEDED(get(id,IID_PPV_ARGS(&factory))));check(unload()==S_FALSE);
    ComPtr<IMFActivate> activation;check(SUCCEEDED(factory->CreateInstance(nullptr,IID_PPV_ARGS(&activation))));
    struct SourceOwner{ComPtr<IMFMediaSourceEx> value;~SourceOwner(){if(value)value->Shutdown();}} source;
    check(FAILED(activation->ActivateObject(IID_PPV_ARGS(&source.value))));check(!source.value);
    dm::camera::FramePublisher publisher({4,4,30});publisher.start();
    check(SUCCEEDED(activation->SetString(dm::camera::pipe_attribute,publisher.name().c_str())));
    check(SUCCEEDED(MFSetAttributeSize(activation.Get(),MF_MT_FRAME_SIZE,4,4)));
    check(SUCCEEDED(MFSetAttributeRatio(activation.Get(),MF_MT_FRAME_RATE,30,1)));
    check(SUCCEEDED(activation->ActivateObject(IID_PPV_ARGS(&source.value))));
    ComPtr<IMFPresentationDescriptor> pd;check(SUCCEEDED(source.value->CreatePresentationDescriptor(&pd)));
    PROPVARIANT position{};check(SUCCEEDED(source.value->Start(pd.Get(),nullptr,&position)));
    auto announced=next(source.value.Get(),MENewStream);PROPVARIANT value{};check(SUCCEEDED(announced->GetValue(&value)));
    ComPtr<IMFMediaStream> stream;check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&stream))));PropVariantClear(&value);
    announced.Reset();next(source.value.Get(),MESourceStarted);next(stream.Get(),MEStreamStarted);
    // Repeated Start must reuse the existing subscriber, not close/reopen its
    // pipe or attempt to start its worker twice.
    check(SUCCEEDED(source.value->Start(pd.Get(),nullptr,&position)));
    next(source.value.Get(),MEUpdatedStream);next(source.value.Get(),MESourceStarted);next(stream.Get(),MEStreamStarted);
    check(SUCCEEDED(stream->RequestSample(nullptr)));
    auto delivery=std::async(std::launch::async,[stream]{ComPtr<IMFMediaEvent> event;auto hr=stream->GetEvent(0,&event);return std::pair{hr,event};});
    const auto beforeDelivery=MFGetSystemTime();publisher.submit(std::vector<std::uint8_t>(24,51),4567);
    if(delivery.wait_for(std::chrono::seconds(2))!=std::future_status::ready){source.value->Shutdown();throw std::runtime_error("DLL camera sample delivery timed out");}
    auto [hr,event]=delivery.get();check(SUCCEEDED(hr));MediaEventType type{};check(SUCCEEDED(event->GetType(&type))&&type==MEMediaSample);
    check(SUCCEEDED(event->GetValue(&value)));ComPtr<IMFSample> sample;check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));PropVariantClear(&value);
    LONGLONG timestamp=0;check(SUCCEEDED(sample->GetSampleTime(&timestamp))&&timestamp>=beforeDelivery&&timestamp<=MFGetSystemTime());
    check(SUCCEEDED(source.value->Stop()));next(stream.Get(),MEStreamStopped);next(source.value.Get(),MESourceStopped);
    check(SUCCEEDED(source.value->Start(pd.Get(),nullptr,&position)));
    next(source.value.Get(),MEUpdatedStream);next(source.value.Get(),MESourceStarted);next(stream.Get(),MEStreamStarted);
    check(SUCCEEDED(stream->RequestSample(nullptr)));
    auto resumed=std::async(std::launch::async,[stream]{ComPtr<IMFMediaEvent> event;auto hr=stream->GetEvent(0,&event);return std::pair{hr,event};});
    // Reuse the same publisher without submitting another frame: a restarted
    // source must reconnect even if its previous reader was waiting for data.
    if(resumed.wait_for(std::chrono::seconds(2))!=std::future_status::ready){source.value->Shutdown();throw std::runtime_error("Camera restart did not reconnect");}
    auto [resumedHr,resumedEvent]=resumed.get();check(SUCCEEDED(resumedHr));check(SUCCEEDED(resumedEvent->GetType(&type))&&type==MEMediaSample);resumedEvent.Reset();
    check(SUCCEEDED(activation->DetachObject()));activation.Reset();factory.Reset();check(unload()==S_FALSE);
    auto failure=std::async(std::launch::async,[s=source.value]{ComPtr<IMFMediaEvent> event;auto hr=s->GetEvent(0,&event);return std::pair{hr,event};});
    publisher.stop();
    if(failure.wait_for(std::chrono::seconds(2))!=std::future_status::ready){source.value->Shutdown();throw std::runtime_error("Camera disconnect was not reported");}
    auto [failureHr,failureEvent]=failure.get();check(SUCCEEDED(failureHr));check(SUCCEEDED(failureEvent->GetType(&type))&&type==MEError);
    next(stream.Get(),MEError);check(FAILED(stream->RequestSample(nullptr)));failureEvent.Reset();
    check(SUCCEEDED(source.value->Stop()));next(stream.Get(),MEStreamStopped);next(source.value.Get(),MESourceStopped);
    source.value->Shutdown();source.value.Reset();stream.Reset();pd.Reset();event.Reset();sample.Reset();publisher.stop();
    check(unload()==S_OK);
    // Frame Server may release IMFActivate immediately after obtaining the source.
    // Its retained source must remain usable without an explicit DetachObject.
    check(SUCCEEDED(get(id,IID_PPV_ARGS(&factory))));
    check(SUCCEEDED(factory->CreateInstance(nullptr,IID_PPV_ARGS(&activation))));
    dm::camera::FramePublisher retainedPublisher({4,4,30});retainedPublisher.start();
    check(SUCCEEDED(activation->SetString(dm::camera::pipe_attribute,retainedPublisher.name().c_str())));
    check(SUCCEEDED(MFSetAttributeSize(activation.Get(),MF_MT_FRAME_SIZE,4,4)));
    check(SUCCEEDED(MFSetAttributeRatio(activation.Get(),MF_MT_FRAME_RATE,30,1)));
    check(SUCCEEDED(activation->ActivateObject(IID_PPV_ARGS(&source.value))));
    ComPtr<IMFMediaSourceEx> extended;check(SUCCEEDED(source.value.As(&extended)));
    ComPtr<IMFAttributes> inherited;check(SUCCEEDED(extended->GetSourceAttributes(&inherited)));
    UINT32 inheritedWidth=0,inheritedHeight=0;check(SUCCEEDED(MFGetAttributeSize(inherited.Get(),MF_MT_FRAME_SIZE,&inheritedWidth,&inheritedHeight))&&inheritedWidth==4&&inheritedHeight==4);
    wchar_t inheritedPipe[128]{};check(SUCCEEDED(inherited->GetString(dm::camera::pipe_attribute,inheritedPipe,128,nullptr))&&retainedPublisher.name()==inheritedPipe);
    inherited.Reset();extended.Reset();
    activation.Reset();factory.Reset();
    DWORD characteristics=0;check(SUCCEEDED(source.value->GetCharacteristics(&characteristics)));
    check(SUCCEEDED(source.value->CreatePresentationDescriptor(&pd)));
    check(SUCCEEDED(source.value->Shutdown()));source.value.Reset();pd.Reset();
    retainedPublisher.stop();check(unload()==S_OK);
    std::cout<<"Unregistered camera DLL activation, pipe delivery and COM ownership passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
