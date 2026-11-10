#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <dm/microphone.hpp>
#include <dm/microphone_pcm.hpp>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace dm::microphone {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr))
        throw std::runtime_error(std::string(operation) + " (HRESULT " +
                                 std::to_string(static_cast<unsigned long>(hr)) + ")");
}
struct Com {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com() {
        if (hr != RPC_E_CHANGED_MODE) check(hr, "Initialize microphone COM");
    }
    ~Com() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(bool manual = false) : value(CreateEventW(nullptr, manual, FALSE, nullptr)) {
        if (!value) throw std::runtime_error("Create microphone event failed");
    }
    ~Handle() { CloseHandle(value); }
};
ComPtr<IMMDeviceEnumerator> enumerator() {
    ComPtr<IMMDeviceEnumerator> value;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&value)),
          "Enumerate microphone endpoints");
    return value;
}
std::wstring identity(IMMDevice* device) {
    LPWSTR id = nullptr;
    check(device->GetId(&id), "Read microphone identifier");
    struct Free {
        LPWSTR p;
        ~Free() { CoTaskMemFree(p); }
    } free{id};
    return id;
}
PcmFormat parse_format(const WAVEFORMATEX& type) {
    bool floating = type.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (type.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (type.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            throw std::runtime_error("Truncated microphone format");
        const auto& extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(type);
        if (extended.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            floating = true;
        else if (extended.SubFormat != KSDATAFORMAT_SUBTYPE_PCM)
            throw std::runtime_error("Unsupported microphone PCM subtype");
        if (extended.Samples.wValidBitsPerSample != type.wBitsPerSample)
            throw std::runtime_error("Packed microphone PCM is not supported");
    } else if (!floating && type.wFormatTag != WAVE_FORMAT_PCM)
        throw std::runtime_error("Microphone must supply PCM");
    PcmFormat format{type.nSamplesPerSec, type.nChannels, type.wBitsPerSample, floating};
    validate_pcm(format);
    if (type.nBlockAlign != format.channels * (format.bits / 8) ||
        type.nAvgBytesPerSec != type.nSamplesPerSec * type.nBlockAlign)
        throw std::runtime_error("Invalid microphone block alignment");
    return format;
}
}
std::vector<Device> enumerate() {
    Com com;
    auto query = enumerator();
    ComPtr<IMMDeviceCollection> devices;
    check(query->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices), "List microphones");
    UINT count = 0;
    check(devices->GetCount(&count), "Count microphones");
    std::vector<Device> result;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        check(devices->Item(i, &device), "Read microphone");
        auto id = identity(device.Get());
        auto name = id;
        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
                value.pwszVal)
                name = value.pwszVal;
            PropVariantClear(&value);
        }
        result.push_back({std::move(id), std::move(name)});
    }
    return result;
}
struct Capture::State {
    std::wstring id;
    Handle stop_event{true};
    std::thread worker;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Packet> queue;
    bool started = false, stopping = false, ended = false, muted = false;
    std::string error;
    explicit State(std::wstring selected) : id(std::move(selected)) {
        if (id.empty() || id.size() > 4096) throw std::invalid_argument("Select a local microphone");
    }
    void run() noexcept {
        try {
            if (WaitForSingleObject(stop_event.value, 0) == WAIT_OBJECT_0)
                throw std::runtime_error("Microphone stopped");
            Com com;
            auto query = enumerator();
            ComPtr<IMMDeviceCollection> devices;
            check(query->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices), "Refresh microphones");
            UINT count = 0;
            check(devices->GetCount(&count), "Count microphones");
            ComPtr<IMMDevice> selected;
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> device;
                check(devices->Item(i, &device), "Read microphone");
                if (identity(device.Get()) == id) {
                    selected = device;
                    break;
                }
            }
            if (!selected) throw std::runtime_error("Selected microphone is no longer connected");
            if (WaitForSingleObject(stop_event.value, 0) == WAIT_OBJECT_0)
                throw std::runtime_error("Microphone stopped");
            ComPtr<IAudioClient> audio;
            check(selected->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, &audio),
                  "Open microphone; check Windows microphone permission");
            WAVEFORMATEX* mix = nullptr;
            check(audio->GetMixFormat(&mix), "Read microphone mix format");
            struct Free {
                WAVEFORMATEX* p;
                ~Free() { CoTaskMemFree(p); }
            } free{mix};
            const auto format = parse_format(*mix);
            if (WaitForSingleObject(stop_event.value, 0) == WAIT_OBJECT_0)
                throw std::runtime_error("Microphone stopped");
            check(audio->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, 1000000, 0, mix,
                                    nullptr),
                  "Initialize shared microphone capture");
            Handle data_event;
            check(audio->SetEventHandle(data_event.value), "Set microphone capture event");
            ComPtr<IAudioCaptureClient> capture;
            check(audio->GetService(IID_PPV_ARGS(&capture)), "Get microphone capture service");
            if (WaitForSingleObject(stop_event.value, 0) == WAIT_OBJECT_0)
                throw std::runtime_error("Microphone stopped");
            check(audio->Start(), "Start microphone capture");
            struct Stop {
                IAudioClient* p;
                ~Stop() { p->Stop(); }
            } stop{audio.Get()};
            HANDLE handles[]{stop_event.value, data_event.value};
            std::uint64_t sequence = 0;
            while (true) {
                const auto wait = WaitForMultipleObjects(2, handles, FALSE, 2000);
                if (wait == WAIT_OBJECT_0) break;
                if (wait != WAIT_OBJECT_0 + 1) throw std::runtime_error("Microphone capture timed out or event failed");
                UINT32 frames = 0;
                check(capture->GetNextPacketSize(&frames), "Read microphone packet size");
                while (frames) {
                    BYTE* data = nullptr;
                    DWORD flags = 0;
                    UINT64 position = 0, qpc = 0;
                    check(capture->GetBuffer(&data, &frames, &flags, &position, &qpc), "Read microphone samples");
                    struct Release {
                        IAudioCaptureClient* p;
                        UINT32 frames;
                        ~Release() {
                            if (frames) p->ReleaseBuffer(frames);
                        }
                    } release{capture.Get(), frames};
                    if (!frames || frames > 4800)
                        throw std::runtime_error("Microphone packet exceeds bounded capture limit");
                    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    if (!silent && !data) throw std::runtime_error("Microphone returned an empty sample buffer");
                    Packet packet;
                    packet.sequence = sequence++;
                    packet.device_position = position;
                    packet.discontinuity =
                        (flags & (AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY | AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) != 0;
                    packet.samples = mono_pcm16(silent ? std::span<const std::uint8_t>{}
                                                       : std::span<const std::uint8_t>(
                                                             data, static_cast<std::size_t>(frames) * mix->nBlockAlign),
                                                frames, format, silent);
                    {
                        std::lock_guard lock(mutex);
                        if (stopping) return;
                        packet.muted = muted;
                        if (muted) std::fill(packet.samples.begin(), packet.samples.end(), std::int16_t{});
                        if (queue.size() >= 8)
                            throw std::runtime_error("Microphone consumer exceeded the bounded capture queue");
                        queue.push_back(std::move(packet));
                        changed.notify_all();
                    }
                    // Release before querying the next packet (not at loop end).
                    const auto released = capture->ReleaseBuffer(frames);
                    release.frames = 0;
                    check(released, "Release microphone packet");
                    check(capture->GetNextPacketSize(&frames), "Read next microphone packet size");
                }
            }
        } catch (const std::exception& failure) {
            std::lock_guard lock(mutex);
            if (!stopping) error = failure.what();
        } catch (...) {
            std::lock_guard lock(mutex);
            if (!stopping) error = "Microphone capture failed";
        }
        {
            std::lock_guard lock(mutex);
            ended = true;
            changed.notify_all();
        }
    }
};
Capture::Capture(std::wstring selected) : state_(std::make_unique<State>(std::move(selected))) {}
Capture::~Capture() {
    stop();
}
void Capture::start() {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    if (s.started || s.stopping) throw std::runtime_error("Microphone requires a new capture generation");
    s.worker = std::thread([&s] { s.run(); });
    s.started = true;
}
std::optional<Packet> Capture::read(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) throw std::invalid_argument("Microphone read needs a positive timeout");
    auto& s = *state_;
    std::unique_lock lock(s.mutex);
    if (!s.started) throw std::runtime_error("Microphone capture is not started");
    s.changed.wait_for(lock, timeout, [&] { return !s.queue.empty() || !s.error.empty() || s.ended || s.stopping; });
    if (!s.error.empty()) throw std::runtime_error(s.error);
    if (s.queue.empty()) return {};
    auto packet = std::move(s.queue.front());
    s.queue.pop_front();
    return packet;
}
void Capture::set_muted(bool value) {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    s.muted = value;
    for (auto& packet : s.queue)
        if (value) {
            std::fill(packet.samples.begin(), packet.samples.end(), std::int16_t{});
            packet.muted = true;
        }
}
void Capture::stop() noexcept {
    auto& s = *state_;
    {
        std::lock_guard lock(s.mutex);
        s.stopping = true;
        s.queue.clear();
        s.changed.notify_all();
    }
    SetEvent(s.stop_event.value);
    if (s.worker.joinable()) s.worker.join();
}
}
