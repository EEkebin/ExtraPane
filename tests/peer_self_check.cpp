#include <extrapane/service.hpp>
#include <atomic>
#include <iostream>
#include <thread>
using namespace extrapane::peers;
using namespace std::chrono_literals;
int main(){
    const auto directory=std::filesystem::temp_directory_path()/("extrapane-self-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        std::atomic<unsigned> ready=0,stopped=0;
        Service::Hooks hooks;
        hooks.prepare=[](const auto&,auto&){return Service::Endpoint{};};
        hooks.ready=[&](const auto&,const auto&,auto){++ready;};
        hooks.stopped=[&](const auto&){++stopped;};
        {
            Service service({directory,"Self",{{"display","Local virtual display",Kind::display_destination,true,true,""}},true},hooks);
            // Gateway catalog refresh must not treat our own loopback route as
            // a separately trusted downstream device.
            service.gateway(true);
            service.start("127.0.0.1",0);
            unsigned port=0;Service::Token connection=0,grant=0;
            bool connected=false,catalog=false,closed=false,opened=false;
            const auto deadline=std::chrono::steady_clock::now()+10s;
            while(!closed){
                if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Self-connection deadline");
                for(auto& event:service.poll()){
                    if(event.kind==Service::EventKind::error)throw std::runtime_error(event.detail);
                    if(event.kind==Service::EventKind::listening)port=event.port;
                    if(event.context.connection!=connection)continue;
                    if(event.kind==Service::EventKind::connected){
                        if(!event.verified)throw std::runtime_error("Self connection bypassed pairing");
                        connected=true;service.catalog(connection);
                    }
                    if(event.kind==Service::EventKind::catalog){
                        if(event.catalogs.size()!=1||event.catalogs[0].device!=service.identity())throw std::runtime_error("Wrong self catalog");
                        catalog=true;
                    }
                    if(event.kind==Service::EventKind::granted)grant=event.context.grant;
                    if(event.kind==Service::EventKind::disconnected){if(!grant)throw std::runtime_error("Premature self disconnect");closed=true;}
                }
                if(port&&!connection)connection=service.connect({"localhost",static_cast<std::uint16_t>(port),"",service.identity(),service.pairing_code(),false});
                if(connected&&catalog&&!opened){service.open(connection,{service.identity(),"display",true,1920,1080,60});opened=true;}
                if(grant&&ready==2&&!stopped){service.close(connection,grant);service.gateway(false);}
                if(stopped==2&&!closed)service.disconnect(connection);
                std::this_thread::sleep_for(2ms);
            }
            if(!service.remembered().empty())throw std::runtime_error("Self trust was persisted");
        }
        if(ready!=2||stopped!=2)throw std::runtime_error("Both self resource endpoints must release");
        std::filesystem::remove_all(directory);
        std::cout<<"Self connection: authenticated localhost, catalog, dual endpoint lifecycle and no self trust passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
