#include <extrapane/session_route.hpp>
#include <extrapane/camera_protocol.hpp>
#include <extrapane/microphone_protocol.hpp>
#include <dm/video_protocol.hpp>
#include <dm/prefix_buffer.hpp>
#include <iostream>
#include <vector>
using extrapane::ResourceKind;
void require(bool value){if(!value)throw std::runtime_error("Session routing check failed");}
template<class F> void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected);}
struct Source{
    std::vector<std::uint8_t> data;std::size_t offset=0,calls=0;
    void read(std::span<std::uint8_t> out){++calls;if(out.size()>data.size()-offset)throw std::runtime_error("truncated");std::copy_n(data.begin()+offset,out.size(),out.begin());offset+=out.size();}
};
void route(std::span<const std::uint8_t> message,ResourceKind expected){
    Source source{{message.begin(),message.end()}};dm::protocol::PrefixBuffer input;
    auto read=[&](auto bytes){source.read(bytes);};
    std::array<std::uint8_t,8> first{};input.peek(first,read);
    require(extrapane::classify_session_prefix(first)==expected&&source.offset==8);
    const auto calls=source.calls;input.peek(first,read);require(source.calls==calls);
    // Adapter sees its original complete offer even though the router looked
    // ahead. Split reads model decoders that consume header fields separately.
    std::vector<std::uint8_t> received(message.size());
    input.read(std::span(received).first(3),read);
    std::array<std::uint8_t,8> after_three{};
    if(message.size()>=11){input.peek(after_three,read);require(std::equal(after_three.begin(),after_three.end(),message.begin()+3));}
    input.read(std::span(received).subspan(3),read);
    require(std::equal(received.begin(),received.end(),message.begin())&&source.offset==message.size());
}
int main(){try{
    auto display=dm::video::wire::request;route(display,ResourceKind::display);
    display[7]=1;route(display,ResourceKind::display);
    const auto camera=extrapane::camera_wire::encode({1280,720,30,{1,17}});route(camera,ResourceKind::camera);
    const auto microphone=extrapane::microphone_wire::encode_offer({2,91});route(microphone,ResourceKind::microphone);
    const std::array<std::uint8_t,8> usb{'E','P','U','S',3,0,0,0};route(usb,ResourceKind::usb);
    {auto legacy=usb;legacy[4]=1;rejects([&]{extrapane::classify_session_prefix(legacy);});}
    for(const auto* original:std::array<const std::uint8_t*,4>{display.data(),camera.data(),microphone.data(),usb.data()}){
        std::array<std::uint8_t,8> bytes{};std::copy_n(original,8,bytes.begin());++bytes[4];rejects([&]{extrapane::classify_session_prefix(bytes);});
        rejects([&]{extrapane::classify_session_prefix(std::span(bytes).first(7));});
    }
    std::array<std::uint8_t,8> unknown{};rejects([&]{extrapane::classify_session_prefix(unknown);});
    display[7]=2;rejects([&]{extrapane::classify_session_prefix(std::span(display).first(8));});
    dm::protocol::PrefixBuffer bounded;unsigned reads=0;std::array<std::uint8_t,9> oversized{};
    rejects([&]{bounded.peek(oversized,[&](auto){++reads;});});require(reads==0);
    // Partial source failure must poison the lookahead object; reusing a
    // partially consumed encrypted stream could route bytes to the wrong parser.
    dm::protocol::PrefixBuffer broken;
    rejects([&]{broken.peek(unknown,[&](auto bytes){++reads;bytes[0]='E';throw std::runtime_error("interrupted");});});
    const auto failures=reads;
    rejects([&]{broken.read(unknown,[&](auto){++reads;});});
    rejects([&]{broken.peek(unknown,[&](auto){++reads;});});require(reads==failures);
    std::cout<<"Session routing and admitted-prefix checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
