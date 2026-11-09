#include <extrapane/camera_protocol.hpp>
#include <stdexcept>
namespace extrapane::camera_wire {
namespace {
void validate(const Offer& offer){
    if(!offer.width||!offer.height||offer.width>3840||offer.height>2160||offer.width%2||offer.height%2||
       !offer.fps||offer.fps>60||!offer.binding.resource||!offer.binding.generation)
        throw std::invalid_argument("Invalid negotiated camera offer");
}
void put(OfferBytes& bytes,unsigned offset,std::uint64_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes[offset+size-1-i]=static_cast<std::uint8_t>(value>>(8*i));}
std::uint64_t get(std::span<const std::uint8_t> bytes,unsigned offset,unsigned size){std::uint64_t value=0;for(unsigned i=0;i<size;++i)value=(value<<8)|bytes[offset+i];return value;}
}
OfferBytes encode(const Offer& offer){
    validate(offer);OfferBytes bytes{};bytes[0]='E';bytes[1]='P';bytes[2]='C';bytes[3]='M';bytes[5]=1;bytes[6]=1;
    put(bytes,8,offer.width,2);put(bytes,10,offer.height,2);put(bytes,12,offer.fps,2);
    put(bytes,16,offer.binding.resource,8);put(bytes,24,offer.binding.generation,8);return bytes;
}
Offer decode(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=32||bytes[0]!='E'||bytes[1]!='P'||bytes[2]!='C'||bytes[3]!='M'||bytes[4]!=0||bytes[5]!=1||bytes[6]!=1||bytes[7]!=0||bytes[14]!=0||bytes[15]!=0)
        throw std::invalid_argument("Unsupported camera session protocol");
    Offer offer{static_cast<unsigned>(get(bytes,8,2)),static_cast<unsigned>(get(bytes,10,2)),static_cast<unsigned>(get(bytes,12,2)),{get(bytes,16,8),get(bytes,24,8)}};
    validate(offer);return offer;
}
}
