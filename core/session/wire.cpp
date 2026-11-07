#include <extrapane/wire.hpp>
#include <algorithm>
#include <stdexcept>

namespace extrapane::wire {
namespace {
void validate(const Header& h){
    switch(h.operation){
    case Operation::capabilities:case Operation::request:case Operation::grant:
    case Operation::reject:case Operation::stop:case Operation::status:break;
    default:throw std::invalid_argument("Unknown resource operation");
    }
    switch(h.kind){
    case ResourceKind::display:case ResourceKind::camera:case ResourceKind::microphone:case ResourceKind::usb:break;
    default:throw std::invalid_argument("Unknown resource kind");
    }
    if(h.payload_bytes>max_control_payload)throw std::invalid_argument("Resource control payload exceeds limit");
    if(h.operation!=Operation::capabilities&&!h.resource_id)throw std::invalid_argument("Resource ID required");
    if(h.operation==Operation::capabilities&&h.resource_id)throw std::invalid_argument("Capabilities must not name a resource instance");
    if(h.operation==Operation::stop&&h.payload_bytes)throw std::invalid_argument("Stop must not contain a payload");
}
void put(std::span<std::uint8_t> bytes,std::size_t at,std::uint64_t value,unsigned size){
    for(unsigned i=0;i<size;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(8*(size-i-1)));
}
std::uint64_t take(std::span<const std::uint8_t> bytes,std::size_t at,unsigned size){
    std::uint64_t value=0;for(unsigned i=0;i<size;++i)value=(value<<8)|bytes[at+i];return value;
}
}
HeaderBytes encode(const Header& h){
    validate(h);
    HeaderBytes bytes{'E','P','R','S'};
    put(bytes,4,version,2);bytes[6]=static_cast<std::uint8_t>(h.operation);
    bytes[7]=static_cast<std::uint8_t>(h.kind);put(bytes,8,h.resource_id,8);put(bytes,16,h.payload_bytes,4);
    return bytes;
}
Header decode(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=24||bytes[0]!='E'||bytes[1]!='P'||bytes[2]!='R'||bytes[3]!='S')throw std::invalid_argument("Invalid resource envelope");
    if(take(bytes,4,2)!=version)throw std::invalid_argument("Resource protocol mismatch: update both endpoints");
    if(std::any_of(bytes.begin()+20,bytes.end(),[](auto byte){return byte!=0;}))throw std::invalid_argument("Reserved resource flags must be zero");
    Header h{static_cast<Operation>(bytes[6]),static_cast<ResourceKind>(bytes[7]),take(bytes,8,8),static_cast<std::uint32_t>(take(bytes,16,4))};
    validate(h);return h;
}
}
