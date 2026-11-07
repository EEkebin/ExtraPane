#pragma once
#include <dm/access.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>
namespace dm::access {
inline constexpr std::array<uint8_t,8> group_prefix{'E','P','G','R',1,0,0,0};
using GroupRequest=std::array<uint8_t,16>;
using GroupReply=std::array<uint8_t,520>;
inline GroupRequest group_request(unsigned mask){if(!mask||mask>15)throw std::invalid_argument("Select one or more supported resources");GroupRequest data{};std::copy(group_prefix.begin(),group_prefix.end(),data.begin());data[8]=static_cast<uint8_t>(mask);return data;}
inline unsigned group_mask(const GroupRequest& data){if(!std::equal(group_prefix.begin(),group_prefix.end(),data.begin())||!data[8]||data[8]>15||!std::all_of(data.begin()+9,data.end(),[](uint8_t v){return v==0;}))throw std::invalid_argument("Invalid grouped admission request");return data[8];}
inline GroupReply group_reply(unsigned mask,const std::vector<ChannelCredential>& tickets){
    (void)group_request(mask);GroupReply data{'E','P','G','S',1,static_cast<uint8_t>(mask),0,0};unsigned seen=0;
    for(const auto& item:tickets){if(item.kind>3||!(mask&(1u<<item.kind))||(seen&(1u<<item.kind)))throw std::invalid_argument("Unexpected grouped credential");seen|=1u<<item.kind;const auto text=formatCredential(item);std::copy(text.begin(),text.end(),data.begin()+8+128*item.kind);}
    if(seen!=mask)throw std::invalid_argument("Incomplete grouped credentials");return data;
}
inline std::vector<ChannelCredential> group_tickets(const GroupReply& data,unsigned mask){
    (void)group_request(mask);const std::array<uint8_t,8> header{'E','P','G','S',1,static_cast<uint8_t>(mask),0,0};if(!std::equal(header.begin(),header.end(),data.begin()))throw std::invalid_argument("Invalid grouped admission response");std::vector<ChannelCredential> result;
    for(unsigned kind=0;kind<4;++kind){const auto start=data.begin()+8+kind*128;
        if(!(mask&(1u<<kind))){if(!std::all_of(start,start+128,[](uint8_t v){return v==0;}))throw std::invalid_argument("Unrequested grouped credential");continue;}
        auto ticket=channelCredential(std::string(start,start+105));if(!ticket||ticket->kind!=kind||!std::all_of(start+105,start+128,[](uint8_t v){return v==0;}))throw std::invalid_argument("Malformed grouped credential");result.push_back(std::move(*ticket));
    }return result;
}
}
