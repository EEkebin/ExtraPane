#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
namespace dm::video {
enum class Quality : unsigned { automatic, sharp, smooth };
inline const char* quality_name(Quality mode){return mode==Quality::sharp?"Sharp":mode==Quality::smooth?"Smooth":"Auto";}
// One observation per second. Hysteresis prevents a slow frame or short burst
// from repeatedly rebuilding the codec. Display geometry is never changed.
class QualityController {
    Quality mode_;unsigned level_=0,bad_=0,good_=0;
public:
    explicit QualityController(Quality mode):mode_(mode),level_(mode==Quality::smooth?1u:0u){}
    unsigned scale()const{return mode_==Quality::sharp?100u:level_==0?100u:level_==1?75u:50u;}
    unsigned bitrate(unsigned width,unsigned height,unsigned fps)const{
        auto base=std::clamp<std::uint64_t>(static_cast<std::uint64_t>(width)*height*fps/8,4000000,60000000);
        if(mode_==Quality::sharp)base=base*(100u-level_*25u)/100u;
        return static_cast<unsigned>(std::max<std::uint64_t>(2000000,base));
    }
    bool mode(Quality value){if(value==mode_)return false;mode_=value;level_=value==Quality::smooth?1u:0u;bad_=good_=0;return true;}
    bool observe(double work_ms,double decode_ms,double render_ms,unsigned drops,unsigned fps){
        const double budget=1000.0/std::max(1u,fps);
        const bool overloaded=work_ms>budget*1.15 || decode_ms>budget || render_ms>budget*1.3 || drops>std::max(2u,fps/10);
        if(overloaded){++bad_;good_=0;}else{bad_=0;if(work_ms<budget*.8 && decode_ms<budget*.7 && render_ms<budget*1.15 && !drops)++good_;else good_=0;}
        if(bad_>=2&&level_<2){++level_;bad_=good_=0;return true;}
        const unsigned floor=mode_==Quality::smooth?1u:0u;
        if(good_>=8&&level_>floor){--level_;bad_=good_=0;return true;}
        return false;
    }
};
inline std::pair<unsigned,unsigned> stream_size(unsigned width,unsigned height,unsigned scale){
    return {std::max(2u,(width*scale/100)&~1u),std::max(2u,(height*scale/100)&~1u)};
}
}
