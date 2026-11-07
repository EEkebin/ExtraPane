#pragma once
#include <dm/control.hpp>
#include <atomic>
namespace dm::mac {
class Transport {
    access::ControlOperation operation_=access::ControlOperation::open;unsigned kind_=0;
    std::string code_;
public:
    inline static std::atomic_int created=0,closed=0;
    inline static std::atomic_bool malformed=false;
    Transport(const char*,const char*,unsigned,std::string code):code_(std::move(code)){++created;}
    ~Transport(){++closed;}
    void connect(){if(code_!="123456")throw std::runtime_error("Access code rejected");}
    void cancel(){}
    void write(std::span<const uint8_t> bytes){access::ControlRequest request{};if(bytes.size()!=request.size())throw std::runtime_error("Bad request length");std::copy(bytes.begin(),bytes.end(),request.begin());auto parsed=access::parse_control(request);operation_=parsed.first;kind_=parsed.second;}
    std::vector<uint8_t> read(size_t count,int){
        auto reply=access::control_reply(operation_,kind_,true,operation_==access::ControlOperation::add?"EPCT1:"+std::to_string(kind_)+":"+std::string(32,'a')+":"+std::string(64,'b'):std::string{});
        if(count!=reply.size())throw std::runtime_error("Bad reply length");if(malformed)reply[0]=0;return {reply.begin(),reply.end()};
    }
};
}
