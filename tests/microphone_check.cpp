#include <dm/microphone_pcm.hpp>
#include <dm/microphone_endpoint.hpp>
#include <dm/microphone_fanout.hpp>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <iostream>
using namespace dm::microphone;
template<class F>void rejects(F f){try{f();}catch(const std::invalid_argument&){return;}throw std::runtime_error("Invalid PCM accepted");}
template<class T,std::size_t N>auto bytes(const std::array<T,N>& values){return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(values.data()),sizeof(values));}
void require(bool value){if(!value)throw std::runtime_error("PCM conversion check failed");}
int main(){try{
    require(endpoint_slot_count==4);
    for(unsigned slot=1;slot<=endpoint_slot_count;++slot){
        require(endpoint_arguments(slot)==L" --run --slot "+std::to_wstring(slot));
        require(endpoint_ready(slot,"MICROPHONE_READY slot="+std::to_string(slot)));
        require(!endpoint_ready(slot,"MICROPHONE_READY slot="+std::to_string(slot==4?1:slot+1)));
        require(!endpoint_ready(slot,"MICROPHONE_READY slot="+std::to_string(slot)+" extra"));
        for(unsigned other=1;other<slot;++other)require(endpoint_server_lease(slot)!=endpoint_server_lease(other));
    }
    rejects([]{endpoint_arguments(0);});rejects([]{endpoint_server_lease(5);});rejects([]{endpoint_ready(100,"MICROPHONE_READY slot=1");});
    {
        FanoutQueue fast,slow;FanoutFrame input;input.pcm.fill(9);
        for(std::uint64_t position=0;position<20;++position){input.position=position;fast.push(input);slow.push(input);const auto received=fast.pop(std::chrono::milliseconds(1));require(received&&received->position==position&&!received->discontinuity);}
        auto received=slow.pop(std::chrono::milliseconds(1));require(received&&received->position==14&&received->discontinuity);
        slow.mute(true);slow.push(input);fast.push(input);received=slow.pop(std::chrono::milliseconds(1));require(received&&received->muted&&received->discontinuity);for(auto byte:received->pcm)require(byte==0);
        received=fast.pop(std::chrono::milliseconds(1));require(received&&!received->muted&&received->pcm[0]==9);
        slow.close();slow.push(input);require(!slow.pop(std::chrono::milliseconds(1)));fast.push(input);require(fast.pop(std::chrono::milliseconds(1)).has_value());
    }
    const std::array<std::int16_t,6> stereo{-32768,32767,1000,3000,-2000,-4000};
    auto pcm=mono_pcm16(bytes(stereo),3,{48000,2,16,false});
    require(pcm==std::vector<std::int16_t>({-1,2000,-3000}));
    const std::array<float,7> floats{-2,-1,0,1,2,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};
    pcm=mono_pcm16(bytes(floats),7,{48000,1,32,true});
    require(pcm==std::vector<std::int16_t>({-32768,-32768,0,32767,32767,0,0}));
    pcm=mono_pcm16({},480,{48000,1,16,false},true);require(pcm.size()==480);for(auto value:pcm)require(value==0);
    rejects([]{validate_pcm({44100,1,16,false});});
    rejects([]{validate_pcm({48000,3,16,false});});
    rejects([]{validate_pcm({48000,1,24,false});});
    rejects([]{validate_pcm({48000,1,16,true});});
    rejects([]{mono_pcm16({},4801,{},true);});
    rejects([]{mono_pcm16({},0,{},true);});
    rejects([]{mono_pcm16({},1,{});});
    std::cout<<"Microphone PCM checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
