#include <dm/usb_recovery.hpp>
#include <algorithm>
#include <stdexcept>
namespace dm::usb {
namespace {
void valid(const RecoveryRecord& r){
    const auto state=static_cast<unsigned>(r.state);
    if(state<1||state>7||r.disk_number==0xffffffffu||r.bus_id.size()>64||r.binding_id.size()>38||
       std::all_of(r.fingerprint.begin(),r.fingerprint.end(),[](auto b){return b==0;})||
       std::all_of(r.owner.begin(),r.owner.end(),[](auto b){return b==0;}))throw std::invalid_argument("Invalid USB recovery journal");
    if(!r.bus_id.empty()){
        bool dash=false;for(char c:r.bus_id){if(c=='-'&&!dash){dash=true;continue;}if(c<'0'||c>'9')throw std::invalid_argument("Invalid journal bus ID");}
        if(!dash||r.bus_id.front()=='-'||r.bus_id.back()=='-')throw std::invalid_argument("Invalid journal bus ID");
    }
    if(!r.binding_id.empty()){
        std::string_view guid=r.binding_id;if(guid.size()==38&&guid.front()=='{'&&guid.back()=='}')guid=guid.substr(1,36);
        if(guid.size()!=36)throw std::invalid_argument("Invalid journal binding ID");
        for(std::size_t i=0;i<36;++i){const char c=guid[i];if(i==8||i==13||i==18||i==23){if(c!='-')throw std::invalid_argument("Invalid journal binding ID");}
            else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))throw std::invalid_argument("Invalid journal binding ID");}
    }
    if((r.state==RecoveryState::prepared||r.state==RecoveryState::offline)&&(!r.bus_id.empty()||!r.binding_id.empty()))throw std::invalid_argument("Unexpected USB journal binding");
    if(r.state==RecoveryState::binding_intent&&(r.bus_id.empty()||!r.binding_id.empty()))throw std::invalid_argument("Incomplete USB binding intent");
    if((r.state==RecoveryState::bound||r.state==RecoveryState::released||r.state==RecoveryState::ejected)&&(r.bus_id.empty()||r.binding_id.empty()))throw std::invalid_argument("Incomplete USB journal binding");
}
void put(std::span<std::uint8_t> b,unsigned o,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b[o+n-1-i]=static_cast<std::uint8_t>(v>>(8*i));}
std::uint64_t get(std::span<const std::uint8_t> b,unsigned o,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|b[o+i];return v;}
}
bool valid_recovery_transition(RecoveryState from,RecoveryState to)noexcept{
    return (from==RecoveryState::prepared&&to==RecoveryState::offline)||
        (from==RecoveryState::offline&&to==RecoveryState::binding_intent)||
        (from==RecoveryState::binding_intent&&to==RecoveryState::bound)||
        (from==RecoveryState::bound&&to==RecoveryState::ejected)||
        (from==RecoveryState::ejected&&to==RecoveryState::released)||
        ((from==RecoveryState::prepared||from==RecoveryState::offline||from==RecoveryState::released||from==RecoveryState::restored)&&to==RecoveryState::restored);
}
std::vector<std::uint8_t> encode_recovery(const RecoveryRecord& r){
    valid(r);std::vector<std::uint8_t> b(72+r.bus_id.size()+r.binding_id.size());b[0]='E';b[1]='P';b[2]='U';b[3]='R';b[5]=1;b[6]=static_cast<std::uint8_t>(r.state);
    put(b,8,r.disk_number,4);put(b,12,r.prior_attributes,8);std::copy(r.fingerprint.begin(),r.fingerprint.end(),b.begin()+20);std::copy(r.owner.begin(),r.owner.end(),b.begin()+52);
    b[68]=static_cast<std::uint8_t>(r.bus_id.size());b[69]=static_cast<std::uint8_t>(r.binding_id.size());std::copy(r.bus_id.begin(),r.bus_id.end(),b.begin()+72);std::copy(r.binding_id.begin(),r.binding_id.end(),b.begin()+72+r.bus_id.size());return b;
}
RecoveryRecord decode_recovery(std::span<const std::uint8_t> b){
    if(b.size()<72||b.size()>174||b[0]!='E'||b[1]!='P'||b[2]!='U'||b[3]!='R'||b[4]||b[5]!=1||b[7]||b[70]||b[71]||b.size()!=72u+b[68]+b[69])throw std::invalid_argument("Malformed USB recovery journal");
    RecoveryRecord r;r.state=static_cast<RecoveryState>(b[6]);r.disk_number=static_cast<std::uint32_t>(get(b,8,4));r.prior_attributes=get(b,12,8);std::copy_n(b.begin()+20,32,r.fingerprint.begin());std::copy_n(b.begin()+52,16,r.owner.begin());
    r.bus_id.assign(b.begin()+72,b.begin()+72+b[68]);r.binding_id.assign(b.begin()+72+b[68],b.end());valid(r);return r;
}
}
