#include <extrapane/speaker_protocol.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace extrapane;
using namespace extrapane::speaker_wire;
void require(bool v) {
    if (!v) throw std::runtime_error("Speaker protocol check failed");
}
template <class F>
void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid speaker protocol accepted");
}
struct Pipe {
    std::vector<std::uint8_t> bytes;
    std::size_t cursor = 0;
    void read(std::span<std::uint8_t> out) {
        if (out.size() > bytes.size() - cursor) throw std::runtime_error("EOF");
        std::copy_n(bytes.data() + cursor, out.size(), out.data());
        cursor += out.size();
    }
    void write(std::span<const std::uint8_t> in) { bytes.insert(bytes.end(), in.begin(), in.end()); }
    auto channel(media::Binding binding) {
        return Channel(binding, [this](auto b) { read(b); }, [this](auto b) { write(b); });
    }
};
int main() {
    try {
        const media::Binding binding{9, 17};
        auto offer = encode_offer(binding);
        require(decode_offer(offer) == binding);
        for (auto field : {4, 5, 6, 7, 8, 12, 14}) {
            auto bad = offer;
            ++bad[field];
            rejects([&] { decode_offer(bad); });
        }
        rejects([] { encode_offer({0, 1}); });
        rejects([] { decode_offer({}); });
        Frame frame;
        frame.binding = binding;
        frame.pcm.fill(55);
        frame.muted = true;
        auto h = encode_header(frame);
        auto decoded = decode_header(h);
        require(decoded.muted && decoded.binding == binding);
        h[7] = 4;
        rejects([&] { decode_header(h); });
        Pipe pipe;
        auto sender = pipe.channel(binding);
        sender.send(frame);
        require(pipe.bytes.size() == 40 + 1920);
        require(std::all_of(pipe.bytes.begin() + 40, pipe.bytes.end(), [](auto b) { return b == 0; }));
        auto receiver = pipe.channel(binding);
        auto received = receiver.receive();
        for (auto b : received.pcm) require(b == 0);
        frame.sequence = 1;
        frame.position = 480;
        frame.muted = false;
        sender.send(frame);
        require(receiver.receive().pcm == frame.pcm);
        frame.sequence = 2;
        frame.position = 2400;
        frame.discontinuity = true;
        sender.send(frame);
        require(receiver.receive().position == 2400);
        frame.sequence = 3;
        frame.position = 2880;
        frame.discontinuity = false;
        sender.send(frame);
        require(receiver.receive().sequence == 3);
        rejects([&] { sender.send(frame); });
        frame.sequence = 4;
        frame.position = 3360;
        rejects([&] { sender.send(frame); });
        Pipe gap;
        auto gapSender = gap.channel(binding);
        frame.sequence = 0;
        frame.position = 0;
        gapSender.send(frame);
        frame.sequence = 1;
        frame.position = 960;
        rejects([&] { gapSender.send(frame); });
        Pipe wrong;
        auto wrongSender = wrong.channel(binding);
        frame.sequence = 0;
        frame.binding.generation++;
        rejects([&] { wrongSender.send(frame); });
        Pipe shortPipe;
        shortPipe.bytes.assign(39, 0);
        auto shortReceiver = shortPipe.channel(binding);
        rejects([&] { shortReceiver.receive(); });
        Pipe overflow;
        auto overflowSender = overflow.channel(binding);
        frame.binding = binding;
        frame.position = std::numeric_limits<std::uint64_t>::max();
        rejects([&] { overflowSender.send(frame); });
        // Untrusted wire receive rejects foreign generations, replay and partial payloads.
        for (int mode = 0; mode < 3; ++mode) {
            Pipe hostile;
            Frame f;
            f.binding = binding;
            auto header = encode_header(f);
            hostile.write(header);
            hostile.write(f.pcm);
            auto target = hostile.channel(binding);
            if (mode == 0) {
                hostile.bytes[23] ^= 1;
                rejects([&] { target.receive(); });
            }
            if (mode == 1) {
                target.receive();
                hostile.cursor = 0;
                rejects([&] { target.receive(); });
            }
            if (mode == 2) {
                hostile.bytes.pop_back();
                rejects([&] { target.receive(); });
            }
            rejects([&] { target.receive(); });
        }
        std::cout << "Stereo speaker wire checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
