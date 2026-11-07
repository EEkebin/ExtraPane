#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <stdexcept>
#include <cstring>
namespace dm::video {
// Pack negotiated pixels from a padded NV12 surface without color conversion.
inline std::vector<std::uint8_t> pack_nv12(unsigned width,unsigned height,std::size_t pitch,
    std::span<const std::uint8_t> y,std::span<const std::uint8_t> uv){
    if(!width||!height||width%2||height%2||width>4096||height>4096||pitch<width||pitch>65536)
        throw std::invalid_argument("Invalid NV12 geometry");
    if(y.size()<pitch*(height-1)+width||uv.size()<pitch*(height/2-1)+width)
        throw std::invalid_argument("Truncated NV12 planes");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width)*height*3/2);
    for(unsigned row=0;row<height;++row)std::memcpy(result.data()+std::size_t(row)*width,y.data()+row*pitch,width);
    for(unsigned row=0;row<height/2;++row)std::memcpy(result.data()+std::size_t(width)*height+std::size_t(row)*width,uv.data()+row*pitch,width);
    return result;
}
}
