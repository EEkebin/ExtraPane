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

// A byte-transparent TCP proxy cuts the outer TLS transport without changing
// either Service's trust state or resource policy.
class Proxy {
    Socket listener=invalid;unsigned target;std::atomic<bool> stopping=false,cutRequested=false;std::thread worker;
public:
    unsigned port=0;
    explicit Proxy(unsigned destination):target(destination){
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto a=address(0);need(!bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof(a))&&!listen(listener,4),"Proxy listen");
#ifdef _WIN32
        int size=sizeof(a);
#else
        socklen_t size=sizeof(a);
#endif
        need(!getsockname(listener,reinterpret_cast<sockaddr*>(&a),&size),"Proxy address");port=ntohs(a.sin_port);nonblock(listener);
        worker=std::thread([this]{Socket client=invalid,server=invalid;auto reset=[&]{if(client!=invalid)close_socket(client);if(server!=invalid)close_socket(server);client=server=invalid;};
            while(!stopping){if(cutRequested.exchange(false))reset();
                if(client==invalid){client=accept(listener,nullptr,nullptr);if(client!=invalid){server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto a=address(target);if(::connect(server,reinterpret_cast<sockaddr*>(&a),sizeof(a)))reset();}}
                if(client!=invalid){fd_set set;FD_ZERO(&set);FD_SET(client,&set);FD_SET(server,&set);timeval timeout{0,5000};auto n=select(static_cast<int>(std::max(client,server)+1),&set,nullptr,nullptr,&timeout);
                    if(n>0){for(auto pair:{std::pair{client,server},std::pair{server,client}})if(FD_ISSET(pair.first,&set)){char b[8192];int count=recv(pair.first,b,sizeof(b),0);if(count<=0){reset();break;}int offset=0;while(offset<count){int wrote=send(pair.second,b+offset,count-offset,
#ifdef _WIN32
0
#else
MSG_NOSIGNAL
#endif
);if(wrote<=0){reset();break;}offset+=wrote;}if(client==invalid)break;}}
                }else std::this_thread::sleep_for(1ms);
            }reset();});
    }
    void cut(){cutRequested=true;}
    ~Proxy(){stopping=true;worker.join();close_socket(listener);}
};
int main(){
#ifdef _WIN32
    WSADATA data{};need(!WSAStartup(MAKEWORD(2,2),&data),"WSA");
#endif
    auto dir=std::filesystem::temp_directory_path()/("extrapane-reconnect-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);
    try{
        std::atomic<unsigned> prepared=0;
        Service::Hooks hooks;hooks.prepare=[&](const auto&,auto&){++prepared;return Service::Endpoint{};};hooks.ready=[](const auto&,const auto&,auto){};
        std::vector<Offer> offers{{"camera","Camera",Kind::camera,true,false,""},{"usb","USB",Kind::usb,true,false,""}};
        Service provider({dir/"provider","Provider",offers,true},hooks), consumer({dir/"consumer","Consumer",{},true},hooks);
        auto wait=[&](auto predicate){auto end=std::chrono::steady_clock::now()+8s;while(!predicate()){need(std::chrono::steady_clock::now()<end,"Reconnect deadline");std::this_thread::sleep_for(2ms);}};
        provider.start("127.0.0.1",0);unsigned port=0;wait([&]{for(auto e:provider.poll())if(e.kind==Service::EventKind::listening)port=e.port;return port!=0;});Proxy proxy(port);
        bool initial=true;
        auto open=[&](std::string resource){auto token=consumer.connect({initial?"127.0.0.1":"localhost",static_cast<std::uint16_t>(proxy.port),"127.0.0.1",initial?provider.identity():DeviceId{},initial?provider.pairing_code():std::string{},false});initial=false;
            bool connected=false;wait([&]{for(auto e:consumer.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::connected)connected=true;}return connected;});
            auto query=consumer.catalog(token);bool catalog=false;wait([&]{for(auto e:consumer.poll())if(e.kind==Service::EventKind::catalog&&e.context.request==query)catalog=true;return catalog;});
            auto request=consumer.open(token,{provider.identity(),resource,false});Service::Token grant=0;wait([&]{for(auto e:consumer.poll()){if(e.kind==Service::EventKind::error)throw std::runtime_error(e.detail);if(e.kind==Service::EventKind::granted&&e.context.request==request)grant=e.context.grant;}return grant!=0;});return std::pair{token,grant};};
        auto [first,old]=open("camera");proxy.cut();bool retry=false,restored=false;Service::Token newGrant=0;
        wait([&]{for(auto e:consumer.poll()){if(e.kind==Service::EventKind::reconnecting)retry=true;if(e.kind==Service::EventKind::granted){need(retry&&e.context.connection==first&&e.resource&&e.resource->resource=="camera","Restored wrong connection/resource");newGrant=e.context.grant;restored=true;}}return restored;});
        need(newGrant!=old,"Old grant reused");consumer.disconnect(first);bool gone=false;wait([&]{for(auto e:consumer.poll())if(e.kind==Service::EventKind::disconnected)gone=true;return gone;});
        auto [second,secondGrant]=open("camera");proxy.cut();retry=false;wait([&]{for(auto e:consumer.poll())if(e.kind==Service::EventKind::reconnecting)retry=true;return retry;});
        provider.offers({offers[1]});gone=false;bool denied=false;wait([&]{for(auto e:consumer.poll()){need(e.kind!=Service::EventKind::granted,"Withdrawn permission restored");if(e.kind==Service::EventKind::error)denied=true;if(e.kind==Service::EventKind::disconnected)gone=true;}return denied;});need(!gone,"Withdrawn resource destroyed authenticated catalog");consumer.disconnect(second);
        provider.offers(offers);
        auto [third,thirdGrant]=open("camera");proxy.cut();retry=false;wait([&]{for(auto e:consumer.poll())if(e.kind==Service::EventKind::reconnecting)retry=true;return retry;});
        consumer.close(third,thirdGrant);gone=false;wait([&]{for(auto e:consumer.poll())if(e.kind==Service::EventKind::disconnected)gone=true;return gone;});
        auto until=std::chrono::steady_clock::now()+1300ms;while(std::chrono::steady_clock::now()<until){for(auto e:consumer.poll())need(e.kind!=Service::EventKind::connected&&e.kind!=Service::EventKind::granted,"Cancelled reconnect resurrected");std::this_thread::sleep_for(2ms);}
        auto [fourth,usbGrant]=open("usb");proxy.cut();gone=false;wait([&]{for(auto e:consumer.poll()){need(e.kind!=Service::EventKind::reconnecting,"USB retried automatically");if(e.kind==Service::EventKind::disconnected)gone=true;}return gone;});
        auto wrong=provider.identity();wrong[0]^=0x80;
        consumer.connect({"localhost",static_cast<std::uint16_t>(proxy.port),"127.0.0.1",wrong,provider.pairing_code(),false});bool rejected=false;
        wait([&]{for(auto e:consumer.poll()){need(e.kind!=Service::EventKind::connected,"Explicit identity was replaced by address hint");if(e.kind==Service::EventKind::error)rejected=true;}return rejected;});
        {TrustStore store(dir/"ambiguous");PairSecret secret{};secret.fill(7);store.remember({provider.identity(),"One","127.0.0.1",static_cast<std::uint16_t>(proxy.port)},secret);store.remember({wrong,"Two","localhost",static_cast<std::uint16_t>(proxy.port)},secret);}
        Service ambiguous({dir/"ambiguous","Ambiguous",{},true},hooks);ambiguous.connect({"localhost",static_cast<std::uint16_t>(proxy.port),"127.0.0.1",{},provider.pairing_code(),false});rejected=false;
        wait([&]{for(auto e:ambiguous.poll()){need(e.kind!=Service::EventKind::connected,"Ambiguous hint was trusted");if(e.kind==Service::EventKind::error){need(e.detail.find("Several remembered")!=std::string::npos,"Wrong ambiguity failure");rejected=true;}}return rejected;});
        std::cout<<"Actual TLS reconnect: stable token, fresh permissions, explicit cancellation and no USB retry passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    std::filesystem::remove_all(dir);return 0;
}
