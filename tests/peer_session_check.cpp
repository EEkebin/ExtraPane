#include <extrapane/peer_session.hpp>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace extrapane::peers;
using namespace std::chrono_literals;
static auto deadline(){return std::chrono::steady_clock::now()+5s;}
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){try{
    const auto host=dm::peer_auth::random_identity(),guest=dm::peer_auth::random_identity();
    Catalog catalog{host,1,1,"Loopback host",{{"camera-a","Test camera",Kind::camera,true,false,""}}};
    SharingPolicy sharing(catalog);sharing.sharing(true);
    dm::peer_auth::Policy auth(host);std::map<DeviceId,dm::peer_auth::Secret> hostPairs,clientPairs;
    transport::Listener listener("127.0.0.1",0);
    auto accepted=std::async(std::launch::async,[&]{return AuthenticatedConnection::server(listener.accept(deadline()),auth,
        [&](const DeviceId& id)->std::optional<dm::peer_auth::Secret>{auto i=hostPairs.find(id);return i==hostPairs.end()?std::nullopt:std::optional(i->second);},
        [&](const DeviceId& id,const dm::peer_auth::Secret& secret){hostPairs.insert_or_assign(id,secret);},deadline());});
    dm::peer_auth::ClientOptions options;options.local=guest;options.expected_peer=host;options.code=auth.code();
    auto client=AuthenticatedConnection::client(transport::Stream::connect("localhost",listener.port(),"127.0.0.1",deadline()),options,
        [&](const DeviceId& id,const dm::peer_auth::Secret& secret){clientPairs.insert_or_assign(id,secret);},deadline());
    auto server=accepted.get();require(server.identity().paired&&server.remote_id()==guest,"server identity not authenticated");require(client.identity().paired&&client.remote_id()==host,"client identity not authenticated");
    require(hostPairs.contains(guest)&&clientPairs.contains(host),"pair not remembered");sharing.remember(server.remote_id());
    unsigned starts=0,stops=0;bool failStart=false;
    SessionDispatcher dispatcher(sharing,server,{[&](const Grant& g,const rpc::Open& request,std::string& reason){++starts;require(g.owner==host&&request.resource=="camera-a","adapter received unapproved resource");if(failStart){reason="Adapter unavailable";return false;}return true;},[&](const Grant&){++stops;}});
    auto exchange=[&](rpc::Message request){
        auto serving=std::async(std::launch::async,[&]{auto message=server.receive(deadline());server.send(dispatcher.handle(message,100),deadline());});
        client.send(request,deadline());auto response=client.receive(deadline());serving.get();return response;
    };
    auto reply=exchange({rpc::Operation::catalog,1,{}});require(reply.operation==rpc::Operation::catalog_reply&&reply.id==1,"catalog reply mismatch");
    auto catalogs=rpc::decode_catalogs(reply.payload);require(catalogs.size()==1&&catalogs.front()==catalog,"catalog content mismatch");
    rpc::Open open{host,"camera-a",false};
    reply=exchange({rpc::Operation::open,2,rpc::encode(open)});require(reply.operation==rpc::Operation::granted,"open not granted");const auto grant=rpc::decode_granted(reply.payload).grant;require(dispatcher.owns(grant)&&starts==1,"adapter not started");
    open.resource="127.0.0.1:22";reply=exchange({rpc::Operation::open,3,rpc::encode(open)});require(reply.operation==rpc::Operation::rejected&&starts==1,"arbitrary endpoint reached adapter");
    open.resource="camera-a";open.input=true;reply=exchange({rpc::Operation::open,4,rpc::encode(open)});require(reply.operation==rpc::Operation::rejected&&starts==1,"input permission bypass");
    reply=exchange({rpc::Operation::close,grant+100,{}});require(reply.operation==rpc::Operation::rejected&&stops==0,"foreign grant closed");
    auto revoked=sharing.forget(guest);auto events=dispatcher.revoked(revoked);require(events.size()==1&&stops==1&&!dispatcher.owns(grant),"revocation did not stop adapter");
    reply=exchange({rpc::Operation::open,5,rpc::encode(open)});require(reply.operation==rpc::Operation::rejected&&starts==1,"revoked identity reopened resource");
    sharing.remember(guest);open.input=false;failStart=true;reply=exchange({rpc::Operation::open,6,rpc::encode(open)});require(reply.operation==rpc::Operation::rejected&&starts==2&&stops==2&&sharing.grants().empty(),"failed adapter was not rolled back");
    failStart=false;reply=exchange({rpc::Operation::open,7,rpc::encode(open)});require(reply.operation==rpc::Operation::granted,"reopen failed");dispatcher.close();require(stops==3&&sharing.grants().empty(),"teardown did not release grants");
    std::cout<<"peer session: exporter-bound pairing, catalog, grant, endpoint/input denial, revocation, rollback and teardown passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
