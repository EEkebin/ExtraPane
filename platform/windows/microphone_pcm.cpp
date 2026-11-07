#include <dm/microphone_pcm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dm::microphone {
void validate_pcm(PcmFormat f){
    if(f.rate!=48000||(f.channels!=1&&f.channels!=2)||
       (f.floating?f.bits!=32:f.bits!=16))
        throw std::invalid_argument("Microphone requires 48 kHz mono/stereo PCM16 or float32; resampling is not implemented");
}
std::vector<std::int16_t> mono_pcm16(std::span<const std::uint8_t> bytes,
    std::size_t frames,PcmFormat f,bool silence){
    validate_pcm(f);
    if(!frames||frames>4800)throw std::invalid_argument("Microphone packet must contain 1 to 4800 frames");
    const auto needed=frames*f.channels*(f.bits/8);
    if(!silence&&bytes.size()!=needed)throw std::invalid_argument("Microphone PCM packet length mismatch");
    std::vector<std::int16_t> result(frames,0);
    if(silence)return result;
    for(std::size_t frame=0;frame<frames;++frame){
        double sum=0;
        for(unsigned channel=0;channel<f.channels;++channel){
            const auto offset=(frame*f.channels+channel)*(f.bits/8);
            if(f.floating){
                float value;std::memcpy(&value,bytes.data()+offset,sizeof(value));
                if(std::isfinite(value))sum+=std::clamp(static_cast<double>(value),-1.0,1.0)*32768.0;
            }else{
                std::int16_t value;std::memcpy(&value,bytes.data()+offset,sizeof(value));sum+=value;
            }
        }
        result[frame]=static_cast<std::int16_t>(std::clamp(std::lround(sum/f.channels),-32768L,32767L));
    }
    return result;
}
}
