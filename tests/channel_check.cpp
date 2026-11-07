#include <extrapane/negotiation.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace extrapane;
using namespace extrapane::wire;
void check(bool value){if(!value)throw std::runtime_error("Resource channel check failed");}
template<class F> bool rejects(F action){try{action();return false;}catch(const std::exception&){return true;}}
struct ListenerStub:Listener{Availability start()override{return {true,{}};}void stop()noexcept override{}};
struct ResourceStub:Resource{
    int& stops;explicit ResourceStub(int& value):stops(value){}
    Availability prepare()override{return {true,{}};}
    Availability start()override{return {true,{}};}
    void stop()noexcept override{++stops;}
};
Message caps_message(const Capabilities& caps){auto body=encode_capabilities(caps);return {{Operation::capabilities,ResourceKind::display,0,static_cast<std::uint32_t>(body.size())},std::move(body)};}
Message request_message(std::uint64_t id,ResourceKind kind){auto body=encode_request({id,kind,"opaque-peer-device"});return {{Operation::request,kind,id,static_cast<std::uint32_t>(body.size())},std::move(body)};}
int main(){try{
    auto caps=unavailable_capabilities();caps[1].source={true,{}};
    const auto encoded=encode_capabilities(caps);check(decode_capabilities(encoded)[1].source.available);
    for(std::size_t length=0;length<encoded.size();++length)check(rejects([&]{decode_capabilities(std::span(encoded).first(length));}));
    auto trailing=encoded;trailing.push_back(0);check(rejects([&]{decode_capabilities(trailing);}));
    auto duplicate=caps;duplicate[1].kind=ResourceKind::display;check(rejects([&]{encode_capabilities(duplicate);}));
    auto oversized=caps;oversized[0].source.reason=std::string(1025,'a');check(rejects([&]{encode_capabilities(oversized);}));
    const auto request=request_message(7,ResourceKind::camera);
    check(decode_request(request.header,request.payload).device_id=="opaque-peer-device");
    Bytes buffer;std::size_t cursor=0;
    auto read=[&](std::span<std::uint8_t> out){if(out.size()>buffer.size()-cursor)throw std::runtime_error("Disconnected");std::copy_n(buffer.begin()+cursor,out.size(),out.begin());cursor+=out.size();};
    auto write=[&](std::span<const std::uint8_t> bytes){buffer.insert(buffer.end(),bytes.begin(),bytes.end());};
    ControlChannel channel(read,write);channel.send(request);auto received=channel.receive();check(received.payload==request.payload);
    check(rejects([&]{channel.receive();}));check(channel.failed());check(rejects([&]{channel.send(request);}));
    int readCalls=0;auto bad=encode(request.header);bad[16]=255;
    ControlChannel bounded([&](std::span<std::uint8_t> bytes){++readCalls;check(bytes.size()==24);std::copy(bad.begin(),bad.end(),bytes.begin());},write);
    check(rejects([&]{bounded.receive();}));check(readCalls==1);
    ControlChannel broken(read,[](std::span<const std::uint8_t>){throw std::runtime_error("Write failed");});
    check(rejects([&]{broken.send(request);}));check(broken.failed());
    Server server(3);check(server.start(std::make_unique<ListenerStub>()).available);
    check(server.admit(1).available);check(server.admit(2).available);
    auto local=unavailable_capabilities();local[1].destination={true,{}};
    int factories=0,stops=0;
    auto factory=[&](ResourceKind){++factories;return std::make_unique<ResourceStub>(stops);};
    Negotiation negotiation(server,1,local,factory);
    check(negotiation.handle(caps_message(caps)).header.operation==Operation::capabilities);
    check(negotiation.handle(request_message(1,ResourceKind::microphone)).header.operation==Operation::reject);check(factories==0);
    check(negotiation.handle(request_message(2,ResourceKind::camera)).header.operation==Operation::grant);check(factories==1);
    check(negotiation.handle(request_message(2,ResourceKind::camera)).header.operation==Operation::reject);check(factories==1);
    check(negotiation.handle({{Operation::stop,ResourceKind::microphone,2,0},{}}).header.operation==Operation::reject);check(stops==0);
    check(negotiation.handle({{Operation::stop,ResourceKind::camera,2,0},{}}).header.operation==Operation::status);check(stops==1);
    check(server.has_session(1)&&server.has_session(2)&&server.listening());
    check(negotiation.handle(request_message(3,ResourceKind::camera)).header.operation==Operation::grant);
    check(rejects([&]{negotiation.handle(caps_message(caps));}));check(stops==2);
    check(!server.has_session(1)&&server.has_session(2)&&server.listening());
    Negotiation beforeCaps(server,2,local,factory);
    check(rejects([&]{beforeCaps.handle(request);}));check(!server.has_session(2));
    check(rejects([&]{Negotiation invalid(server,9,local,factory);}));
    std::cout<<"Resource channel and negotiation checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
