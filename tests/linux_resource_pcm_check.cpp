#include "../platform/linux/resource_pcm.hpp"
#include <cassert>
#include <iostream>
#include <vector>
int main(){
    using namespace dm::linux_client;
    std::vector<extrapane::microphone_wire::Frame> frames;
    PcmPacketizer packetizer({11,22},[&](auto frame){frames.push_back(frame);});
    const std::vector<std::uint8_t> first(400,0x31),second(560,0x32),whole(1920,0x33);
    packetizer.push(first,false,false);assert(frames.empty());packetizer.push(second,false,false);
    assert(frames.size()==1&&frames[0].binding.resource==11&&frames[0].sequence==0&&frames[0].position==0);
    assert(frames[0].pcm[399]==0x31&&frames[0].pcm[400]==0x32);
    packetizer.push(whole,true,false);assert(frames.size()==3&&frames[2].sequence==2&&frames[2].position==960);
    assert(frames[1].muted&&std::all_of(frames[1].pcm.begin(),frames[1].pcm.end(),[](auto x){return x==0;}));
    packetizer.push(first,false,false);packetizer.push(whole,false,true);
    assert(frames.size()==5&&frames[3].discontinuity&&!frames[4].discontinuity&&frames[3].position==1640);
    bool rejected=false;try{packetizer.push(std::span(first).first(3),false,false);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    std::cout<<"Linux PCM packet boundaries, mute and discontinuity checks passed\n";
}
