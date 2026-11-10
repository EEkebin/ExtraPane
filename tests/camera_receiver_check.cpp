#include <dm/camera_receiver.hpp>
#include <dm/camera_media_source.hpp>
#include <dm/nv12.hpp>
#include <mfapi.h>
#include <mferror.h>
#include <future>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <source_location>
using namespace dm::camera;
using namespace extrapane::media;
using Microsoft::WRL::ComPtr;
void check(bool ok, const std::source_location location = std::source_location::current()) {
    if (!ok) throw std::runtime_error("Camera receiver check failed at line " + std::to_string(location.line()));
}
template <class F>
void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected);
}
struct Counts {
    int created = 0, decoded = 0, destroyed = 0, prepared = 0, started = 0, stopped = 0;
    std::wstring pipe;
    bool wrong_size = false, throw_decode = false;
    std::int64_t decoded_timestamp = -1;
};
struct Registration final : CameraRegistration {
    Counts& counts;
    explicit Registration(Counts& c) : counts(c) {}
    extrapane::Availability prepare(const std::wstring& pipe, Format, unsigned) override {
        ++counts.prepared;
        counts.pipe = pipe;
        return {true, {}};
    }
    extrapane::Availability start() override {
        ++counts.started;
        return {true, {}};
    }
    void stop() noexcept override { ++counts.stopped; }
};
struct SyntheticDecoder final : Decoder {
    Counts& counts;
    explicit SyntheticDecoder(Counts& c) : counts(c) { ++counts.created; }
    ~SyntheticDecoder() { ++counts.destroyed; }
    void decode(std::span<const std::uint8_t> bytes, std::int64_t timestamp, const Sink& sink) override {
        ++counts.decoded;
        counts.decoded_timestamp = timestamp;
        if (counts.throw_decode) throw std::runtime_error("Simulated malformed H264");
        check(bytes.size() == 1 && bytes[0] == 42);
        sink(std::vector<std::uint8_t>(counts.wrong_size ? 23 : 24, 91), timestamp);
    }
};
std::unique_ptr<Receiver> make_receiver(Counts& c) {
    return std::make_unique<Receiver>(Format{4, 4, 30}, 1, Binding{7, 9}, std::make_unique<Registration>(c),
                                      [&c](Format) { return std::make_unique<SyntheticDecoder>(c); });
}
ComPtr<IMFMediaEvent> next(IMFMediaEventGenerator* source, MediaEventType expected) {
    ComPtr<IMFMediaEvent> event;
    check(SUCCEEDED(source->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event)));
    MediaEventType type{};
    check(SUCCEEDED(event->GetType(&type)) && type == expected);
    return event;
}
int main() {
    try {
        // Pitch padding and padded coded heights must never appear in the camera image.
        std::vector<std::uint8_t> y(8 * 8, 255), uv(8 * 4, 255);
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned col = 0; col < 4; ++col) y[row * 8 + col] = 16;
        for (unsigned row = 0; row < 2; ++row)
            for (unsigned col = 0; col < 4; ++col) uv[row * 8 + col] = 128;
        auto packed = dm::video::pack_nv12(4, 4, 8, y, uv);
        check(packed.size() == 24);
        for (unsigned i = 0; i < 24; ++i) check(packed[i] == (i < 16 ? 16 : 128));
        rejects([&] { dm::video::pack_nv12(4, 4, 3, y, uv); });
        rejects([&] { dm::video::pack_nv12(4, 4, 8, std::span(y).first(27), uv); });
        check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
        struct Com {
            ~Com() { CoUninitialize(); }
        } com;
        check(SUCCEEDED(MFStartup(MF_VERSION)));
        struct Mf {
            ~Mf() { MFShutdown(); }
        } mf;
        // A failed resource is destroyed during negotiation. Its concrete local
        // diagnostic must survive and must not prevent cleanup if reporting throws.
        std::string diagnostic;
        Counts broken;
        {
            Receiver r(
                {4, 4, 30}, 1, {7, 9}, std::make_unique<Registration>(broken),
                [](Format) -> std::unique_ptr<Decoder> { throw std::runtime_error("Decoder HRESULT 0x80004005"); },
                [&](const std::string& message) {
                    diagnostic = message;
                    throw std::runtime_error("report failed");
                });
            auto result = r.prepare();
            check(!result.available && r.failed());
            check(result.reason.find("0x80004005") != std::string::npos);
        }
        check(diagnostic.find("0x80004005") != std::string::npos && broken.stopped == 1);
        Counts counts;
        auto receiver = make_receiver(counts);
        check(counts.created == 0 && counts.prepared == 0);
        check(receiver->prepare().available && receiver->start().available);
        MediaSource feed({4, 4, 30}, counts.pipe);
        auto source = feed.source();
        ComPtr<IMFPresentationDescriptor> pd;
        check(SUCCEEDED(source->CreatePresentationDescriptor(&pd)));
        PROPVARIANT start{};
        check(SUCCEEDED(source->Start(pd.Get(), nullptr, &start)));
        auto announced = next(source.Get(), MENewStream);
        PROPVARIANT value{};
        check(SUCCEEDED(announced->GetValue(&value)));
        ComPtr<IMFMediaStream> stream;
        check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&stream))));
        PropVariantClear(&value);
        next(source.Get(), MESourceStarted);
        next(stream.Get(), MEStreamStarted);
        check(SUCCEEDED(stream->RequestSample(nullptr)));
        auto delivery = std::async(std::launch::async, [stream] {
            ComPtr<IMFMediaEvent> e;
            auto hr = stream->GetEvent(0, &e);
            return std::pair{hr, e};
        });
        extrapane::wire::Bytes wire;
        std::size_t cursor = 0;
        auto read = [&](std::span<std::uint8_t> out) {
            if (out.size() > wire.size() - cursor) throw std::runtime_error("Disconnected");
            std::copy_n(wire.begin() + cursor, out.size(), out.begin());
            cursor += out.size();
        };
        auto write = [&](std::span<const std::uint8_t> bytes) { wire.insert(wire.end(), bytes.begin(), bytes.end()); };
        const auto beforeDelivery = MFGetSystemTime();
        Channel channel({7, 9}, read, write);
        channel.send({{7, 9}, 0, 9000000, {42}});
        receiver->receive_one(channel);
        check(counts.decoded_timestamp == 0); // Network epoch is rebased before decoding.
        if (delivery.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            source->Shutdown();
            throw std::runtime_error("Receiver frame delivery timed out");
        }
        auto [hr, event] = delivery.get();
        check(SUCCEEDED(hr));
        MediaEventType type{};
        check(SUCCEEDED(event->GetType(&type)) && type == MEMediaSample);
        check(SUCCEEDED(event->GetValue(&value)));
        ComPtr<IMFSample> sample;
        check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&sample))));
        PropVariantClear(&value);
        // The virtual camera must expose local QPC-correlated time to Frame Server,
        // rather than the rebased network timestamp accepted by the decoder.
        const auto afterDelivery = MFGetSystemTime();
        LONGLONG timestamp = -1;
        check(SUCCEEDED(sample->GetSampleTime(&timestamp)) && timestamp >= beforeDelivery &&
              timestamp <= afterDelivery);
        ComPtr<IMFMediaBuffer> buffer;
        check(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)));
        BYTE* pixels = nullptr;
        DWORD size = 0;
        check(SUCCEEDED(buffer->Lock(&pixels, nullptr, &size)));
        bool correct = size == 24 && pixels[0] == 91 && pixels[23] == 91;
        buffer->Unlock();
        check(correct);
        rejects([&] { receiver->receive_one(channel); });
        check(receiver->failed() && counts.stopped == 1 && counts.destroyed == 1);
        feed.stop();
        for (unsigned failure = 0; failure < 6; ++failure) {
            Counts c;
            auto r = make_receiver(c);
            check(r->prepare().available && r->start().available);
            Frame frame{{7, 9}, 0, 0, {42}};
            if (failure == 0) frame.binding.generation = 10;
            if (failure == 1) frame.sequence = 1;
            if (failure == 2) c.wrong_size = true;
            if (failure == 3) c.throw_decode = true;
            if (failure == 4) {
                r->accept(frame);
                frame.sequence = 1;
                frame.timestamp = -1;
            }
            if (failure == 5) {
                frame.timestamp = std::numeric_limits<std::int64_t>::min();
                r->accept(frame);
                frame.sequence = 1;
                frame.timestamp = std::numeric_limits<std::int64_t>::max();
            }
            rejects([&] { r->accept(frame); });
            check(r->failed() && c.stopped == 1 && c.destroyed == 1);
            r->stop();
            check(c.stopped == 1);
        }
        std::cout << "Camera receiver routing, native sample delivery, stride packing and failure cleanup passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
