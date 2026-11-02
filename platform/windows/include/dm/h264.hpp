#pragma once
#include <dm/video.hpp>
#include <span>
struct ID3D11Device;

namespace dm::video {
class H264Encoder {
public:
    H264Encoder(unsigned width, unsigned height, unsigned fps,ID3D11Device* gpu_device=nullptr,unsigned bitrate=0);
    ~H264Encoder();
    std::vector<std::uint8_t> encode(const Frame& frame, std::uint64_t sequence);
    std::string description() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
class H264Decoder {
public:
    H264Decoder(unsigned width, unsigned height, unsigned fps, std::optional<unsigned> adapter=std::nullopt,bool gpu_output=false);
    ~H264Decoder();
    std::vector<Frame> decode(std::span<const std::uint8_t> access_unit, std::uint64_t sequence);
    std::vector<Frame> finish();
    std::uint64_t gpu_frames() const;
    std::string description() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
void h264_roundtrip_check();
void gpu_h264_roundtrip_check();
}
