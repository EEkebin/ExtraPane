#include <windows.h>
#include <dm/display.hpp>
#include "../drivers/idd/ModeRequest.h"
#include <array>
#include <stdexcept>
#include <iostream>
using namespace ExtraPane::DisplayRequest;
void require(bool value){if(!value)throw std::runtime_error("Virtual display mode request check failed");}
int main(){try{
    // Same producer/validator path used before server display allocation. This
    // updates thread-local naming only: no registry, driver or screen changes.
    for(unsigned slot=1;slot<=4;++slot)dm::display::select_client_layout_profile(L"10.0.20.130",slot);
    bool rejected=false;try{dm::display::select_layout_profile(L"10.0.20.130-slot-1");}catch(const std::invalid_argument&){rejected=true;}require(rejected);
    rejected=false;try{dm::display::select_client_layout_profile(L"10.0.20.130",0);}catch(const std::invalid_argument&){rejected=true;}require(rejected);
    dm::display::select_layout_profile(L"");
    require(Default({1,0,0,0}));require(!Valid({1,0,0,0}));require(!Default({0,0,0,0}));
    for(const Request mode: {Request{1,2,2,1},Request{1,1366,768,59},Request{1,4096,2160,60},Request{1,2160,4096,24},Request{1,3000,2000,60}}){
        require(Valid(mode));std::array<unsigned char,128> first{},second{};first[8]=17;second[8]=17;
        EncodeDescriptor(first.data(),mode);Request decoded{};require(DecodeDescriptor(first.data(),decoded));
        require(decoded.Version==mode.Version&&decoded.Width==mode.Width&&decoded.Height==mode.Height&&decoded.Hz==mode.Hz&&first[8]==17);
        EncodeDescriptor(second.data(),{1,1280,720,30});require(first!=second); // Independent device descriptions.
        first[120]^=1;require(!DecodeDescriptor(first.data(),decoded)); // Corrupt checksum fails closed.
    }
    for(const Request mode: {Request{2,1920,1080,60},Request{1,0,1080,60},Request{1,1921,1080,60},Request{1,1920,1079,60},Request{1,4096,4096,60},Request{1,4098,1080,60},Request{1,1920,1080,61},Request{1,1920,1080,0}})require(!Valid(mode));
    std::array<unsigned char,128> bad{};EncodeDescriptor(bad.data(),{1,1921,1080,60});Request decoded{};require(!DecodeDescriptor(bad.data(),decoded));
    std::cout<<"Virtual display property bounds and per-instance descriptor checks passed (no device creation)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
