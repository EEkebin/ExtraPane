#include <extrapane/channel.hpp>
#include <limits>
#include <stdexcept>
#include <utility>

namespace extrapane::wire {
namespace {
constexpr std::size_t max_text=1024;
void put_text(Bytes& out,const std::string& text){
    if(text.size()>max_text || text.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid resource text");
    out.push_back(static_cast<std::uint8_t>(text.size()>>8));out.push_back(static_cast<std::uint8_t>(text.size()));
    out.insert(out.end(),text.begin(),text.end());
}
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data):data_(data){}
    std::uint8_t byte(){if(data_.empty())throw std::invalid_argument("Truncated resource payload");auto value=data_.front();data_=data_.subspan(1);return value;}
    std::string text(){
        const auto high=byte();const auto low=byte();const auto length=(std::size_t(high)<<8)|low;
        if(length>max_text || length>data_.size())throw std::invalid_argument("Invalid resource text length");
        std::string result(data_.begin(),data_.begin()+length);data_=data_.subspan(length);
        if(result.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid resource text");
        return result;
    }
    void finish(){if(!data_.empty())throw std::invalid_argument("Trailing resource payload");}
private:std::span<const std::uint8_t> data_;
};
void put_availability(Bytes& out,const Availability& value){out.push_back(value.available?1:0);put_text(out,value.reason);}
Availability take_availability(Reader& in){auto flag=in.byte();if(flag>1)throw std::invalid_argument("Invalid availability flag");return {flag!=0,in.text()};}
}
ControlChannel::ControlChannel(Read read,Write write):read_(std::move(read)),write_(std::move(write)){
    if(!read_||!write_)throw std::invalid_argument("Missing encrypted stream callbacks");
}
Message ControlChannel::receive(){
    if(failed_)throw std::runtime_error("Resource channel is closed after failure");
    try{
        HeaderBytes bytes;read_(bytes);auto header=decode(bytes);
        Message message{header,Bytes(header.payload_bytes)};
        if(!message.payload.empty())read_(message.payload);
        return message;
    }catch(...){failed_=true;throw;}
}
void ControlChannel::send(const Message& message){
    if(failed_)throw std::runtime_error("Resource channel is closed after failure");
    if(message.header.payload_bytes!=message.payload.size())throw std::invalid_argument("Resource payload length mismatch");
    const auto header=encode(message.header);
    try{write_(header);if(!message.payload.empty())write_(message.payload);}
    catch(...){failed_=true;throw;}
}
Bytes encode_capabilities(const Capabilities& capabilities){
    Bytes result;unsigned seen=0;
    for(const auto& c:capabilities){
        const auto kind=static_cast<unsigned>(c.kind);
        if(kind>3 || (seen&(1u<<kind)))throw std::invalid_argument("Invalid capability kinds");
        seen|=1u<<kind;result.push_back(static_cast<std::uint8_t>(kind));
        put_availability(result,c.source);put_availability(result,c.destination);
    }return result;
}
Capabilities decode_capabilities(std::span<const std::uint8_t> bytes){
    Reader in(bytes);auto result=unavailable_capabilities();unsigned seen=0;
    for(unsigned i=0;i<4;++i){
        const auto kind=in.byte();if(kind>3 || (seen&(1u<<kind)))throw std::invalid_argument("Invalid capability kinds");
        seen|=1u<<kind;result[kind]={static_cast<ResourceKind>(kind),take_availability(in),take_availability(in)};
    }in.finish();return result;
}
Bytes encode_request(const ResourceRequest& request){
    // Validate kind/id through the common envelope validator.
    (void)encode({Operation::request,request.kind,request.id,0});
    if(request.device_id.empty())throw std::invalid_argument("Resource device identifier required");
    Bytes result;put_text(result,request.device_id);return result;
}
ResourceRequest decode_request(const Header& header,std::span<const std::uint8_t> bytes){
    (void)encode(header);
    if(header.operation!=Operation::request || header.payload_bytes!=bytes.size())throw std::invalid_argument("Invalid resource request envelope");
    Reader in(bytes);auto id=in.text();in.finish();
    if(id.empty())throw std::invalid_argument("Resource device identifier required");
    return {header.resource_id,header.kind,std::move(id)};
}
}
