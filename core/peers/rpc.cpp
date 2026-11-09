#include <extrapane/peer_rpc.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace extrapane::peers::rpc {
namespace {
void require(bool ok,const char* message="Invalid peer control message"){if(!ok)throw std::invalid_argument(message);}
void put(std::span<std::uint8_t> out,std::uint64_t n){for(std::size_t i=out.size();i;--i){out[i-1]=static_cast<std::uint8_t>(n);n>>=8;}}
std::uint64_t get(std::span<const std::uint8_t> in){std::uint64_t n=0;for(auto b:in)n=(n<<8)|b;return n;}
void valid(const Message& m,std::size_t size){
    require(m.id&&size<=max_payload);
    switch(m.operation){
    case Operation::forget:case Operation::forget_ack:case Operation::cancel_request:case Operation::catalog:case Operation::close:case Operation::stopped:case Operation::stream_close:case Operation::ping:case Operation::pong:require(!size);break;
    case Operation::granted:require(size>=10&&size<=115);break;
    case Operation::stream_open:case Operation::stream_credit:require(size==8);break;
    case Operation::stream_data:require(size&&size<=data_chunk);break;
    case Operation::rejected:require(size&&size<=1024);break;
    case Operation::open:require(size>=50&&size<=410);break;
    case Operation::catalog_reply:require(size>=4);break;
    default:require(false);
    }
}
}
Header header(const Message& m){
    valid(m,m.payload.size());Header out{'E','P','P','C',1,static_cast<std::uint8_t>(m.operation)};
    put(std::span(out).subspan(8,8),m.id);put(std::span(out).subspan(16,4),m.payload.size());return out;
}
Message decode_header(std::span<const std::uint8_t> bytes,std::size_t& length){
    require(bytes.size()==header_size&&std::equal(bytes.begin(),bytes.begin()+5,std::array<std::uint8_t,5>{'E','P','P','C',1}.begin()));
    require(!bytes[6]&&!bytes[7]&&get(bytes.subspan(20,4))==0);
    Message result{static_cast<Operation>(bytes[5]),get(bytes.subspan(8,8)),{}};length=static_cast<std::size_t>(get(bytes.subspan(16,4)));valid(result,length);return result;
}
Bytes encode(const Message& m){auto h=header(m);Bytes out(h.begin(),h.end());out.insert(out.end(),m.payload.begin(),m.payload.end());return out;}
Bytes integer(std::uint64_t value){Bytes out(8);put(out,value);return out;}
std::uint64_t integer(std::span<const std::uint8_t> value){require(value.size()==8);return get(value);}
Bytes detail(const std::string& value){require(!value.empty()&&value.size()<=1024&&value.find('\0')==std::string::npos);return {value.begin(),value.end()};}
std::string detail(std::span<const std::uint8_t> value){require(!value.empty()&&value.size()<=1024&&std::find(value.begin(),value.end(),0)==value.end());return {value.begin(),value.end()};}
Bytes encode_catalogs(const std::vector<Catalog>& catalogs){
    require(catalogs.size()<=256);Bytes result(4);put(result,catalogs.size());std::set<DeviceId> seen;
    for(const auto& catalog:catalogs){require(seen.insert(catalog.device).second);auto payload=peers::encode(catalog);require(result.size()+4+payload.size()<=max_payload,"Visible catalog exceeds connection limit");const auto offset=result.size();result.resize(offset+4);put(std::span(result).subspan(offset,4),payload.size());result.insert(result.end(),payload.begin(),payload.end());}
    return result;
}
std::vector<Catalog> decode_catalogs(std::span<const std::uint8_t> bytes){
    require(bytes.size()>=4&&bytes.size()<=max_payload);const auto count=get(bytes.first(4));require(count<=256);bytes=bytes.subspan(4);std::vector<Catalog> result;std::set<DeviceId> seen;
    for(std::uint64_t i=0;i<count;++i){require(bytes.size()>=4);const auto size=get(bytes.first(4));bytes=bytes.subspan(4);require(size<=bytes.size());auto item=peers::decode_catalog(bytes.first(static_cast<std::size_t>(size)));require(seen.insert(item.device).second);result.push_back(std::move(item));bytes=bytes.subspan(static_cast<std::size_t>(size));}
    require(bytes.empty());return result;
}
Bytes encode(const Granted& grant){
    require(grant.grant&&grant.native_credential.size()<=105&&std::all_of(grant.native_credential.begin(),grant.native_credential.end(),[](unsigned char c){return c>=33&&c<=126;}));
    Bytes result(10+grant.native_credential.size());put(std::span(result).first(8),grant.grant);put(std::span(result).subspan(8,2),grant.native_credential.size());std::copy(grant.native_credential.begin(),grant.native_credential.end(),result.begin()+10);return result;
}
Granted decode_granted(std::span<const std::uint8_t> bytes){
    require(bytes.size()>=10&&bytes.size()<=115&&get(bytes.subspan(8,2))==bytes.size()-10);
    Granted result{get(bytes.first(8)),{bytes.begin()+10,bytes.end()}};encode(result);return result;
}
Bytes encode(const Open& request){
    require(valid_id(request.owner)&&!request.resource.empty()&&request.resource.size()<=256&&request.resource.find('\0')==std::string::npos);
    require((!request.width&&!request.height&&!request.refresh)||(request.width>=64&&request.width<=16384&&request.height>=64&&request.height<=16384&&request.refresh&&request.refresh<=360));
    require(request.native_credential.size()<=105&&std::all_of(request.native_credential.begin(),request.native_credential.end(),[](unsigned char c){return c>=33&&c<=126;}));
    Bytes result(49+request.resource.size()+request.native_credential.size());std::copy(request.owner.begin(),request.owner.end(),result.begin());result[32]=request.input?1:0;
    put(std::span(result).subspan(33,4),request.width);put(std::span(result).subspan(37,4),request.height);put(std::span(result).subspan(41,4),request.refresh);put(std::span(result).subspan(45,2),request.resource.size());std::copy(request.resource.begin(),request.resource.end(),result.begin()+47);const auto offset=47+request.resource.size();put(std::span(result).subspan(offset,2),request.native_credential.size());std::copy(request.native_credential.begin(),request.native_credential.end(),result.begin()+offset+2);return result;
}
Open decode_open(std::span<const std::uint8_t> bytes){
    require(bytes.size()>=50&&bytes.size()<=410&&bytes[32]<=1);
    const auto resource_size=static_cast<std::size_t>(get(bytes.subspan(45,2)));require(resource_size&&resource_size<=256&&49+resource_size<=bytes.size());
    const auto offset=47+resource_size;const auto credential_size=get(bytes.subspan(offset,2));require(credential_size<=105&&offset+2+credential_size==bytes.size());
    Open request;std::copy_n(bytes.begin(),32,request.owner.begin());request.input=bytes[32]!=0;request.width=static_cast<std::uint32_t>(get(bytes.subspan(33,4)));request.height=static_cast<std::uint32_t>(get(bytes.subspan(37,4)));request.refresh=static_cast<std::uint32_t>(get(bytes.subspan(41,4)));request.resource={bytes.begin()+47,bytes.begin()+offset};request.native_credential={bytes.begin()+offset+2,bytes.end()};encode(request);return request;
}
std::vector<Message> Decoder::feed(std::span<const std::uint8_t> bytes){
    require(!failed_,"Peer control decoder is closed");
    try{
        require(bytes.size()<=max_payload+header_size&&pending_.size()+bytes.size()<=2*(max_payload+header_size));pending_.insert(pending_.end(),bytes.begin(),bytes.end());std::vector<Message> result;
        std::size_t offset=0;
        while(pending_.size()-offset>=header_size){std::size_t length=0;auto message=decode_header(std::span(pending_).subspan(offset,header_size),length);if(pending_.size()-offset<header_size+length)break;message.payload.assign(pending_.begin()+offset+header_size,pending_.begin()+offset+header_size+length);offset+=header_size+length;result.push_back(std::move(message));}
        pending_.erase(pending_.begin(),pending_.begin()+offset);return result;
    }catch(...){failed_=true;pending_.clear();throw;}
}
Multiplexer::Multiplexer(bool initiator,Authorize check):initiator_(initiator),next_(initiator?1:2),authorize_(std::move(check)){require(static_cast<bool>(authorize_));}
Message Multiplexer::open(std::uint64_t grant){
    require(grant&&authorize_(grant),"Resource grant is not authorized");require(streams_.size()<max_streams&&next_<=UINT64_MAX-2,"Peer stream capacity reached");const auto id=next_;next_+=2;streams_.emplace(id,StreamInfo{grant});return {Operation::stream_open,id,integer(grant)};
}
void Multiplexer::opened(const Message& m){
    require(m.operation==Operation::stream_open&&m.id>last_remote_&&static_cast<bool>(m.id&1)!=initiator_&& !streams_.contains(m.id)&&streams_.size()<max_streams);
    const auto grant=integer(m.payload);require(grant&&authorize_(grant),"Resource grant is not authorized");streams_.emplace(m.id,StreamInfo{grant});last_remote_=m.id;
}
std::optional<Message> Multiplexer::send(std::uint64_t id,std::span<const std::uint8_t> bytes){
    auto found=streams_.find(id);require(found!=streams_.end());auto& stream=found->second;require(authorize_(stream.grant),"Resource grant was revoked");if(bytes.empty()||!stream.send_credit)return {};
    const auto count=std::min({bytes.size(),stream.send_credit,data_chunk});stream.send_credit-=count;return Message{Operation::stream_data,id,Bytes(bytes.begin(),bytes.begin()+count)};
}
void Multiplexer::receive(const Message& m){
    auto found=streams_.find(m.id);require(found!=streams_.end()&&m.operation==Operation::stream_data);auto& stream=found->second;require(authorize_(stream.grant),"Resource grant was revoked");
    require(!m.payload.empty()&&m.payload.size()<=data_chunk&&m.payload.size()<=stream.receive_credit&&queued_+m.payload.size()<=max_queued,"Peer media exceeded its receive window");
    stream.receive_credit-=m.payload.size();queued_+=m.payload.size();stream.pending.insert(stream.pending.end(),m.payload.begin(),m.payload.end());
}
std::span<const std::uint8_t> Multiplexer::pending(std::uint64_t id)const{const auto found=streams_.find(id);require(found!=streams_.end()&&authorize_(found->second.grant));return found->second.pending;}
Message Multiplexer::consumed(std::uint64_t id,std::size_t count){
    auto found=streams_.find(id);require(found!=streams_.end());auto& stream=found->second;require(count&&count<=stream.pending.size()&&count<=window-stream.receive_credit);
    stream.pending.erase(stream.pending.begin(),stream.pending.begin()+count);stream.receive_credit+=count;queued_-=count;return {Operation::stream_credit,id,integer(count)};
}
void Multiplexer::credited(const Message& m){auto found=streams_.find(m.id);require(found!=streams_.end()&&m.operation==Operation::stream_credit);const auto count=integer(m.payload);require(count&&count<=window-found->second.send_credit);found->second.send_credit+=static_cast<std::size_t>(count);}
bool Multiplexer::close(std::uint64_t id)noexcept{auto found=streams_.find(id);if(found==streams_.end())return false;queued_-=found->second.pending.size();streams_.erase(found);return true;}
std::vector<std::uint64_t> Multiplexer::revoke(std::uint64_t grant){std::vector<std::uint64_t> result;for(const auto& [id,stream]:streams_)if(stream.grant==grant)result.push_back(id);for(auto id:result)close(id);return result;}
std::vector<std::uint64_t> Multiplexer::ids()const{std::vector<std::uint64_t> result;for(const auto& [id,_]:streams_)result.push_back(id);return result;}
}
