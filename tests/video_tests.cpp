#include <dm/video.hpp>
#include <dm/video_layout.hpp>
#include <dm/video_protocol.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid video data accepted");
}
}
int main() {
    try {
        using namespace dm::video;
        QualityController controller(Quality::automatic);
        require(!controller.observe(40,2,5,0,60),"Single slow sample must not change quality");
        require(controller.observe(40,2,5,0,60)&&controller.scale()==75,"Sustained overload must downscale");
        for(int i=0;i<7;++i)require(!controller.observe(4,2,5,0,60),"Recovery must wait for stability");
        require(controller.observe(4,2,5,0,60)&&controller.scale()==100,"Stable recovery must restore resolution");
        controller.mode(Quality::sharp);controller.observe(40,2,5,0,60);controller.observe(40,2,5,0,60);
        require(controller.scale()==100&&controller.bitrate(3000,2000,60)<45000000,"Sharp must preserve dimensions while reducing bitrate");
        const auto fb=wire::decode_feedback(wire::encode_feedback({17,Quality::smooth,1200,4000,3}));
        require(fb.sequence==17&&fb.quality==Quality::smooth&&fb.dropped==3,"Feedback roundtrip failed");
        rejects([]{wire::encode_feedback({0,static_cast<Quality>(3)});});
        rejects([]{wire::encode_feedback({0,Quality::automatic,5000001});});
        require(stream_size(3000,2000,75)==std::pair<unsigned,unsigned>{2250,1500},"Stream scaling dimensions wrong");
        const auto portrait=content_rect(1080,1920,3000,2000);
        require(portrait.width==1125 && portrait.height==2000 && portrait.x==937 && portrait.y==0,"Portrait viewport must preserve aspect and center input area");
        const auto landscape=content_rect(3000,2000,1000,1000);
        require(landscape.width==1000 && landscape.height==666 && landscape.y==167,"Landscape viewport must letterbox");
        auto request=wire::request;
        wire::set_client_display(request,{3000,2000,60});
        auto preference=wire::take_client_display(request);
        require(preference.width==3000 && preference.height==2000 && preference.hz==60 && request==wire::request,"Client display roundtrip failed");
        rejects([&]{wire::set_client_display(request,{0,2000,60});});
        rejects([&]{wire::set_client_display(request,{16385,2000,60});});
        rejects([&]{wire::set_client_display(request,{3000,2000,1001});});
        auto offer = wire::encode_offer({1280,720,10,30});
        require(wire::decode_offer(offer).width == 1280, "Offer width lost");
        rejects([&] { (void)wire::decode_offer(std::span(offer).first(12)); });
        auto unknown_grant = offer; unknown_grant[5] = 255;
        rejects([&] { (void)wire::decode_offer(unknown_grant); });
        rejects([] { (void)wire::encode_offer({100000,720,10,30}); });
        rejects([] { (void)wire::encode_offer({1280,720,0,30}); });
        const auto h264_offer=wire::encode_offer({1280,720,30,180,wire::Codec::h264});
        require(wire::decode_offer(h264_offer).codec==wire::Codec::h264,"Codec selection lost");
        rejects([] { (void)wire::encode_offer({1279,720,30,180,wire::Codec::h264}); });
        require(wire::decode_offer(wire::encode_offer({3000,2000,60,3600,wire::Codec::h264})).height==2000,"Native mode lost");
        rejects([] { (void)wire::encode_offer({4096,4096,60,180,wire::Codec::h264}); });
        rejects([] { (void)wire::encode_offer({3000,2000,61,180,wire::Codec::h264}); });
        require(wire::decode_offer(wire::encode_offer({3000,2000,60,0,wire::Codec::h264})).frames==0,"Continuous session lost");
        auto unknown_codec=offer; unknown_codec[6]=255;
        rejects([&] { (void)wire::decode_offer(unknown_codec); });
        auto old_version=offer; old_version[4]=2;
        rejects([&] { (void)wire::decode_offer(old_version); });
        rejects([] { (void)wire::encode_header({0,wire::max_jpeg+1,0}); });
        require(wire::decode_header(wire::encode_header({42,0,123})).bytes==0,"Reconfiguration marker lost");
        auto header = wire::encode_header({123,4000,0x123456789abcdef0ull});
        const auto decoded = wire::decode_header(header);
        require(decoded.sequence == 123 && decoded.bytes == 4000 && decoded.micros == 0x123456789abcdef0ull, "Header lost endian-sensitive fields");
        header[8] = 255;
        rejects([&] { (void)wire::decode_header(header); });
        Frame source{64,32,{}};
        source.bgra.resize(64 * 32 * 4);
        for (std::size_t i=0;i<source.bgra.size();i+=4) {
            source.bgra[i]=30; source.bgra[i+1]=100; source.bgra[i+2]=200; source.bgra[i+3]=255;
        }
        const auto jpeg = encode_jpeg(source);
        const auto result = decode_jpeg(jpeg,64,32);
        require(result.bgra.size() == source.bgra.size(), "Decoded pixel size differs");
        require(result.bgra[0] >= 25 && result.bgra[0] <= 35 && result.bgra[2] >= 195 && result.bgra[2] <= 205, "JPEG colors incorrect");
        rejects([&] { (void)decode_jpeg(jpeg,128,32); });
        rejects([] { (void)decode_jpeg(std::vector<std::uint8_t>{1,2,3},64,32); });
        rejects([] { (void)decode_jpeg({},1280,720); });
        rejects([] { (void)encode_jpeg(Frame{64,32,{}}); });
        std::cout << "Video bounds, protocol and native JPEG checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
