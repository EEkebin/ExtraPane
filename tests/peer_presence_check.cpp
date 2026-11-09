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
 Service client({path/"legacy-client","Legacy cancellation check",{},true},{});auto connection=client.connect({"127.0.0.1",listener.port(),{},identity,auth.code(),false});bool catalog=false;Service::Token request=0,unsupported=0;bool unsupportedDenied=false;const auto until=deadline();
 while(!closed){require(std::chrono::steady_clock::now()<until,"Legacy cancellation deadline");for(const auto& e:client.poll()){if(e.kind==Service::EventKind::error){if(e.context.connection==unsupported)unsupportedDenied=true;else throw std::runtime_error(e.detail);}if(e.kind==Service::EventKind::connected)client.catalog(connection);if(e.kind==Service::EventKind::catalog)catalog=true;if(e.kind==Service::EventKind::stopped&&e.context.request==request)cancelled=true;}
  if(catalog&&!unsupported)unsupported=client.connect_via(connection,dm::peer_auth::random_identity(),"123456");if(catalog&&unsupportedDenied&&!request)request=client.open(connection,{identity,"camera"});if(opened&&!cancelled)client.cancel(connection,request);std::this_thread::sleep_for(2ms);
 }remote.get();require(echoed&&cancelled&&closed&&unsupportedDenied,"Legacy peer fallback not exercised");
}
void open_admission_race(const std::filesystem::path& path){
 for(unsigned scenario=0;scenario<3;++scenario){
  const auto directory=path/("open-race-"+std::to_string(scenario));TrustStore store(directory);const auto remoteId=dm::peer_auth::random_identity();transport::Listener listener("127.0.0.1",0);std::atomic<bool> entered=false,release=false;
  auto deadline=[] {return std::chrono::steady_clock::now()+5s;};
  auto server=std::async(std::launch::async,[&]{auto stream=listener.accept(deadline());std::array<std::uint8_t,72> request{};stream.read(request,deadline());require(request[5]==0,"Race did not begin as open handshake");entered=true;const auto until=deadline();while(!release){require(std::chrono::steady_clock::now()<until,"Race gate timeout");std::this_thread::sleep_for(1ms);}auto response=request;response[3]='S';std::copy(remoteId.begin(),remoteId.end(),response.begin()+8);std::copy(store.identity().begin(),store.identity().end(),response.begin()+40);stream.write(response,deadline());const std::array<std::uint8_t,1> ack{1};stream.write(ack,deadline());std::this_thread::sleep_for(100ms);});
  Service client({directory,"Race client",{},true},{});const auto connection=client.connect({"127.0.0.1",listener.port(),{},remoteId,{},true});const auto until=deadline();while(!entered){require(std::chrono::steady_clock::now()<until,"Race server not entered");std::this_thread::sleep_for(1ms);}
  PairSecret secret{};secret[0]=94;const auto pairedId=scenario==2?dm::peer_auth::random_identity():remoteId;store.remember({pairedId,"Concurrent pairing","127.0.0.1",listener.port()},secret);if(scenario==1)store.begin_forget(pairedId);release=true;
  bool rejected=false;while(!rejected){require(std::chrono::steady_clock::now()<until,"Unpaired admission was not rejected");for(const auto& event:client.poll())if(event.context.connection==connection){require(event.kind!=Service::EventKind::connected,"In-flight open handshake bypassed new trust");if(event.kind==Service::EventKind::error)rejected=true;}std::this_thread::sleep_for(1ms);}server.get();
 }
}
void no_pairing_downgrade(const std::filesystem::path& path){
 const auto a=path/"downgrade-a",b=path/"downgrade-b";DeviceId aid,bid;PairSecret key{};key[0]=89;
 {TrustStore left(a),right(b);aid=left.identity();bid=right.identity();left.remember({bid,"B"},key);right.remember({aid,"A"},key);}
 auto wait=[](auto predicate){const auto until=std::chrono::steady_clock::now()+5s;while(!predicate()){require(std::chrono::steady_clock::now()<until,"Pairing downgrade deadline");std::this_thread::sleep_for(2ms);}};
 Service provider({b,"B",{},true},{});provider.start("127.0.0.1",0);std::uint16_t port=0;wait([&]{for(const auto& e:provider.poll())if(e.kind==Service::EventKind::listening)port=e.port;return port!=0;});
 {TrustStore left(a);left.update_hint({bid,"B","127.0.0.1",port});}
 Service client({a,"A",{},true},{});
 auto dial=[&](DeviceId identity,bool shouldConnect,const char* address="127.0.0.1"){
  const auto token=client.connect({address,port,{},identity,{},true});bool complete=false;wait([&]{for(const auto& e:client.poll())if(e.context.connection==token){if(e.kind==Service::EventKind::connected){require(shouldConnect&&e.verified,"Saved device downgraded to open access");complete=true;}if(e.kind==Service::EventKind::error){require(!shouldConnect,"Saved proof unexpectedly failed");complete=true;}}return complete;});
 };
 dial(bid,true);dial({},true,"LOCALHOST");
 const auto unrelated=dm::peer_auth::random_identity();{TrustStore left(a);PairSecret other{};other[0]=90;left.remember({unrelated,"Other","127.0.0.1",port},other);}
 dial({},false);dial(bid,true);
 {TrustStore left(a);left.begin_forget(bid);}dial(bid,false);dial({},false);
 provider.pairing_required(false);Service unknown({path/"downgrade-unknown","Unknown",{},true},{});const auto token=unknown.connect({"127.0.0.1",port,{},bid,{},true});bool open=false;
 wait([&]{for(const auto& e:unknown.poll())if(e.context.connection==token){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected){require(!e.verified,"Unknown open connection unexpectedly paired");open=true;}}return open;});
}
void changed_paired_route(const std::filesystem::path& path){
 const auto a=path/"route-a",b=path/"route-b";DeviceId aid,bid;PairSecret key{};key[0]=71;
 {TrustStore left(a),right(b);aid=left.identity();bid=right.identity();left.remember({bid,"B"},key);right.remember({aid,"A"},key);}
 transport::Listener stale("127.0.0.1",0);std::atomic<unsigned> attempts=0;std::atomic<bool> done=false;
 auto stalePeer=std::async(std::launch::async,[&]{while(!done){try{auto stream=stale.accept(std::chrono::steady_clock::now()+200ms);std::array<std::uint8_t,72> request{};stream.read(request,std::chrono::steady_clock::now()+1s);auto response=request;response[3]='S';DeviceId wrong=bid;wrong[0]^=1;std::copy(wrong.begin(),wrong.end(),response.begin()+8);std::copy(aid.begin(),aid.end(),response.begin()+40);stream.write(response,std::chrono::steady_clock::now()+1s);++attempts;}catch(...){}}});
 try{
 {TrustStore left(a);left.update_hint({bid,"B","127.0.0.1",stale.port()});}
 Service real({b,"B",{},true},{});real.start("127.0.0.1",0);std::uint16_t port=0;const auto until=std::chrono::steady_clock::now()+70s;
 while(!port){require(std::chrono::steady_clock::now()<until,"Real route missing");for(const auto& e:real.poll())if(e.kind==Service::EventKind::listening)port=e.port;std::this_thread::sleep_for(2ms);}
 Service client({a,"A",{},true},{});client.auto_connect(true);
 while(!attempts){require(std::chrono::steady_clock::now()<until,"Stale route not attempted");std::this_thread::sleep_for(2ms);}
 std::this_thread::sleep_for(100ms);client.observe_route(bid,"127.0.0.1",port);bool catalog=false;
 while(!catalog){require(std::chrono::steady_clock::now()<until,"Changed paired route stayed latched");for(const auto& e:client.poll()){require(e.kind!=Service::EventKind::error,"Automatic route failure spammed UI");if(e.kind==Service::EventKind::catalog)catalog=true;}std::this_thread::sleep_for(10ms);}
 require(attempts==1,"Failed stale endpoint retried without a new route");require(client.remembered().front().address_hint=="127.0.0.1"&&client.remembered().front().port_hint==port,"Authenticated changed route not persisted");
 }catch(...){done=true;stalePeer.wait();throw;}done=true;stalePeer.get();
}
void duplicate_screens(const std::filesystem::path& path){
 auto wait=[](auto predicate){const auto until=std::chrono::steady_clock::now()+5s;while(!predicate()){require(std::chrono::steady_clock::now()<until,"Screen duplicate deadline");std::this_thread::sleep_for(2ms);}};
 std::atomic<bool> entered=false,release=false;Service::Hooks hooks;
 hooks.prepare_request=[&](const auto&,auto&){entered=true;const auto until=std::chrono::steady_clock::now()+5s;while(!release&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);};
 Service provider({path/"screen-provider","Screen",{{"screen","Screen",Kind::existing_screen,true},{"virtual","Virtual",Kind::display_destination,true}},true},{});
 provider.start("127.0.0.1",0);std::uint16_t port=0;wait([&]{for(const auto& e:provider.poll())if(e.kind==Service::EventKind::listening)port=e.port;return port!=0;});
 Service client({path/"screen-client","Client",{},true},hooks);
 auto connect=[&](std::string code){auto token=client.connect({"127.0.0.1",port,{},provider.identity(),code,false});bool catalog=false;wait([&]{for(const auto& e:client.poll()){if(e.kind==Service::EventKind::connected&&e.context.connection==token)client.catalog(token);if(e.kind==Service::EventKind::catalog&&e.context.connection==token)catalog=true;if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);}return catalog;});return token;};
 const auto one=connect(provider.pairing_code()),two=connect({});const auto first=client.open(one,{provider.identity(),"screen"});wait([&]{return entered.load();});
 const auto duplicate=client.open(two,{provider.identity(),"screen"});bool rejected=false;wait([&]{for(const auto& e:client.poll()){if(e.kind==Service::EventKind::error&&e.context.request==duplicate)rejected=true;require(e.kind!=Service::EventKind::disconnected,"Duplicate screen dropped control link");}return rejected;});
 const auto v1=client.open(one,{provider.identity(),"virtual",false,1920,1080,60}),v2=client.open(two,{provider.identity(),"virtual",false,2560,1440,120});
 client.cancel(one,first);client.cancel(one,v1);client.cancel(two,v2);release=true;
 unsigned stopped=0;wait([&]{for(const auto& e:client.poll()){require(e.kind!=Service::EventKind::error,"Repeated virtual destination rejected");if(e.kind==Service::EventKind::stopped&&(e.context.request==first||e.context.request==v1||e.context.request==v2))++stopped;}return stopped==3;});
}
void bilateral_forget(const std::filesystem::path& path){
 auto deadline=[] {return std::chrono::steady_clock::now()+3s;};
 auto wait=[](auto predicate){const auto until=std::chrono::steady_clock::now()+5s;while(!predicate()){require(std::chrono::steady_clock::now()<until,"Bilateral forget deadline");std::this_thread::sleep_for(2ms);}};
 const auto a=path/"forget-a",b=path/"forget-b";
 DeviceId aid,bid;PairSecret key{};
 {TrustStore left(a),right(b);aid=left.identity();bid=right.identity();key[0]=42;left.remember({bid,"B"},key);right.remember({aid,"A"},key);}
 std::uint16_t port=0;
 {Service remote({b,"B",{},true},{});remote.start("127.0.0.1",0);wait([&]{for(const auto& e:remote.poll())if(e.kind==Service::EventKind::listening)port=e.port;return port!=0;});
  {TrustStore store(a);store.update_hint({bid,"B","127.0.0.1",port});}
  Service local({a,"A",{},true},{});local.forget(bid);
  wait([&]{return local.remembered().empty()&&remote.remembered().empty()&&local.pending_forgets().empty();});
  require(TrustStore(a).revocation_secret(bid).has_value()&&TrustStore(b).revocation_secret(aid).has_value(),"Lost-ACK tombstone missing");
 }
 // Simulate receiver durable deletion followed by lost ACK; restart both.
 {TrustStore left(a),right(b);PairSecret fresh{};fresh[0]=43;left.remember({bid,"B","127.0.0.1",port},fresh);right.remember({aid,"A"},fresh);left.begin_forget(bid);require(right.receive_forget(aid,fresh),"Receiver forget failed");}
 {Service remote({b,"B",{},true},{});remote.start("127.0.0.1",port);Service local({a,"A",{},true},{});
  wait([&]{return local.pending_forgets().empty();});require(local.remembered().empty()&&remote.remembered().empty(),"ACK retry restored trust");for(const auto& event:remote.poll())require(event.kind!=Service::EventKind::trust_changed,"Tombstone replay mutated ordinary peer state");
 }
 // Tombstone credentials authenticate only the revocation mode, never catalog.
 {Service remote({b,"B",{},true},{});remote.start("127.0.0.1",port);
  dm::peer_auth::ClientOptions proof;proof.local=aid;proof.expected_peer=bid;proof.mode=dm::peer_auth::Mode::remembered;dm::peer_auth::Secret secret;secret.bytes=*TrustStore(a).revocation_secret(bid);proof.remembered=secret;
  bool denied=false;try{auto connection=AuthenticatedConnection::client(transport::Stream::connect("127.0.0.1",port,{},deadline()),proof,{},deadline());}catch(...){denied=true;}require(denied,"Tombstone authenticated ordinary access");
  proof.mode=dm::peer_auth::Mode::revocation;auto connection=AuthenticatedConnection::client(transport::Stream::connect("127.0.0.1",port,{},deadline()),proof,{},deadline());
  denied=false;try{connection.send({rpc::Operation::catalog,7,{}},deadline());connection.receive(deadline());}catch(...){denied=true;}require(denied,"Revocation channel exposed catalog");
 }
}
void mutual_resources_without_discovery(const std::filesystem::path& path){
 Service accepting({path/"mutual-accepting","Laptop",{{"camera","Laptop camera",Kind::camera,true}},true},{});
 Service initiating({path/"mutual-initiating","Desktop",{{"speaker","Desktop speakers",Kind::speaker,true}},true},{});
 accepting.auto_connect(true);initiating.auto_connect(true);accepting.start("127.0.0.1",0);initiating.start("127.0.0.1",0);
 const auto until=std::chrono::steady_clock::now()+12s;auto wait=[&]{require(std::chrono::steady_clock::now()<until,"Mutual resources unavailable without discovery");std::this_thread::sleep_for(2ms);};
 std::uint16_t port=0;while(!port){for(const auto& e:accepting.poll())if(e.kind==Service::EventKind::listening)port=e.port;wait();}
 initiating.connect({"127.0.0.1",port,{},accepting.identity(),accepting.pairing_code(),false});bool forward=false,reverse=false;
 while(!forward||!reverse){
  for(const auto& e:initiating.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::catalog&&e.context.peer==accepting.identity())for(const auto& catalog:e.catalogs)for(const auto& offer:catalog.offers)if(offer.id=="camera")forward=true;}
  for(const auto& e:accepting.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::catalog&&e.context.peer==initiating.identity()){require(!e.context.incoming,"Reverse catalog not usable for resource requests");for(const auto& catalog:e.catalogs)for(const auto& offer:catalog.offers)if(offer.id=="speaker")reverse=true;}}
  wait();
 }
 require(accepting.remembered().size()==1&&initiating.remembered().size()==1,"Mutual availability created extra pairing");
 require(accepting.remembered()[0].address_hint.empty()&&accepting.remembered()[0].port_hint==0,"Reverse tunnel invented a remote listening endpoint");
}
int main(int argc,char** argv){auto path=std::filesystem::temp_directory_path()/("extrapane-presence-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);
std::atomic<bool> release=false,entered=false;std::atomic<unsigned> preparations=0;
try{
 if(argc>1&&std::string(argv[1])=="--mutual"){mutual_resources_without_discovery(path);std::filesystem::remove_all(path);return 0;}
 if(argc>1&&std::string(argv[1])=="--changed-route"){changed_paired_route(path);std::filesystem::remove_all(path);return 0;}
 legacy_cancel(path);bilateral_forget(path);duplicate_screens(path);no_pairing_downgrade(path);open_admission_race(path);
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
