#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dm::mac::resource {
enum class Kind {Camera,Microphone};
struct Choice {
    Kind kind=Kind::Camera;
    std::string id,label,reason;
    unsigned width=0,height=0,fps=0;
    bool available=false;
};
struct Packet {
    std::vector<std::uint8_t> bytes;
    std::int64_t timestamp100ns=0;
    bool keyframe=false,discontinuity=false,muted=false;
};
std::vector<Choice> enumerate(Kind);
// Callback may run off the main thread. Capture never prompts implicitly.
void requestPermission(Kind,std::function<void(bool,std::string)>);
class Capture {
    struct State;std::unique_ptr<State> state_;
public:
    // Mute predicate receives the sample start on the host clock, in 100ns units.
    explicit Capture(Choice,std::function<bool(std::int64_t)> muted={}); // Call off the main thread.
    ~Capture();
    Capture(const Capture&)=delete;Capture& operator=(const Capture&)=delete;
    const Choice& choice()const;
    // Camera: complete AnnexB access unit. Mic: aligned S16LE 48kHz mono
    // capture chunks; caller packetizes into wire-format ten-millisecond frames.
    Packet nextPacket(const std::function<bool()>& cancelled);
    void setMirrored(bool); // Throws if this native connection cannot mirror.
};
}
