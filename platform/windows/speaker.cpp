#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <dm/speaker.hpp>
#include <extrapane/speaker_jitter.hpp>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace dm::speaker {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr,const char* message){if(FAILED(hr))throw std::runtime_error(std::string(message)+" (HRESULT "+std::to_string(static_cast<unsigned long>(hr))+")");}
struct Com {HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Com(){if(result!=RPC_E_CHANGED_MODE)check(result,"Initialize speaker COM");}~Com(){if(SUCCEEDED(result))CoUninitialize();}};
struct Event {HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);Event(){if(!value)throw std::runtime_error("Create speaker event failed");}~Event(){CloseHandle(value);}};
ComPtr<IMMDeviceEnumerator> enumerator(){ComPtr<IMMDeviceEnumerator> result;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&result)),"Enumerate playback devices");return result;}
std::wstring identity(IMMDevice* device){LPWSTR id=nullptr;check(device->GetId(&id),"Read output identity");std::wstring result=id;CoTaskMemFree(id);return result;}
}
std::vector<Device> enumerate(){
    Com com;auto query=enumerator();ComPtr<IMMDeviceCollection> collection;check(query->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection),"List speakers");
    UINT count=0;check(collection->GetCount(&count),"Count speakers");std::vector<Device> result;
    for(UINT i=0;i<count;++i){ComPtr<IMMDevice> device;check(collection->Item(i,&device),"Read speaker");auto id=identity(device.Get()),name=id;ComPtr<IPropertyStore> properties;
        if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))){PROPVARIANT value;PropVariantInit(&value);if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)name=value.pwszVal;PropVariantClear(&value);}
        result.push_back({std::move(id),std::move(name)});
    }return result;
}
struct Playback::State {
    std::wstring device;std::atomic_bool& mute;std::atomic_uint& volume;std::atomic_uint64_t& muteEpoch;std::atomic_bool stopping=false;
    std::thread worker;std::mutex mutex;std::condition_variable changed;
    struct Packet {extrapane::speaker_wire::Frame frame;std::uint64_t epoch;};std::deque<Packet> pending;bool ready=false;std::string error;
    State(std::wstring id,std::atomic_bool& m,std::atomic_uint& v,std::atomic_uint64_t& e):device(std::move(id)),mute(m),volume(v),muteEpoch(e){}
    void run()noexcept{
        try{
            Com com;auto query=enumerator();ComPtr<IMMDevice> output;check(query->GetDevice(device.c_str(),&output),"Selected speaker is unavailable");
            ComPtr<IMMEndpoint> endpoint;check(output.As(&endpoint),"Read speaker direction");EDataFlow direction;check(endpoint->GetDataFlow(&direction),"Read speaker direction");if(direction!=eRender)throw std::runtime_error("Select a playback output");
            DWORD state=0;check(output->GetState(&state),"Read speaker state");if(!(state&DEVICE_STATE_ACTIVE))throw std::runtime_error("Selected speaker is not active");
            ComPtr<IAudioClient> audio;check(output->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,&audio),"Open selected speaker");
            WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
            check(audio->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_NOPERSIST|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,200000,0,&format,nullptr),"Initialize stereo speaker playback");
            Event event;check(audio->SetEventHandle(event.value),"Set speaker event");UINT32 capacity=0;check(audio->GetBufferSize(&capacity),"Read speaker buffer size");if(!capacity||capacity>48000)throw std::runtime_error("Speaker buffer exceeds supported limit");
            ComPtr<IAudioRenderClient> render;check(audio->GetService(IID_PPV_ARGS(&render)),"Open speaker render service");
            BYTE* initial=nullptr;check(render->GetBuffer(capacity,&initial),"Prime speaker output");check(render->ReleaseBuffer(capacity,AUDCLNT_BUFFERFLAGS_SILENT),"Prime speaker silence");
            check(audio->Start(),"Start speaker output");struct Stop{IAudioClient* value;~Stop(){value->Stop();value->Reset();}}stop{audio.Get()};
            {std::lock_guard lock(mutex);ready=true;changed.notify_all();}
            extrapane::speaker_wire::JitterBuffer jitter;std::vector<std::int16_t> samples(capacity*2);unsigned timeouts=0;auto epoch=muteEpoch.load();
            while(!stopping){
                const auto waited=WaitForSingleObject(event.value,100);if(waited==WAIT_TIMEOUT){if(++timeouts>=20)throw std::runtime_error("Selected speaker stopped consuming audio");continue;}if(waited!=WAIT_OBJECT_0)throw std::runtime_error("Speaker event failed");timeouts=0;
                const auto currentEpoch=muteEpoch.load();const bool muted=mute.load();const bool boundary=currentEpoch!=epoch;if(boundary){jitter.reset();epoch=currentEpoch;check(audio->Stop(),"Pause speaker at mute boundary");check(audio->Reset(),"Clear queued speaker audio at mute boundary");}
                // Only this WASAPI worker owns jitter state. Packet transfer takes
                // a short try-lock; network/transport never run on this thread.
                {std::unique_lock lock(mutex,std::try_to_lock);if(lock.owns_lock()){while(!pending.empty()){if(!muted&&pending.front().epoch==epoch)jitter.accept(pending.front().frame);pending.pop_front();}}}
                UINT32 padding=0;check(audio->GetCurrentPadding(&padding),"Read speaker padding");if(padding>capacity)throw std::runtime_error("Invalid speaker padding");const auto available=capacity-padding;if(!available)continue;
                auto pcm=std::span(samples).first(available*2);if(muted)std::fill(pcm.begin(),pcm.end(),0);else jitter.render(pcm);
                const auto gain=std::min(volume.load(),100u);for(auto& sample:pcm)sample=static_cast<std::int16_t>(static_cast<int>(sample)*static_cast<int>(gain)/100);
                BYTE* bytes=nullptr;check(render->GetBuffer(available,&bytes),"Acquire speaker buffer");std::copy_n(reinterpret_cast<const BYTE*>(pcm.data()),available*4,bytes);// Recheck immediately before submission: a control transition may have
                // arrived while the worker rendered/copied this chunk. The next
                // event also resets WASAPI's previously submitted buffer.
                const bool stale=mute.load()||muteEpoch.load()!=epoch;
                if(stale)jitter.reset();
                check(render->ReleaseBuffer(available,stale?AUDCLNT_BUFFERFLAGS_SILENT:0),"Submit speaker audio");if(boundary)check(audio->Start(),"Resume speaker after mute boundary");
            }
        }catch(const std::exception& e){std::lock_guard lock(mutex);error=e.what();changed.notify_all();}
        catch(...){std::lock_guard lock(mutex);error="Speaker playback failed";changed.notify_all();}
    }
};
Playback::Playback(std::wstring device,std::atomic_bool& mute,std::atomic_uint& volume,std::atomic_uint64_t& muteEpoch):state_(std::make_unique<State>(std::move(device),mute,volume,muteEpoch)){
    auto& s=*state_;if(s.device.empty()||s.device.size()>4096)throw std::invalid_argument("Select a local speaker");s.worker=std::thread([&s]{s.run();});
    std::unique_lock lock(s.mutex);const bool signaled=s.changed.wait_for(lock,std::chrono::seconds(10),[&]{return s.ready||!s.error.empty();});
    if(!signaled||!s.error.empty()){const auto error=s.error.empty()?"Speaker startup timed out":s.error;lock.unlock();stop();throw std::runtime_error(error);}
}
Playback::~Playback(){stop();}
void Playback::push(const extrapane::speaker_wire::Frame& frame,std::uint64_t receivedEpoch){auto& s=*state_;std::lock_guard lock(s.mutex);if(!s.error.empty())throw std::runtime_error(s.error);if(s.stopping||s.mute||receivedEpoch!=s.muteEpoch.load())return;auto next=frame;if(s.pending.size()>=12){s.pending.clear();next.discontinuity=true;}if(s.mute){s.pending.clear();return;}s.pending.push_back({std::move(next),receivedEpoch});}
void Playback::stop()noexcept{auto& s=*state_;s.stopping=true;if(s.worker.joinable())s.worker.join();std::lock_guard lock(s.mutex);s.pending.clear();}
}
