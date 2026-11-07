#include <dm/microphone_pcm.hpp>
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
