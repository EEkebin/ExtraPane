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
int main(){auto directory=std::filesystem::temp_directory_path()/("extrapane-reverse-relay-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(directory);
try{
#ifdef _WIN32
WSADATA w{};check(!WSAStartup(MAKEWORD(2,2),&w),"WSA startup");
#endif
const auto end=std::chrono::steady_clock::now()+20s;
auto wait=[&]{check(std::chrono::steady_clock::now()<end,"Reverse relay deadline");std::this_thread::sleep_for(1ms);};
Socket listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto address=addr(0);check(!bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))&&!listen(listener,4),"fake capture listen");
#ifdef _WIN32
int length=sizeof(address);
#else
socklen_t length=sizeof(address);
#endif
check(!getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length),"capture endpoint port");nonblock(listener);
std::atomic<Socket> clientSocket{invalid};std::atomic<unsigned> ended{0};
Service::Hooks providerHooks;
providerHooks.prepare=[&](const auto& context,auto& request){check(context.incoming&&context.kind==Kind::existing_screen,"capture owner direction");request.native_credential="EPCT1.reverse-ticket";return Service::Endpoint{Service::Role::connect_local,ntohs(address.sin_port)};};
providerHooks.ready=[](const auto&,const auto&,auto){};providerHooks.stopped=[&](const auto&){++ended;};
Service::Hooks viewerHooks;
viewerHooks.prepare=[](const auto& context,auto&){check(!context.incoming&&context.kind==Kind::existing_screen,"viewer direction");return Service::Endpoint{};};
viewerHooks.ready=[&](const auto&,const auto& request,auto port){check(request.native_credential=="EPCT1.reverse-ticket","reverse ticket lost at gateway");auto socketValue=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto target=addr(port);check(!::connect(socketValue,reinterpret_cast<sockaddr*>(&target),sizeof(target)),"viewer loopback connect");clientSocket=socketValue;check(send(socketValue,"screen request",14,0)==14,"viewer request send");nonblock(socketValue);};
viewerHooks.stopped=[&](const auto&){auto socketValue=clientSocket.exchange(invalid);if(socketValue!=invalid)close_socket(socketValue);};
{
Service provider({directory/"provider","Capture device",{{"screen","Existing screen",Kind::existing_screen,true,false,""}},true},providerHooks);
Service gateway({directory/"gateway","Gateway",{},true},{});
Service viewer({directory/"viewer","Viewer",{},true},viewerHooks);
auto start=[&](Service& service){service.start("127.0.0.1",0);for(;;){for(auto& e:service.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::listening)return e.port;}wait();}};
const auto providerPort=start(provider),gatewayPort=start(gateway);
auto connect=[&](Service& from,Service& to,unsigned port){const auto token=from.connect({"localhost",static_cast<std::uint16_t>(port),"",to.identity(),to.pairing_code(),false});for(;;){for(auto& e:from.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected&&e.context.connection==token)return token;}wait();}};
connect(gateway,provider,providerPort);const auto link=connect(viewer,gateway,gatewayPort);gateway.gateway(true);
bool catalog=false;while(!catalog){for(auto& e:gateway.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::catalog)catalog=true;}wait();}
viewer.catalog(link);catalog=false;while(!catalog){for(auto& e:viewer.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::catalog){check(e.catalogs.size()==2,"missing delegated screen");catalog=true;}}wait();}
viewer.open(link,{provider.identity(),"screen",false});Socket captured=invalid;
while(captured==invalid){for(auto& e:viewer.poll())if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);for(auto& e:gateway.poll())if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);captured=accept(listener,nullptr,nullptr);wait();}nonblock(captured);
std::string request;while(request.size()<14){char buffer[32];auto count=recv(captured,buffer,sizeof(buffer),0);if(count>0)request.append(buffer,count);wait();}check(request=="screen request","reverse request delivery");
check(send(captured,"captured video",14,0)==14,"capture response send");std::string video;while(video.size()<14){char buffer[32];auto count=recv(clientSocket.load(),buffer,sizeof(buffer),0);if(count>0)video.append(buffer,count);wait();}check(video=="captured video","reverse capture delivery");
gateway.forget(provider.identity());while(!ended){viewer.poll();gateway.poll();provider.poll();wait();}close_socket(captured);
}
close_socket(listener);std::filesystem::remove_all(directory);std::cout<<"Reverse existing-screen gateway media and revocation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ec;std::filesystem::remove_all(directory,ec);return 1;}}
