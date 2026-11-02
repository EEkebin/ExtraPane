#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
namespace dm::video {
enum class Quality : unsigned { native, reduced, low_bandwidth };
inline const char* quality_name(Quality mode){return mode==Quality::reduced?"75% stream":mode==Quality::low_bandwidth?"50% stream":"Native stream";}
// Fixed for the connection: never rebuild codecs or reset input in response
// to timing noise. The native default matches the original streaming path.
class QualityController {
    Quality mode_;
public:
    explicit QualityController(Quality mode):mode_(mode){}
    unsigned scale()const{return mode_==Quality::native?100u:mode_==Quality::reduced?75u:50u;}
    unsigned bitrate(unsigned width,unsigned height,unsigned fps)const{
        return static_cast<unsigned>(std::clamp<std::uint64_t>(static_cast<std::uint64_t>(width)*height*fps/8,4000000,60000000));
    }
};
inline std::pair<unsigned,unsigned> stream_size(unsigned width,unsigned height,unsigned scale){
    return {std::max(2u,(width*scale/100)&~1u),std::max(2u,(height*scale/100)&~1u)};
}
}
