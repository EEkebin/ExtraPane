#include <extrapane/service.hpp>
#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
using Socket=SOCKET;constexpr auto invalid=INVALID_SOCKET;
void close_socket(Socket s){closesocket(s);}void nonblock(Socket s){u_long v=1;ioctlsocket(s,FIONBIO,&v);}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
using Socket=int;constexpr auto invalid=-1;
void close_socket(Socket s){close(s);}void nonblock(Socket s){fcntl(s,F_SETFL,O_NONBLOCK);}
#endif
using namespace extrapane::peers;using namespace std::chrono_literals;
void need(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
sockaddr_in address(unsigned port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(static_cast<std::uint16_t>(port));return a;}
int main(){auto directory=std::filesystem::temp_directory_path()/("extrapane-async-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(directory);std::atomic<bool> release=false;
try{
#ifdef _WIN32
WSADATA data{};need(!WSAStartup(MAKEWORD(2,2),&data),"Socket initialization");
#endif
Socket sink=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto local=address(0);need(!bind(sink,reinterpret_cast<sockaddr*>(&local),sizeof(local))&&!listen(sink,4),"Sink listen");
#ifdef _WIN32
int length=sizeof(local);
#else
socklen_t length=sizeof(local);
#endif
need(!getsockname(sink,reinterpret_cast<sockaddr*>(&local),&length),"Sink port");nonblock(sink);
std::atomic<bool> entered=false;std::atomic<unsigned> slowReady=0,slowStop=0;std::atomic<Socket> source=invalid,lateSource=invalid;std::atomic<bool> consumerEntered=false,consumerRelease=false;
std::thread::id nativeThread;std::mutex threadMutex;auto fixedThread=[&]{std::lock_guard lock(threadMutex);if(nativeThread==std::thread::id{})nativeThread=std::this_thread::get_id();need(nativeThread==std::this_thread::get_id(),"Native hooks changed thread");};
Service::Hooks providerHooks;providerHooks.prepare=[&](const auto&,auto& request){fixedThread();if(request.resource=="slow"){entered=true;const auto until=std::chrono::steady_clock::now()+5s;while(!release&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);}return Service::Endpoint{};};
providerHooks.ready=[&](const auto&,const auto& request,unsigned port){fixedThread();if(request.resource=="slow"){++slowReady;return;}auto socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto target=address(port);need(!::connect(socket,reinterpret_cast<sockaddr*>(&target),sizeof(target)),"Fake source connect");if(request.resource=="late"){lateSource=socket;need(send(socket,"queued",6,0)==6,"Deferred source send");}else source=socket;};
providerHooks.stopped=[&](const auto& context){fixedThread();if(context.kind==Kind::microphone)++slowStop;else if(context.kind==Kind::speaker){auto socket=lateSource.exchange(invalid);if(socket!=invalid)close_socket(socket);}else{auto socket=source.exchange(invalid);if(socket!=invalid)close_socket(socket);}};
providerHooks.poll=[&]{fixedThread();return std::vector<Service::Context>{};};
Service::Context pendingContext;bool reportedPending=false;std::atomic<bool> terminatePending=false;
Service::Hooks consumerHooks;consumerHooks.prepare_request=[&](const auto& context,auto& request){if(request.resource=="slow")pendingContext=context;};
consumerHooks.poll=[&]{if(terminatePending&&!reportedPending&&pendingContext.request){reportedPending=true;return std::vector<Service::Context>{pendingContext};}return std::vector<Service::Context>{};};
consumerHooks.prepare=[&](const auto&,auto& request){if(request.resource=="late"){consumerEntered=true;const auto until=std::chrono::steady_clock::now()+5s;while(!consumerRelease&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);}return Service::Endpoint{Service::Role::connect_local,ntohs(local.sin_port)};};consumerHooks.ready=[](const auto&,const auto&,auto){};
{
Service provider({directory/"provider","Slow native provider",{{"fast","Fast",Kind::camera,true,false,""},{"slow","Slow",Kind::microphone,true,false,""},{"late","Late consumer",Kind::speaker,true,false,""}},true},providerHooks);
Service consumer({directory/"consumer","Consumer",{},true},consumerHooks);
auto wait=[&](auto predicate){const auto until=std::chrono::steady_clock::now()+5s;while(!predicate()){need(std::chrono::steady_clock::now()<until,"Async test deadline");std::this_thread::sleep_for(1ms);}};
provider.start("127.0.0.1",0);unsigned port=0;wait([&]{for(const auto& e:provider.poll())if(e.kind==Service::EventKind::listening)port=e.port;return port!=0;});
auto connect=[&](std::string code){auto connection=consumer.connect({"localhost",static_cast<std::uint16_t>(port),"127.0.0.1",provider.identity(),std::move(code),false});bool connected=false;wait([&]{for(const auto& e:consumer.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected&&e.context.connection==connection)connected=true;}return connected;});auto request=consumer.catalog(connection);bool catalog=false;wait([&]{for(const auto& e:consumer.poll())if(e.kind==Service::EventKind::catalog&&e.context.request==request)catalog=true;return catalog;});return connection;};
auto first=connect(provider.pairing_code());auto request=consumer.open(first,{provider.identity(),"fast",false});Service::Token fastGrant=0;Socket received=invalid;
wait([&]{for(const auto& e:consumer.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::granted&&e.context.request==request)fastGrant=e.context.grant;}if(received==invalid)received=accept(sink,nullptr,nullptr);return fastGrant&&received!=invalid&&source!=invalid;});nonblock(received);
// The provider may send a stream_open immediately after its granted reply,
// before the consumer has finished native endpoint preparation. Buffer it per
// authorized grant, preserving other streams, then replay after endpoint readiness.
auto lateRequest=consumer.open(first,{provider.identity(),"late",false});wait([&]{return consumerEntered.load()&&lateSource!=invalid;});
need(send(source,"during",6,0)==6,"Live media during consumer preparation");std::string during;
wait([&]{char b[16];auto n=recv(received,b,sizeof(b),0);if(n>0)during.append(b,n);return during.size()==6;});need(during=="during","Consumer preparation blocked existing stream");
consumerRelease=true;Service::Token lateGrant=0;Socket lateReceived=invalid;
wait([&]{for(const auto& e:consumer.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::granted&&e.context.request==lateRequest)lateGrant=e.context.grant;}if(lateReceived==invalid)lateReceived=accept(sink,nullptr,nullptr);return lateGrant&&lateReceived!=invalid;});nonblock(lateReceived);std::string queued;
wait([&]{char b[16];auto n=recv(lateReceived,b,sizeof(b),0);if(n>0)queued.append(b,n);return queued.size()==6;});need(queued=="queued","Deferred stream bytes were not replayed");consumer.close(first,lateGrant);wait([&]{return lateSource==invalid;});close_socket(lateReceived);
auto second=connect({});auto slowRequest=consumer.open(second,{provider.identity(),"slow",false});(void)slowRequest;wait([&]{return entered.load();});
// Native setup remains blocked, while authenticated catalog control and the
// already-established media stream must continue on their independent IO owner.
need(send(source,"still-live",10,0)==10,"Existing source send");std::string delivered;const auto began=std::chrono::steady_clock::now();
wait([&]{char bytes[16];const auto n=recv(received,bytes,sizeof(bytes),0);if(n>0)delivered.append(bytes,n);return delivered.size()==10;});need(delivered=="still-live"&&!release,"Blocking setup starved established media");need(std::chrono::steady_clock::now()-began<1s,"Established media delayed by native setup");
auto query=consumer.catalog(first);bool replied=false;wait([&]{for(const auto& e:consumer.poll())if(e.kind==Service::EventKind::catalog&&e.context.request==query)replied=true;return replied;});
terminatePending=true;bool gone=false;wait([&]{for(const auto& e:provider.poll())if(e.kind==Service::EventKind::disconnected)gone=true;return gone;});release=true;wait([&]{return slowStop.load()!=0;});need(slowReady==0,"Cancelled preparation resurrected a native resource");
need(send(source,"after",5,0)==5,"Unrelated media was stopped by cancellation");delivered.clear();wait([&]{char bytes[16];const auto n=recv(received,bytes,sizeof(bytes),0);if(n>0)delivered.append(bytes,n);return delivered.size()==5;});need(delivered=="after","Unrelated media delivery failed");
consumer.close(first,fastGrant);wait([&]{return source==invalid;});close_socket(received);
}
close_socket(sink);std::filesystem::remove_all(directory);std::cout<<"Async native preparation: live media/control preserved, serialized hooks and no resurrection after cancellation passed\n";return 0;
}catch(const std::exception& e){release=true;std::cerr<<e.what()<<'\n';return 1;}}
