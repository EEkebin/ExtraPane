#include <extrapane/camera_protocol.hpp>
#include <iostream>
#include <stdexcept>
using namespace extrapane::camera_wire;
void check(bool good){if(!good)throw std::runtime_error("Camera offer check failed");}
template<class F>void rejects(F action){bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught);}
int main(){try{
    Offer offer{1280,720,30,{0x102030405060708ULL,0x1112131415161718ULL}};auto bytes=encode(offer);auto result=decode(bytes);
    check(result.width==1280&&result.height==720&&result.fps==30&&result.binding==offer.binding);
    check(bytes[8]==5&&bytes[9]==0&&bytes[10]==2&&bytes[11]==208&&bytes[16]==1&&bytes[23]==8);
    for(std::size_t size=0;size<bytes.size();++size)rejects([&]{decode(std::span(bytes).first(size));});
    for(auto index:{0,4,5,6,7,14,15}){auto changed=bytes;changed[index]^=128;rejects([&]{decode(changed);});}
    auto changed=bytes;changed[8]=255;rejects([&]{decode(changed);});changed=bytes;changed[13]=61;rejects([&]{decode(changed);});
    offer.binding.generation=0;rejects([&]{encode(offer);});
    std::cout<<"Camera offer bounds and protocol rejection passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
