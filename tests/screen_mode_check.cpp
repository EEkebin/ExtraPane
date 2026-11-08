#include <dm/video_protocol.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace dm::video::wire;
void need(bool ok){if(!ok)throw std::runtime_error("Screen request check failed");}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid screen request accepted");}
int main(){try{
    auto legacy=request;const auto default_mode=take_screen_request(legacy);need(default_mode.mode==DisplayMode::extend_desktop&&default_mode.screen_id==0&&legacy==request);
    auto bytes=request;set_screen_request(bytes,{DisplayMode::existing_screen,0x12345678});need(bytes[11]==1&&bytes[12]==0x12&&bytes[15]==0x78&&bytes!=request);
    set_client_display(bytes,{1920,1080,60,false});const auto mode=take_screen_request(bytes);need(mode.mode==DisplayMode::existing_screen&&mode.screen_id==0x12345678);const auto preference=take_client_display(bytes);need(preference.width==1920&&bytes==request);
    rejects([&]{set_screen_request(bytes,{DisplayMode::existing_screen,0});});rejects([&]{set_screen_request(bytes,{DisplayMode::extend_desktop,1});});rejects([&]{set_screen_request(bytes,{static_cast<DisplayMode>(2),1});});
    bytes=request;bytes[11]=2;rejects([&]{take_screen_request(bytes);});bytes=request;bytes[15]=1;rejects([&]{take_screen_request(bytes);});
    std::array<std::uint8_t,31> short_request{};rejects([&]{take_screen_request(short_request);});
    std::cout<<"Existing-screen request selection, legacy preservation and malformed mode rejection passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
