#pragma once
#import <AVFoundation/AVFoundation.h>
#include <span>
#include <vector>
namespace dm::mac {
std::vector<std::vector<uint8_t>> annexb(std::span<const uint8_t> bytes);
class Media {
    CMVideoFormatDescriptionRef format_=nullptr;
    std::vector<uint8_t> sps_,pps_;
public:
    ~Media(){reset();}
    void reset();
    CMSampleBufferRef sample(std::span<const uint8_t> packet,uint64_t micros);
};
}
