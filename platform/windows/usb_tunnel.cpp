#include <dm/usb_tunnel.hpp>
#include <extrapane/usb_tunnel.hpp>
#include <array>
#include <thread>
#include <stdexcept>
namespace dm::usb {
void pump_tunnel(security::TlsStream& remote,net::TcpStream& local,bool importer,const std::string& bus,std::atomic_bool& stop,bool keep_tls_open,std::atomic_bool* return_request,std::atomic_bool* return_busy,const std::function<void()>& blocked,const std::function<bool(bool)>& ejected){
    if(!local.local_endpoint().starts_with("127.0.0.1:")||!local.peer_endpoint().starts_with("127.0.0.1:")||(!importer&&bus.empty())||(importer&&!bus.empty()))throw std::invalid_argument("USB tunnel requires scoped loopback endpoints and an exact exporter identity");
    extrapane::usb::tunnel::Pump pump(importer?std::string{}:bus);
    std::jthread cancel([&](std::stop_token token){while(!token.stop_requested()&&!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(25));if(stop.load()){remote.cancel();local.cancel();}});
    struct Shutdown{security::TlsStream& remote;net::TcpStream& local;bool keep=false;~Shutdown(){if(!keep)remote.cancel();local.cancel();}}shutdown{remote,local};
    bool local_ended=false;
    auto flush=[&]{if(local_ended)return;try{while(!pump.pending().empty()){const auto count=local.write_some(pump.pending());if(count<0)break;if(!count)throw std::runtime_error("Local USB/IP stream closed");pump.consume(static_cast<std::size_t>(count));}}catch(...){if(importer&&ejected&&ejected(true))local_ended=true;else throw;}};
    auto receive=[&]{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);extrapane::usb::tunnel::Header header{};remote.read(header,deadline);auto frame=extrapane::usb::tunnel::decode(header);if(!frame.payload.empty())remote.read(frame.payload,deadline);extrapane::usb::tunnel::validate_control(frame.control,importer);
        const auto event=frame.control;pump.accept(std::move(frame));
        if(event==extrapane::usb::tunnel::Control::return_request&&return_request)*return_request=true;
        if(event==extrapane::usb::tunnel::Control::return_busy&&blocked)blocked();
        };
    std::array<std::uint8_t,extrapane::usb::tunnel::maximum_payload> buffer{};
    auto send=[&]{if(importer&&ejected&&ejected(false))local_ended=true;flush();int count=local_ended?0:-1;if(!local_ended&&pump.read_limit()){try{count=local.read_some(std::span(buffer).first(pump.read_limit()));}catch(...){if(importer&&ejected&&ejected(true)){local_ended=true;count=0;}else throw;}}auto event=extrapane::usb::tunnel::Control::none;
        if(count!=0&&!stop.load()){if(!importer&&return_request&&return_request->exchange(false))event=extrapane::usb::tunnel::Control::return_request;else if(importer&&return_busy&&return_busy->exchange(false))event=extrapane::usb::tunnel::Control::return_busy;}
        auto frame=pump.next(std::span(buffer).first(count>0?static_cast<std::size_t>(count):0),count==0||stop.load(),event);auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);remote.write(extrapane::usb::tunnel::encode(frame),deadline);if(!frame.payload.empty())remote.write(frame.payload,deadline);return frame.payload.empty();};
    while(!stop.load()&&!pump.stopped()){
        if(importer){const bool idle=send();if(pump.stopped())break;receive();flush();if(idle)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        else{receive();if(pump.stopped())break;send();}
    }
    shutdown.keep=keep_tls_open&&!stop.load();
    // jthread joins before borrowed streams can leave their owner's scope. No
    // TLS read/write calls ever overlap; only native-socket shutdown is concurrent.
}
}
