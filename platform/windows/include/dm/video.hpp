#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <functional>
#include <optional>
#include <dm/network.hpp>
#include <dm/video_protocol.hpp>
#include <dm/input_protocol.hpp>

namespace dm::video {
struct GpuFrame;
struct Frame {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
    std::shared_ptr<GpuFrame> gpu=nullptr;
    std::uint64_t capture_generation=0;
};
class DesktopCapture {
public:
    explicit DesktopCapture(const std::wstring& source=L"", unsigned max_width=1280,unsigned max_height=720);
    ~DesktopCapture();
    Frame next();
    Frame next_gpu();
private:
    struct State;
    std::unique_ptr<State> state_;
};
std::vector<std::uint8_t> encode_jpeg(const Frame& frame);
Frame decode_jpeg(const std::vector<std::uint8_t>& jpeg, std::uint32_t width, std::uint32_t height);
void hardware_info();
void host_video(const net::Options& options, const std::string& pair, unsigned frames, unsigned fps, wire::Codec codec=wire::Codec::h264, const std::wstring& source=L"", bool extend=false,bool allow_input=false,unsigned width=1280,unsigned height=720,bool keep_layout=false,bool client_mode=false,const std::function<std::wstring(bool&)>& acquire_monitor={},bool* listener_ready=nullptr);
void serve_video(const net::Options& options,const std::string& pair,bool allow_input,unsigned frames=0);
void receive_video(const net::Options& options, const std::string& pair, const std::function<void(Frame)>& sink, std::optional<unsigned> decoder_adapter=std::nullopt, const std::function<std::vector<input::Event>()>& input_source={},const std::function<void(unsigned)>& input_grant={},wire::ClientDisplay client_display={});
void preview_video(const net::Options& options, const std::string& pair, std::optional<unsigned> decoder_adapter=std::nullopt,bool control=false,bool fullscreen=false);
}
