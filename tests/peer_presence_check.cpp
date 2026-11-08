#include <extrapane/service.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <future>
#include <thread>
using namespace extrapane::peers;
using namespace std::chrono_literals;
void require(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void legacy_cancel(const std::filesystem::path& path){
 auto encoded=rpc::encode({rpc::Operation::cancel_request,91,{}});rpc::Decoder decoder;auto decoded=decoder.feed(encoded);require(decoded.size()==1&&decoded[0].operation==rpc::Operation::cancel_request,"Cancel opcode codec mismatch");bool rejected=false;try{rpc::encode({rpc::Operation::cancel_request,91,{1}});}catch(...){rejected=true;}require(rejected,"Cancel payload not bounded");
 const auto identity=dm::peer_auth::random_identity();dm::peer_auth::Policy auth(identity);transport::Listener listener("127.0.0.1",0);std::atomic<bool> opened=false,cancelled=false,closed=false,echoed=false;
 auto deadline=[](){return std::chrono::steady_clock::now()+5s;};
 auto remote=std::async(std::launch::async,[&]{
  auto peer=AuthenticatedConnection::server(listener.accept(deadline()),auth,[](const DeviceId&)->std::optional<dm::peer_auth::Secret>{return {};},[](const DeviceId&,const dm::peer_auth::Secret&){},deadline());
  for(;;){auto message=peer.receive(deadline());require(message.operation!=rpc::Operation::cancel_request,"New cancellation opcode sent to old peer");
   if(message.operation==rpc::Operation::ping){if(message.id==rpc::cancel_probe)echoed=true;peer.send({rpc::Operation::pong,message.id,{}},deadline());}
   else if(message.operation==rpc::Operation::catalog)peer.send({rpc::Operation::catalog_reply,message.id,rpc::encode_catalogs({Catalog{identity,1,1,"Legacy peer",{{"camera","Legacy camera",Kind::camera,true}}}})},deadline());
   else if(message.operation==rpc::Operation::open){opened=true;const auto until=deadline();while(!cancelled){require(std::chrono::steady_clock::now()<until,"Cancel not requested");std::this_thread::sleep_for(1ms);}peer.send({rpc::Operation::granted,message.id,rpc::encode(rpc::Granted{901,{}})},deadline());}
   else if(message.operation==rpc::Operation::close){require(message.id==901,"Legacy fallback closed wrong grant");closed=true;peer.send({rpc::Operation::stopped,901,{}},deadline());return;}
  }
 });
 Service client({path/"legacy-client","Legacy cancellation check",{},true},{});auto connection=client.connect({"127.0.0.1",listener.port(),{},identity,auth.code(),false});bool catalog=false;Service::Token request=0;const auto until=deadline();
 while(!closed){require(std::chrono::steady_clock::now()<until,"Legacy cancellation deadline");for(const auto& e:client.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected)client.catalog(connection);if(e.kind==Service::EventKind::catalog)catalog=true;if(e.kind==Service::EventKind::stopped&&e.context.request==request)cancelled=true;}
  if(catalog&&!request)request=client.open(connection,{identity,"camera"});if(opened&&!cancelled)client.cancel(connection,request);std::this_thread::sleep_for(2ms);
 }remote.get();require(echoed&&cancelled&&closed,"Legacy peer fallback not exercised");
}
int main(){auto path=std::filesystem::temp_directory_path()/("extrapane-presence-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);
std::atomic<bool> release=false,entered=false;std::atomic<unsigned> preparations=0;
try{
 legacy_cancel(path);
 auto wait=[](auto predicate){const auto deadline=std::chrono::steady_clock::now()+5s;while(!predicate()){require(std::chrono::steady_clock::now()<deadline,"Presence check timed out");std::this_thread::sleep_for(2ms);}};
 Service::Hooks providerHooks;providerHooks.prepare=[&](const auto&,auto&)->Service::Endpoint{++preparations;throw std::runtime_error("Synthetic camera setup failure");};
 providerHooks.ready=[](const auto&,const auto&,auto){};
 Service provider({path/"provider","Provider",{{"camera","Camera",Kind::camera,true}},true},providerHooks);
 provider.start("127.0.0.1",0);std::uint16_t port=0;wait([&]{for(const auto& e:provider.poll())if(e.kind==Service::EventKind::listening)port=e.port;return port!=0;});
 {
  Service initial({path/"client","Client",{},true},{});auto id=initial.connect({"127.0.0.1",port,{},provider.identity(),provider.pairing_code(),false});bool paired=false;
  wait([&]{for(const auto& e:initial.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected&&e.context.connection==id)paired=true;}return paired;});initial.disconnect(id);
 }
 {TrustStore store(path/"client");for(auto device:store.devices()){device.address_hint.clear();device.port_hint=0;store.update_hint(device);}}
 Service::Hooks hooks;hooks.prepare_request=[&](const auto&,auto&){entered=true;const auto deadline=std::chrono::steady_clock::now()+5s;while(!release&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);};
 {
 provider.stop();
 Service client({path/"client","Client",{},true},hooks);client.auto_connect(true);client.observe_route(provider.identity(),"127.0.0.1",port);Service::Token link=0;bool catalog=false;
 std::this_thread::sleep_for(300ms);for(const auto& e:client.poll())require(e.kind!=Service::EventKind::error,"Offline automatic presence spammed an error");provider.start("127.0.0.1",port);
 wait([&]{for(const auto& e:client.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected)link=e.context.connection;if(e.kind==Service::EventKind::catalog)catalog=true;}return link&&catalog;});require(preparations==0,"Catalog autoconnection captured media");
 auto first=client.open(link,{provider.identity(),"camera"});wait([&]{return entered.load();});
 auto query=client.catalog(link);bool replied=false;wait([&]{for(const auto& e:client.poll()){require(e.kind!=Service::EventKind::disconnected,"Slow first preparation dropped paired device");if(e.kind==Service::EventKind::catalog&&e.context.request==query)replied=true;}return replied;});
 client.cancel(link,first);release=true;bool cancelled=false;wait([&]{for(const auto& e:client.poll()){require(e.kind!=Service::EventKind::disconnected,"First resource cancellation dropped device");if(e.kind==Service::EventKind::stopped&&e.context.request==first)cancelled=true;}return cancelled;});require(preparations==0,"Cancelled local preparation opened remote media");
 auto failed=client.open(link,{provider.identity(),"camera"});bool failure=false;wait([&]{for(const auto& e:client.poll()){require(e.kind!=Service::EventKind::disconnected,"Failed first camera dropped paired device");if(e.kind==Service::EventKind::error&&e.context.request==failed){std::cout<<"expected setup failure: "<<e.detail<<" preparations="<<preparations.load()<<"\n";failure=true;}}return failure;});
 query=client.catalog(link);replied=false;wait([&]{for(const auto& e:client.poll()){require(e.kind!=Service::EventKind::disconnected,"Resource failure destroyed usable catalog");if(e.kind==Service::EventKind::catalog&&e.context.request==query)replied=true;}return replied;});
 client.disconnect(link);bool gone=false;wait([&]{for(const auto& e:client.poll())if(e.kind==Service::EventKind::disconnected&&e.context.connection==link)gone=true;return gone;});
 client.auto_connect(false);client.observe_route(provider.identity(),"127.0.0.2",port);client.auto_connect(true);
 const auto until=std::chrono::steady_clock::now()+400ms;while(std::chrono::steady_clock::now()<until){for(const auto& e:client.poll())require(e.kind!=Service::EventKind::connected,"Explicit disconnect was automatically undone");std::this_thread::sleep_for(2ms);}
 require(client.remembered().front().address_hint=="127.0.0.1","Unverified hint overwrote trusted route");
 client.observe_route(provider.identity(),"127.0.0.1",port);auto explicitLink=client.connect({"127.0.0.1",port,{},provider.identity(),{},false});bool connected=false;wait([&]{for(const auto& e:client.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected&&e.context.connection==explicitLink)connected=true;}return connected;});
 if(preparations!=1)std::cerr<<"preparations="<<preparations.load()<<"\n";require(preparations==1,"Control reconnect replayed media");client.disconnect(explicitLink);
 }
 provider.stop();std::cout<<"Paired catalog presence, slow/failed/cancelled first camera isolation, explicit suppression and untrusted route preservation passed\n";
}catch(const std::exception& e){release=true;std::cerr<<e.what()<<'\n';return 1;}
std::filesystem::remove_all(path);return 0;}
