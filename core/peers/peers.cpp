#include <extrapane/peers.hpp>
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace extrapane::peers {
namespace {
void need(bool value,const char* reason){if(!value)throw std::invalid_argument(reason);}
bool valid_kind(Kind kind){return static_cast<unsigned>(kind)<=5;}
bool screen(Kind kind){return kind==Kind::display_destination||kind==Kind::existing_screen;}
void text(const std::string& s,std::size_t limit,bool empty=false){
    need((empty||!s.empty())&&s.size()<=limit,"Invalid peer text length");
    const auto* p=reinterpret_cast<const unsigned char*>(s.data());std::size_t i=0;
    while(i<s.size()){
        unsigned c=p[i++];need(c>=32&&c!=127,"Control character in peer text");
        if(c<128)continue;
        unsigned count=0,scalar=0,min=0;
        if(c>=0xc2&&c<=0xdf){count=1;scalar=c&31;min=0x80;}
        else if(c>=0xe0&&c<=0xef){count=2;scalar=c&15;min=0x800;}
        else if(c>=0xf0&&c<=0xf4){count=3;scalar=c&7;min=0x10000;}
        else need(false,"Invalid UTF-8 peer text");
        need(i+count<=s.size(),"Truncated UTF-8 peer text");
        while(count--){const auto next=p[i++];need((next&0xc0)==0x80,"Invalid UTF-8 continuation");scalar=(scalar<<6)|(next&63);}
        need(scalar>=min&&scalar<=0x10ffff&&!(scalar>=0xd800&&scalar<=0xdfff),"Invalid UTF-8 scalar");
    }
}
void offer(const Offer& v){
    text(v.id,128);text(v.label,128);text(v.unavailable_reason,512,v.available);
    need(valid_kind(v.kind),"Unknown offered resource kind");
    need(!v.input_allowed||screen(v.kind),"Input only applies to screens");
    need(!v.available||v.unavailable_reason.empty(),"Available resource cannot have unavailable reason");
    if(v.width||v.height||v.refresh)need(screen(v.kind)&&v.width>0&&v.width<=16384&&v.height>0&&v.height<=16384&&v.refresh>0&&v.refresh<=1000,"Invalid offered display preset");
}
void announcement(const Announcement& a){
    need(valid_id(a.device)&&a.instance&&a.sequence,"Invalid discovery identity or generation");text(a.name,128);
    need(a.port&&a.lifetime_seconds>=2&&a.lifetime_seconds<=120&&a.offered_kinds<=63,"Invalid discovery fields");
}
Milliseconds later(Milliseconds now,Milliseconds delta){need(now<=std::numeric_limits<Milliseconds>::max()-delta,"Peer clock overflow");return now+delta;}
bool unicast(const std::string& ip){
    unsigned parts[4]{};std::size_t begin=0;
    for(unsigned i=0;i<4;++i){auto end=ip.find('.',begin);if(i==3)end=ip.size();else if(end==std::string::npos)return false;
        if(end==begin||end-begin>3)return false;auto result=std::from_chars(ip.data()+begin,ip.data()+end,parts[i]);
        if(result.ec!=std::errc{}||result.ptr!=ip.data()+end||parts[i]>255)return false;begin=end+1;
    }
    return begin==ip.size()+1&&parts[0]>0&&parts[0]!=127&&parts[0]<224;
}
struct Writer {
    Bytes bytes;
    void number(std::uint64_t n,unsigned count){for(unsigned i=count;i>0;--i)bytes.push_back(static_cast<std::uint8_t>(n>>((i-1)*8)));}
    void id(const DeviceId& value){bytes.insert(bytes.end(),value.begin(),value.end());}
    void string(const std::string& s){number(s.size(),2);bytes.insert(bytes.end(),s.begin(),s.end());}
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::uint64_t number(unsigned count){need(bytes.size()>=count,"Truncated peer message");std::uint64_t n=0;for(unsigned i=0;i<count;++i)n=(n<<8)|bytes[i];bytes=bytes.subspan(count);return n;}
    DeviceId id(){need(bytes.size()>=32,"Truncated device identity");DeviceId result{};std::copy_n(bytes.begin(),32,result.begin());bytes=bytes.subspan(32);return result;}
    std::string string(std::size_t limit){const auto size=number(2);need(size<=limit&&size<=bytes.size(),"Invalid peer string length");std::string s(reinterpret_cast<const char*>(bytes.data()),static_cast<std::size_t>(size));bytes=bytes.subspan(static_cast<std::size_t>(size));return s;}
    void finish(){need(bytes.empty(),"Trailing peer bytes");}
};
Bytes envelope(unsigned kind,Writer data){
    need(data.bytes.size()<=max_catalog_bytes-16,"Peer packet exceeds limit");Writer result;result.bytes={'E','P','D','V'};result.number(1,2);result.number(kind,1);result.number(0,1);result.number(data.bytes.size(),4);result.number(0,4);result.bytes.insert(result.bytes.end(),data.bytes.begin(),data.bytes.end());return result.bytes;
}
Reader envelope(std::span<const std::uint8_t> bytes,unsigned kind){
    need(bytes.size()>=16&&bytes.size()<=max_catalog_bytes,"Peer envelope length invalid");Reader in{bytes};need(in.number(4)==0x45504456&&in.number(2)==1&&in.number(1)==kind&&in.number(1)==0,"Peer envelope mismatch");const auto size=in.number(4);need(in.number(4)==0&&size==in.bytes.size(),"Peer envelope payload mismatch");return in;
}
const Offer* find(const Catalog& catalog,const std::string& id){for(const auto& item:catalog.offers)if(item.id==id)return &item;return nullptr;}
}
bool valid_id(const DeviceId& id)noexcept{return std::any_of(id.begin(),id.end(),[](auto n){return n!=0;});}
void validate(const Catalog& c){
    need(valid_id(c.device)&&c.instance&&c.revision,"Invalid catalog identity or revision");text(c.name,128);need(c.offers.size()<=max_resources,"Too many catalog resources");
    std::size_t bytes=16+32+8+8+2+c.name.size()+2;
    std::set<std::string> ids;for(const auto& value:c.offers){offer(value);need(ids.insert(value.id).second,"Duplicate catalog resource");bytes+=20+value.id.size()+value.label.size()+value.unavailable_reason.size();}
    need(bytes<=max_catalog_bytes,"Catalog exceeds wire limit");
}
Bytes encode(const Announcement& a){announcement(a);Writer out;out.id(a.device);out.number(a.instance,8);out.number(a.sequence,8);out.string(a.name);out.number(a.port,2);out.number(a.lifetime_seconds,2);out.number(a.offered_kinds,1);out.number(a.pairing_required,1);return envelope(1,std::move(out));}
Bytes encode(const Withdrawal& w){need(valid_id(w.device)&&w.instance&&w.sequence,"Invalid withdrawal");Writer out;out.id(w.device);out.number(w.instance,8);out.number(w.sequence,8);return envelope(2,std::move(out));}
Bytes encode(const Catalog& c){validate(c);Writer out;out.id(c.device);out.number(c.instance,8);out.number(c.revision,8);out.string(c.name);out.number(c.offers.size(),2);
    for(const auto& o:c.offers){out.string(o.id);out.string(o.label);out.number(static_cast<unsigned>(o.kind),1);out.number((o.available?1:0)|(o.input_allowed?2:0),1);out.number(o.width,4);out.number(o.height,4);out.number(o.refresh,4);out.string(o.unavailable_reason);}return envelope(3,std::move(out));}
Announcement decode_announcement(std::span<const std::uint8_t> b){auto in=envelope(b,1);Announcement a;a.device=in.id();a.instance=in.number(8);a.sequence=in.number(8);a.name=in.string(128);a.port=static_cast<std::uint16_t>(in.number(2));a.lifetime_seconds=static_cast<std::uint16_t>(in.number(2));a.offered_kinds=static_cast<std::uint8_t>(in.number(1));const auto flag=in.number(1);need(flag<=1,"Invalid pairing flag");a.pairing_required=flag!=0;in.finish();announcement(a);return a;}
Withdrawal decode_withdrawal(std::span<const std::uint8_t> b){auto in=envelope(b,2);Withdrawal w;w.device=in.id();w.instance=in.number(8);w.sequence=in.number(8);in.finish();need(valid_id(w.device)&&w.instance&&w.sequence,"Invalid withdrawal");return w;}
Catalog decode_catalog(std::span<const std::uint8_t> b){auto in=envelope(b,3);Catalog c;c.device=in.id();c.instance=in.number(8);c.revision=in.number(8);c.name=in.string(128);const auto count=in.number(2);need(count<=max_resources,"Too many catalog resources");
    for(unsigned i=0;i<count;++i){Offer o;o.id=in.string(128);o.label=in.string(128);o.kind=static_cast<Kind>(in.number(1));const auto flags=in.number(1);need(flags<=3,"Invalid offer flags");o.available=flags&1;o.input_allowed=flags&2;o.width=static_cast<std::uint32_t>(in.number(4));o.height=static_cast<std::uint32_t>(in.number(4));o.refresh=static_cast<std::uint32_t>(in.number(4));o.unavailable_reason=in.string(512);c.offers.push_back(std::move(o));}in.finish();validate(c);return c;}

void DiscoveryTable::interfaces(std::set<std::uint32_t> selected){selected.erase(0);interfaces_=std::move(selected);for(auto i=routes_.begin();i!=routes_.end();)if(!interfaces_.contains(i->second.interface_index))i=routes_.erase(i);else ++i;}
bool DiscoveryTable::observe(const Announcement& a,std::string ip,std::uint32_t index,Milliseconds now){
    announcement(a);if(!interfaces_.contains(index)||!unicast(ip))return false;expire(now);const Key key{a.device,index};auto at=routes_.find(key);
    if(at!=routes_.end()&&at->second.announcement.instance==a.instance&&a.sequence<=at->second.announcement.sequence)return false;
    if(at==routes_.end()&&routes_.size()>=max_discovered_routes)return false;
    routes_[key]={a,std::move(ip),index,later(now,Milliseconds(a.lifetime_seconds)*1000)};return true;
}
bool DiscoveryTable::withdraw(const Withdrawal& w,const std::string& ip,std::uint32_t index){
    auto found=routes_.find({w.device,index});if(found==routes_.end())return false;const auto& route=found->second;
    if(route.observed_ipv4!=ip||route.announcement.instance!=w.instance||w.sequence<=route.announcement.sequence)return false;routes_.erase(found);return true;
}
void DiscoveryTable::expire(Milliseconds now){for(auto i=routes_.begin();i!=routes_.end();)if(i->second.expires_at<=now)i=routes_.erase(i);else ++i;}
std::vector<Route> DiscoveryTable::snapshot(Milliseconds now){expire(now);std::vector<Route> result;for(const auto& [key,r]:routes_)result.push_back(r);return result;}

SharingPolicy::SharingPolicy(Catalog local):local_(std::move(local)){validate(local_);}
void SharingPolicy::remember(const DeviceId& id){need(valid_id(id)&&id!=local_.device,"Invalid trusted peer");trusted_.insert(id);}
std::vector<Grant> SharingPolicy::cancel_matching(const DeviceId* id,bool delegated){
    std::vector<Grant> result;for(auto i=grants_.begin();i!=grants_.end();)if((!id||i->second.visitor==*id||i->second.owner==*id)&&(!delegated||i->second.delegated)){result.push_back(i->second);i=grants_.erase(i);}else ++i;return result;
}
std::vector<Grant> SharingPolicy::forget(const DeviceId& id){trusted_.erase(id);remotes_.erase(id);return cancel_matching(&id,false);}
std::vector<Grant> SharingPolicy::remote_removed(const DeviceId& id){remotes_.erase(id);return cancel_matching(&id,true);}
std::vector<Grant> SharingPolicy::sharing(bool value){sharing_=value;return value?std::vector<Grant>{}:cancel_matching(nullptr,false);}
std::vector<Grant> SharingPolicy::pairing_required(bool value){
    pairing_required_=value;std::vector<Grant> result;
    if(value){
        for(auto i=grants_.begin();i!=grants_.end();){
            if(!i->second.paired){result.push_back(i->second);i=grants_.erase(i);}
            else ++i;
        }
    }
    return result;
}
std::vector<Grant> SharingPolicy::gateway(bool value){gateway_=value;return value?std::vector<Grant>{}:cancel_matching(nullptr,true);}
std::vector<Grant> SharingPolicy::offers(std::vector<Offer> values){
    Catalog next=local_;need(next.revision!=UINT64_MAX,"Catalog revision exhausted");next.offers=std::move(values);++next.revision;validate(next);local_=std::move(next);std::vector<Grant> result;
    for(auto i=grants_.begin();i!=grants_.end();){const auto* o=i->second.owner==local_.device?find(local_,i->second.resource):nullptr;
        if(i->second.owner==local_.device&&(!o||!o->available||o->kind!=i->second.kind||(i->second.input&&!o->input_allowed))){result.push_back(i->second);i=grants_.erase(i);}else ++i;}
    return result;
}
std::vector<Grant> SharingPolicy::remote(const ConnectionIdentity& peer,Catalog catalog,unsigned hops,Milliseconds now,std::uint16_t ttl){
    validate(catalog);need(peer.paired&&trusted_.contains(peer.device)&&peer.device==catalog.device&&hops==0,"Remote catalog requires a directly paired owner");need(ttl>=2&&ttl<=120,"Invalid remote catalog lifetime");
    const auto expires=later(now,Milliseconds(ttl)*1000);
    const auto old=remotes_.find(peer.device);if(old!=remotes_.end()&&old->second.catalog.instance==catalog.instance){need(catalog.revision>=old->second.catalog.revision,"Stale remote catalog");if(catalog.revision==old->second.catalog.revision)need(catalog==old->second.catalog,"Catalog changed without revision");}
    std::vector<Grant> result;for(auto i=grants_.begin();i!=grants_.end();){const auto* o=i->second.owner==peer.device?find(catalog,i->second.resource):nullptr;
        if(i->second.owner==peer.device&&(!o||!o->available||o->kind!=i->second.kind||(i->second.input&&!o->input_allowed)||(old!=remotes_.end()&&old->second.catalog.instance!=catalog.instance))){result.push_back(i->second);i=grants_.erase(i);}else ++i;}
    remotes_[peer.device]={std::move(catalog),expires};return result;
}
std::vector<Grant> SharingPolicy::expire(Milliseconds now){std::vector<Grant> result;for(auto i=remotes_.begin();i!=remotes_.end();)if(i->second.expires<=now){auto stopped=cancel_matching(&i->first,true);result.insert(result.end(),stopped.begin(),stopped.end());i=remotes_.erase(i);}else ++i;return result;}
std::vector<Catalog> SharingPolicy::visible(const ConnectionIdentity& visitor,Milliseconds now)const{
    if(!sharing_||!valid_id(visitor.device))return {};const bool paired=visitor.paired&&(visitor.device==local_.device||trusted_.contains(visitor.device));if(pairing_required_&&!paired)return {};
    std::vector<Catalog> result{local_};if(gateway_&&paired&&visitor.device!=local_.device)for(const auto& [id,remote]:remotes_)if(id!=visitor.device&&trusted_.contains(id)&&remote.expires>now)result.push_back(remote.catalog);return result;
}
Decision SharingPolicy::open(const ConnectionIdentity& visitor,const DeviceId& owner,const std::string& resource,bool input,Milliseconds now){
    if(!sharing_)return {{},"Sharing is stopped"};if(!valid_id(visitor.device))return {{},"Missing connection identity"};const bool paired=visitor.paired&&(visitor.device==local_.device||trusted_.contains(visitor.device));
    if(pairing_required_&&!paired)return {{},"Pair this device first"};const Catalog* catalog=&local_;const bool delegated=owner!=local_.device;
    if(delegated){if(!gateway_||!paired||!trusted_.contains(owner)||owner==visitor.device||visitor.device==local_.device)return {{},"Gateway access is unavailable"};auto remote=remotes_.find(owner);if(remote==remotes_.end()||remote->second.expires<=now)return {{},"Paired destination is offline"};catalog=&remote->second.catalog;}
    const auto* o=find(*catalog,resource);if(!o)return {{},"Resource is not offered"};if(!o->available)return {{},o->unavailable_reason};if(input&&!o->input_allowed)return {{},"Remote input is not enabled for this resource"};
    if(o->kind==Kind::usb)for(const auto& [id,g]:grants_)if(g.owner==owner&&g.resource==resource)return {{},"USB storage is already in use"};
    if(next_grant_==UINT64_MAX)return {{},"Session identity exhausted"};Grant grant{++next_grant_,visitor.device,owner,resource,o->kind,input,delegated,paired};grants_.emplace(grant.id,grant);return {grant,{}};
}
bool SharingPolicy::close(std::uint64_t id)noexcept{return grants_.erase(id)!=0;}
bool SharingPolicy::close(const ConnectionIdentity& requester,std::uint64_t id)noexcept{
    const auto found=grants_.find(id);if(found==grants_.end()||found->second.visitor!=requester.device)return false;
    if(found->second.paired&&(!requester.paired||(requester.device!=local_.device&&!trusted_.contains(requester.device))))return false;
    grants_.erase(found);return true;
}
std::vector<Grant> SharingPolicy::grants()const{std::vector<Grant> result;for(const auto& [id,g]:grants_)result.push_back(g);return result;}

std::optional<Milliseconds> Reconnect::failed(Failure why,Milliseconds now){
    if(stopped_||kind_==Kind::usb||why!=Failure::transport_lost){stop();return {};}
    if(!interrupted_)interrupted_=now;if(now<*interrupted_||now-*interrupted_>=30000||attempts_>=5){stop();return {};}
    constexpr Milliseconds delays[]{1000,2000,4000,8000,8000};const auto delay=delays[attempts_];if(delay>30000-(now-*interrupted_)){stop();return {};}
    due_=later(now,delay);++attempts_;return due_;
}
bool Reconnect::ready(Milliseconds now)const noexcept{return !stopped_&&due_&&now>=*due_&&interrupted_&&now>=*interrupted_&&now-*interrupted_<30000;}
void Reconnect::connected()noexcept{attempts_=0;interrupted_.reset();due_.reset();stopped_=false;}
void Reconnect::stop()noexcept{stopped_=true;due_.reset();}
}
