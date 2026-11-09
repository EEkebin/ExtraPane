#include "../platform/macos/resource_microphone.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

int main(){
    dm::mac::MicrophoneMuteGate gate;
    gate.set(true,100);gate.set(false,200);
    if(!gate.sampleMuted(150)||gate.sampleMuted(201))throw std::runtime_error("Delayed capture crossed mute boundary");
    gate.set(true,300);gate.set(false,400);
    if(!gate.sampleMuted(350)||!gate.sampleMuted(150)||gate.sampleMuted(401))throw std::runtime_error("Mute history lost");
    bool muted=false;
    std::vector<extrapane::microphone_wire::Frame> sent;
    dm::mac::MicrophoneDelivery delivery({11,22},[&]{return muted;},[&](const auto& frame){
        sent.push_back(frame);
        // Simulate the UI muting while the first synchronous send is blocked.
        if(sent.size()==1)muted=true;
    });
    const std::vector<std::uint8_t> chunk(960*3,0x31);
    delivery.push(chunk,false,false);
    auto require=[](bool value){if(!value)throw std::runtime_error("Microphone delivery mute regression");};
    auto silent=[](const auto& frame){return frame.muted&&std::all_of(frame.pcm.begin(),frame.pcm.end(),[](auto b){return b==0;});};
    require(sent.size()==3&&!sent[0].muted&&sent[0].pcm[0]==0x31);
    require(silent(sent[1])&&silent(sent[2]));
    require(sent[2].sequence==2&&sent[2].position==960);
    // A capture-time muted buffer must stay silent after the UI unmutes.
    muted=false;delivery.push(chunk,true,false);
    require(sent.size()==6&&silent(sent[3])&&silent(sent[4])&&silent(sent[5]));
    delivery.push(std::span(chunk).first(960),false,false);
    require(!sent.back().muted&&sent.back().pcm[0]==0x31);
    std::cout<<"Mac microphone mute transitions during buffered delivery passed\n";
}
