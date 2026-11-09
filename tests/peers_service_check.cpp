#include <extrapane/service.hpp>
#include <atomic>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
using Socket=SOCKET;constexpr auto invalid=INVALID_SOCKET;
void close_socket(Socket s){closesocket(s);}
void nonblock(Socket s){u_long v=1;ioctlsocket(s,FIONBIO,&v);}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
using Socket=int;constexpr auto invalid=-1;
void close_socket(Socket s){close(s);}
void nonblock(Socket s){fcntl(s,F_SETFL,O_NONBLOCK);}
#endif
using namespace extrapane::peers;
using namespace std::chrono_literals;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
sockaddr_in addr(unsigned port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(static_cast<std::uint16_t>(port));return a;}
int main(){auto directory=std::filesystem::temp_directory_path()/("extrapane-service-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(directory);
try{
#ifdef _WIN32
WSADATA w{};check(!WSAStartup(MAKEWORD(2,2),&w),"WSA startup");
#endif
{Service settings({directory/"pairing-settings","Settings test",{},true},{});const auto initial=settings.pairing_status();check(!initial.custom&&initial.code.size()==6&&initial.remaining_seconds==60,"Generated status");settings.set_pairing_password(" permanent pass ! ");check(settings.pairing_status().custom&&settings.pairing_status().code.empty()&&settings.pairing_code().empty(),"Custom secret leaked through status");}
{Service reopened({directory/"pairing-settings","Settings test",{},true},{});check(reopened.pairing_status().custom&&reopened.pairing_code().empty(),"Custom secret was not restored");reopened.set_pairing_password("");check(!reopened.pairing_status().custom&&reopened.pairing_status().remaining_seconds==60,"Generated reset");}
const auto end=std::chrono::steady_clock::now()+15s;
auto waiting=[&]{check(std::chrono::steady_clock::now()<end,"Service test deadline");std::this_thread::sleep_for(1ms);};
Socket dest=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto address=addr(0);check(!bind(dest,reinterpret_cast<sockaddr*>(&address),sizeof(address))&&!listen(dest,4),"fake destination bind");
#ifdef _WIN32
int length=sizeof(address);
#else
socklen_t length=sizeof(address);
#endif
check(!getsockname(dest,reinterpret_cast<sockaddr*>(&address),&length),"fake destination port");nonblock(dest);
std::atomic<bool> failNative=false;Service::Context nativeContext;
std::atomic<unsigned> starts=0,stops=0;Socket source=invalid;
Service::Hooks hostHooks;
hostHooks.prepare=[](const auto& context,auto&){check(context.incoming,"provider direction");return Service::Endpoint{};};
hostHooks.ready=[&](const auto& context,const auto&,auto port){nativeContext=context;source=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto a=addr(port);check(!::connect(source,reinterpret_cast<sockaddr*>(&a),sizeof(a)),"fake source connect");const char payload[]="authorized native bytes";check(send(source,payload,sizeof(payload),0)==sizeof(payload),"fake source send");++starts;};
hostHooks.stopped=[&](const auto&){if(source!=invalid){close_socket(source);source=invalid;}++stops;};
hostHooks.poll=[&]{if(failNative.exchange(false)){auto context=nativeContext;context.failure_detail="USB protocol version mismatch\nEPCT1:secret ACCESS_CODE 123456";return std::vector<Service::Context>{context};}return std::vector<Service::Context>{};};
Service::Hooks clientHooks;
clientHooks.prepare=[&](const auto& context,auto&){check(!context.incoming,"consumer direction");return Service::Endpoint{Service::Role::connect_local,ntohs(address.sin_port)};};
clientHooks.ready=[](const auto&,const auto&,auto){};
{
Service host({directory/"host","Fake host",{{"camera","Fake camera",Kind::camera,true,false,""}},true},hostHooks);
Service client({directory/"client","Fake client",{},true},clientHooks);
auto originalCode=host.pairing_code();host.start("127.0.0.1",0);std::uint16_t port=0;
while(!port){for(auto& event:host.poll())if(event.kind==Service::EventKind::listening)port=event.port;waiting();}
auto connect=[&](std::string code){auto id=client.connect({"localhost",port,"127.0.0.1",host.identity(),std::move(code),false});bool connected=false;while(!connected){for(auto& e:client.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected&&e.context.connection==id)connected=true;}host.poll();waiting();}return id;};
auto id=connect(originalCode);auto catalog=client.catalog(id);bool catalogSeen=false;
while(!catalogSeen){for(auto& e:client.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::catalog&&e.context.request==catalog){check(e.catalogs.size()==1&&e.catalogs[0].offers.size()==1,"authenticated catalog");catalogSeen=true;}}waiting();}
auto request=client.open(id,{host.identity(),"camera",false});Service::Token grant=0;Socket received=invalid;
while(!grant||received==invalid){for(auto& e:client.poll()){if(e.kind==Service::EventKind::error||e.kind==Service::EventKind::disconnected)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::granted&&e.context.request==request)grant=e.context.grant;}if(received==invalid)received=accept(dest,nullptr,nullptr);host.poll();waiting();}
nonblock(received);std::string actual;while(actual.size()<24){char buffer[64];int n=recv(received,buffer,sizeof(buffer),0);if(n>0)actual.append(buffer,n);waiting();}check(actual==std::string("authorized native bytes",24),"native media delivery");
host.set_pairing_password("New configured password !");check(host.pairing_status().custom&&stops==0,"Password change stopped active media");host.set_pairing_password("");originalCode=host.pairing_code();
// Authenticated hotplug hint causes an immediate catalog pull without a user
// refresh request, while the existing camera grant remains alive.
host.offers({{"camera","Fake camera",Kind::camera,true,false,""},{"mic","New microphone",Kind::microphone,true,false,""}});
const auto hotplugDeadline=std::chrono::steady_clock::now()+2s;bool added=false;
while(!added){for(auto& e:client.poll()){if(e.kind==Service::EventKind::error||e.kind==Service::EventKind::disconnected)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::catalog&&e.context.connection==id&&e.catalogs[0].offers.size()==2)added=true;}check(std::chrono::steady_clock::now()<hotplugDeadline,"Catalog addition was not pushed promptly");waiting();}
check(stops==0&&starts==1,"Sibling hotplug interrupted active grant");
host.offers({{"camera","Fake camera",Kind::camera,true,false,""}});bool removed=false;
while(!removed){for(auto& e:client.poll())if(e.kind==Service::EventKind::catalog&&e.context.connection==id&&e.catalogs[0].offers.size()==1)removed=true;check(std::chrono::steady_clock::now()<hotplugDeadline,"Catalog removal was not pushed promptly");waiting();}
check(stops==0,"Sibling removal interrupted active grant");
// Listener changes must not stop a granted native stream, including a
// same-port wildcard/specific transition and an occupied-port failure.
auto rebound=host.rebind("0.0.0.0",0);check(rebound.error.empty()&&rebound.port,"Wildcard rebind failed");port=rebound.port;
rebound=host.rebind("127.0.0.1",port);check(rebound.error.empty()&&rebound.port==port,"Same-port specific rebind failed");
auto failed=host.rebind("127.0.0.1",ntohs(address.sin_port));check(!failed.error.empty()&&failed.port==port&&failed.address=="127.0.0.1","Failed rebind lost original listener");
// An independent loopback interface blocks wildcard rebinding after the old
// listener was closed. Rollback must restore its exact endpoint and grants.
Socket conflict=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto conflictAddress=addr(port);conflictAddress.sin_addr.s_addr=htonl(0x7f000002);
check(!bind(conflict,reinterpret_cast<sockaddr*>(&conflictAddress),sizeof(conflictAddress))&&!listen(conflict,1),"Rollback reservation failed");
failed=host.rebind("0.0.0.0",port);check(!failed.error.empty()&&failed.port==port&&failed.address=="127.0.0.1","Overlap failure did not restore listener");close_socket(conflict);
check(stops==0&&starts==1,"Listener rebind stopped active resource");
check(send(source,"live",4,0)==4,"Send after rebind failed");actual.clear();
while(actual.size()<4){char buffer[4];int n=recv(received,buffer,4,0);if(n>0)actual.append(buffer,n);waiting();}check(actual=="live","Rebind lost native media delivery");
failNative=true;bool failureSeen=false;while(!failureSeen){for(const auto& e:client.poll())if(e.kind==Service::EventKind::error&&e.detail=="Native component failed (credential-bearing diagnostic withheld)")failureSeen=true;waiting();}
client.close(id,grant);while(stops==0){client.poll();host.poll();waiting();}check(starts==1,"one approved adapter start");close_socket(received);
std::this_thread::sleep_for(50ms);client.poll();bool disconnected=false;
id=connect({});check(host.pairing_code()==originalCode,"pairing code changed on reconnect");
client.disconnect(id);disconnected=false;while(!disconnected){for(auto& e:client.poll())if(e.kind==Service::EventKind::disconnected&&e.context.connection==id)disconnected=true;host.poll();waiting();}
// Three authenticated services: an opt-in gateway delegates only its paired
// direct provider, forwarding bytes without a native adapter at the gateway.
Service gateway({directory/"gateway","Fake gateway",{},true},{});
gateway.start("127.0.0.1",0);std::uint16_t gatewayPort=0;
while(!gatewayPort){for(auto& e:gateway.poll())if(e.kind==Service::EventKind::listening)gatewayPort=e.port;waiting();}
auto providerLink=gateway.connect({"localhost",port,"127.0.0.1",host.identity(),originalCode,false});bool providerConnected=false;
while(!providerConnected){for(auto& e:gateway.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected&&e.context.connection==providerLink)providerConnected=true;}waiting();}
auto visitorLink=client.connect({"localhost",gatewayPort,"127.0.0.1",gateway.identity(),gateway.pairing_code(),false});bool visitorConnected=false;
while(!visitorConnected){for(auto& e:client.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected&&e.context.connection==visitorLink)visitorConnected=true;}waiting();}
auto before=client.catalog(visitorLink);bool checked=false;
while(!checked){for(auto& e:client.poll())if(e.kind==Service::EventKind::catalog&&e.context.request==before){check(e.catalogs.size()==1,"gateway must be off by default");checked=true;}waiting();}
gateway.disconnect(providerLink);bool providerGone=false;
while(!providerGone){for(auto& e:gateway.poll())if(e.kind==Service::EventKind::disconnected&&e.context.connection==providerLink)providerGone=true;waiting();}
gateway.gateway(true);providerLink=gateway.connect({"localhost",port,"127.0.0.1",host.identity(),{},false});bool providerCatalog=false;
while(!providerCatalog){for(auto& e:gateway.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected&&e.context.peer==host.identity())providerLink=e.context.connection;if(e.kind==Service::EventKind::catalog&&e.context.connection==providerLink)providerCatalog=true;}waiting();}
auto after=client.catalog(visitorLink);checked=false;
while(!checked){for(auto& e:client.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::catalog&&e.context.request==after){check(e.catalogs.size()==2,"direct paired provider absent from gateway");checked=true;}}waiting();}
auto endpointLink=client.connect_via(visitorLink,host.identity());bool endpointCatalog=false;
while(!endpointCatalog){for(auto& e:client.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::catalog&&e.context.connection==endpointLink)endpointCatalog=true;}waiting();}
auto relayRequest=client.open(endpointLink,{host.identity(),"camera",false});Service::Token relayGrant=0;received=invalid;
while(!relayGrant||received==invalid){for(auto& e:client.poll()){if(e.kind==Service::EventKind::error||e.kind==Service::EventKind::disconnected)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::granted&&e.context.request==relayRequest)relayGrant=e.context.grant;}for(auto& e:gateway.poll())if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(received==invalid)received=accept(dest,nullptr,nullptr);waiting();}
nonblock(received);actual.clear();while(actual.size()<24){char buffer[64];int n=recv(received,buffer,sizeof(buffer),0);if(n>0)actual.append(buffer,n);waiting();}check(actual==std::string("authorized native bytes",24),"delegated native media delivery");
client.start("127.0.0.1",0);bool localListening=false;
while(!localListening){for(auto& e:client.poll())if(e.kind==Service::EventKind::listening)localListening=true;waiting();}
client.stop();check(send(source,"live",4,0)==4,"source remains active after consumer stops sharing");actual.clear();
while(actual.size()<4){char buffer[4];int n=recv(received,buffer,4,0);if(n>0)actual.append(buffer,n);waiting();}check(actual=="live","stop sharing incorrectly killed outgoing resource");
gateway.gateway(false);while(stops<2){gateway.poll();client.poll();host.poll();waiting();}check(starts==2,"gateway started unexpected local adapter");close_socket(received);
gateway.stop();
host.stop();check(host.pairing_code()==originalCode,"pairing code changed on stop");
}
close_socket(dest);std::filesystem::remove_all(directory);std::cout<<"Authenticated service orchestration passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ec;std::filesystem::remove_all(directory,ec);return 1;}}
