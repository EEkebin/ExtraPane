#include <dm/usb_tunnel.hpp>
#include <extrapane/usb_tunnel.hpp>
#include <array>
#include <thread>
#include <stdexcept>
namespace dm::usb {
void pump_tunnel(security::TlsStream& remote,net::TcpStream& local,bool importer,const std::string& bus,std::atomic_bool& stop){
    if(!local.local_endpoint().starts_with("127.0.0.1:")||!local.peer_endpoint().starts_with("127.0.0.1:")||(!importer&&bus.empty())||(importer&&!bus.empty()))throw std::invalid_argument("USB tunnel requires scoped loopback endpoints and an exact exporter identity");
    extrapane::usb::tunnel::Pump pump(importer?std::string{}:bus);
    std::jthread cancel([&](std::stop_token token){while(!token.stop_requested()&&!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(25));if(stop.load()){remote.cancel();local.cancel();}});
    struct Shutdown{security::TlsStream& remote;net::TcpStream& local;~Shutdown(){remote.cancel();local.cancel();}}shutdown{remote,local};
    auto flush=[&]{while(!pump.pending().empty()){const auto count=local.write_some(pump.pending());if(count<0)break;if(!count)throw std::runtime_error("Local USB/IP stream closed");pump.consume(static_cast<std::size_t>(count));}};
    auto receive=[&]{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);extrapane::usb::tunnel::Header header{};remote.read(header,deadline);auto frame=extrapane::usb::tunnel::decode(header);if(!frame.payload.empty())remote.read(frame.payload,deadline);pump.accept(std::move(frame));};
    std::array<std::uint8_t,extrapane::usb::tunnel::maximum_payload> buffer{};
    auto send=[&]{flush();int count=-1;if(pump.read_limit())count=local.read_some(std::span(buffer).first(pump.read_limit()));auto frame=pump.next(std::span(buffer).first(count>0?static_cast<std::size_t>(count):0),count==0||stop.load());auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);remote.write(extrapane::usb::tunnel::encode(frame),deadline);if(!frame.payload.empty())remote.write(frame.payload,deadline);return frame.payload.empty();};
    while(!stop.load()&&!pump.stopped()){
        if(importer){const bool idle=send();if(pump.stopped())break;receive();flush();if(idle)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        else{receive();if(pump.stopped())break;send();}
    }
    // jthread joins before borrowed streams can leave their owner's scope. No
    // TLS read/write calls ever overlap; only native-socket shutdown is concurrent.
}
}
