#pragma once
#include <dm/access.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>
namespace dm::access {
inline constexpr std::array<uint8_t,8> control_prefix{'E','P','C','L',1,0,0,0};
using ControlRequest=std::array<uint8_t,16>;
using ControlReply=std::array<uint8_t,128>;
enum class ControlOperation:uint8_t {open=1,ping=2,add=3,close=4};
inline ControlRequest control_request(ControlOperation op,unsigned kind=0){
    if(op<ControlOperation::open||op>ControlOperation::close||kind>3||(op!=ControlOperation::add&&kind))throw std::invalid_argument("Invalid control request");
    ControlRequest value{};std::copy(control_prefix.begin(),control_prefix.end(),value.begin());value[8]=static_cast<uint8_t>(op);value[9]=static_cast<uint8_t>(kind);return value;
}
inline std::pair<ControlOperation,unsigned> parse_control(const ControlRequest& value){
    const auto op=static_cast<ControlOperation>(value[8]);const auto kind=value[9];if(value!=control_request(op,kind))throw std::invalid_argument("Malformed control request");return {op,kind};
}
inline ControlReply control_reply(ControlOperation op,unsigned kind,bool protected_mode,const std::string& credential={}){
    (void)control_request(op,kind);ControlReply reply{'E','P','C','R',1,0,0,0};reply[8]=static_cast<uint8_t>(op);reply[9]=static_cast<uint8_t>(kind);reply[10]=protected_mode?1:0;
    if(op==ControlOperation::add&&protected_mode){auto ticket=channelCredential(credential);if(!ticket||ticket->kind!=kind)throw std::invalid_argument("Missing control credential");std::copy(credential.begin(),credential.end(),reply.begin()+16);}
    else if(!credential.empty())throw std::invalid_argument("Unexpected control credential");return reply;
}
inline std::string control_credential(const ControlReply& reply,ControlOperation op,unsigned kind,bool protected_mode){
    std::string credential;if(op==ControlOperation::add&&protected_mode)credential.assign(reply.begin()+16,reply.begin()+121);
    if(reply!=control_reply(op,kind,protected_mode,credential))throw std::invalid_argument("Invalid server control response");return credential;
}
}
