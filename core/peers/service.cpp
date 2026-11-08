#include <extrapane/service.hpp>
#include <extrapane/media_bridge.hpp>
#include <atomic>
#include <condition_variable>
#include <future>
#include <thread>

namespace extrapane::peers {
namespace {
using Clock=std::chrono::steady_clock;
auto deadline(){return Clock::now()+std::chrono::seconds(3);}
Milliseconds now(){return static_cast<Milliseconds>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
void need(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
struct Service::State {
    struct Resource {Context context;rpc::Open request;bool relay=false;Token other_connection=0,other_grant=0;};
    struct RelayPending {Token visitor=0,grant=0,request=0;bool cancelled=false;};
    struct Link {
        Token token;bool incoming;std::unique_ptr<AuthenticatedConnection> connection;
        std::unique_ptr<MediaBridge> bridge;std::unique_ptr<SessionDispatcher> dispatcher;
        std::map<Token,rpc::Open> pending;std::set<Token> catalogs;
        std::vector<Catalog> catalog_cache;
        std::map<Token,Context> pending_contexts;
        std::map<Token,Clock::time_point> requests;
        std::map<Token,Resource> resources;Token current_request=0;
        bool used_resources=false;
        Clock::time_point activity=Clock::now(),last_ping=Clock::now();
        Clock::time_point last_catalog=Clock::now()-std::chrono::seconds(11);
        Link(Token t,bool in,std::unique_ptr<AuthenticatedConnection> c):token(t),incoming(in),connection(std::move(c)){}
    };
    struct Result {Token token;bool incoming;std::unique_ptr<AuthenticatedConnection> connection;std::string error;std::uint64_t epoch=0;};
    Options options;Hooks hooks;TrustStore store;dm::peer_auth::Policy auth;SharingPolicy policy;
    std::mutex mutex,lifecycle_mutex,trust_mutex;std::condition_variable wake;std::deque<std::function<void()>> commands;std::deque<Event> events;
    std::atomic<Token> sequence{1};std::atomic<bool> ending=false;
    std::atomic<std::uint64_t> epoch=1;
    std::set<Token> cancelled;
    Clock::time_point last_poll=Clock::now();
    std::thread worker,acceptor;std::shared_ptr<transport::Listener> listener;std::atomic<bool> accepting=false;
    struct Retired {std::thread thread;std::shared_ptr<std::atomic<bool>> done;};
    std::vector<Retired> retired;
    std::shared_ptr<std::atomic<bool>> acceptor_done;
    std::atomic<std::uint64_t> listener_serial=1;
    std::vector<std::future<Result>> dialing;std::map<Token,std::unique_ptr<Link>> links;
    std::map<Token,RelayPending> relays;
    State(Options o,Hooks h):options(std::move(o)),hooks(std::move(h)),store(options.private_directory),auth(store.identity(),options.pairing_required),policy(Catalog{store.identity(),static_cast<std::uint64_t>(now())+1,1,options.name,options.offers}){
        policy.pairing_required(options.pairing_required);for(const auto& d:store.devices())policy.remember(d.id);
        retired.reserve(16);worker=std::thread([this]{run();});
    }
    Token next(){auto id=sequence.fetch_add(1);need(id&&id<UINT64_MAX,"Peer request sequence exhausted");return id;}
    void post(std::function<void()> fn){std::lock_guard lock(mutex);need(!ending&&commands.size()<256,"Peer service command queue full or stopped");commands.push_back(std::move(fn));wake.notify_one();}
    void event(Event e){std::lock_guard lock(mutex);if(events.size()>=1024){ending=true;wake.notify_all();return;}events.push_back(std::move(e));}
    dm::peer_auth::Lookup lookup(){return [this](const DeviceId& id)->std::optional<dm::peer_auth::Secret>{auto key=store.secret(id);if(!key)return {};dm::peer_auth::Secret result;result.bytes=*key;return result;};}
    dm::peer_auth::Remember remember(std::string address={},std::uint16_t port=0,std::uint64_t generation=0){if(!generation)generation=epoch.load();return [this,address=std::move(address),port,generation](const DeviceId& id,const dm::peer_auth::Secret& key){std::lock_guard lock(trust_mutex);need(epoch==generation,"Pairing was cancelled");store.remember({id,"Paired device",address,port},key.bytes);};}
    Link& link(Token id){auto f=links.find(id);need(f!=links.end(),"Device connection is not active");return *f->second;}
    void stop_resource(Link& l,Token grant)noexcept{
        auto f=l.resources.find(grant);if(f==l.resources.end())return;
        auto resource=std::move(f->second);l.resources.erase(f);
        try{l.bridge->revoke(grant);}catch(...){}
        if(resource.relay){
            if(l.incoming)policy.close(grant);
            if(l.incoming&&!resource.other_grant)for(auto& [request,pending]:relays)if(pending.visitor==l.token&&pending.grant==grant&&!pending.cancelled){pending.cancelled=true;try{l.connection->queue({rpc::Operation::rejected,pending.request,rpc::detail("Delegated resource was withdrawn")});}catch(...){}}
            auto other=links.find(resource.other_connection);
            if(other!=links.end()&&resource.other_grant){
                stop_resource(*other->second,resource.other_grant);
                try{other->second->connection->queue({other->second->incoming?rpc::Operation::stopped:rpc::Operation::close,resource.other_grant,{}});}catch(...){}
            }
        }else try{if(hooks.stopped)hooks.stopped(resource.context);}catch(...){}
        try{event({EventKind::stopped,resource.context});}catch(...){}
    }
    void cancel_request(Link& l,Token request)noexcept{
        auto relay=relays.find(request);
        if(relay!=relays.end()){
            auto pending=relay->second;relays.erase(relay);auto visitor=links.find(pending.visitor);
            if(visitor!=links.end()){stop_resource(*visitor->second,pending.grant);if(!pending.cancelled)try{visitor->second->connection->queue({rpc::Operation::rejected,pending.request,rpc::detail("Provider request ended")});}catch(...){}}
            l.pending_contexts.erase(request);
        }else {auto f=l.pending_contexts.find(request);if(f!=l.pending_contexts.end()){try{if(hooks.cancel_request)hooks.cancel_request(f->second);}catch(...){}l.pending_contexts.erase(f);}}
        l.pending.erase(request);l.requests.erase(request);
    }
    void drop(Token id,std::string reason={}){auto f=links.find(id);if(f==links.end())return;auto& l=*f->second;l.connection->cancel();if(l.dispatcher)l.dispatcher->close();while(!l.resources.empty())stop_resource(l,l.resources.begin()->first);while(!l.pending_contexts.empty())cancel_request(l,l.pending_contexts.begin()->first);Context c{id,0,0,l.connection->remote_id(),l.incoming};const bool wasProvider=!l.incoming;links.erase(f);if(wasProvider)revoked(policy.remote_removed(c.peer));event({EventKind::disconnected,c,0,std::move(reason)});}
    std::uint16_t prepare(Link& l,Resource& r){need(bool(hooks.prepare)&&bool(hooks.ready),"Native resource adapter is unavailable");auto endpoint=hooks.prepare(r.context,r.request);std::uint16_t port=endpoint.port;if(endpoint.role==Role::listen_local)port=l.bridge->listen(r.context.grant);else l.bridge->connect(r.context.grant,port);hooks.ready(r.context,r.request,port);l.used_resources=true;return port;}
    void granted(Link& l,Token grant,std::uint16_t port){const auto& resource=l.resources.at(grant);auto public_request=resource.request;public_request.native_credential.clear();event({EventKind::granted,resource.context,port,{},{},l.connection->identity().paired,std::move(public_request)});}
    void add(Result result){if(result.epoch!=epoch||cancelled.erase(result.token)){event({EventKind::disconnected,{result.token},0,"Connection cancelled"});return;}if(!result.connection){event({EventKind::error,{result.token},0,result.error});return;}need(links.size()<64,"Peer service connection budget exhausted");auto object=std::make_unique<Link>(result.token,result.incoming,std::move(result.connection));auto& l=*object;const auto identity=l.connection->identity();if(identity.paired&&identity.device!=store.identity()){need(store.secret(identity.device).has_value(),"Device was forgotten during authentication");policy.remember(identity.device);}
        l.bridge=std::make_unique<MediaBridge>(!l.incoming,[&l](Token grant){return l.resources.contains(grant);},[&l](const rpc::Message& m){l.connection->queue(m);});
        Adapter adapter;
        adapter.start=[this,&l](const Grant& grant,const rpc::Open& request,std::string& reason){if(!l.incoming||grant.delegated){reason="Delegated relay is not available";return false;}Resource r{{l.token,l.current_request,grant.id,l.connection->remote_id(),true,grant.kind},request};l.resources.emplace(grant.id,std::move(r));try{auto port=prepare(l,l.resources.at(grant.id));granted(l,grant.id,port);return true;}catch(const std::exception& e){reason=e.what();return false;}};
        adapter.stop=[this,&l](const Grant& g){stop_resource(l,g.id);};
        adapter.credential=[&l](const Grant& g){return l.resources.at(g.id).request.native_credential;};
        l.dispatcher=std::make_unique<SessionDispatcher>(policy,*l.connection,std::move(adapter));Context c{l.token,0,0,identity.device,l.incoming};links.emplace(l.token,std::move(object));event({EventKind::connected,c,0,{},{},identity.paired});if(identity.paired)event({EventKind::trust_changed,c});
    }
    void revoked(const std::vector<Grant>& grants){
        for(auto& [id,l]:links){
            for(auto& message:l->dispatcher->revoked(grants))l->connection->queue(message);
            for(const auto& grant:grants){auto f=l->resources.find(grant.id);if(l->incoming&&f!=l->resources.end()&&f->second.relay){stop_resource(*l,grant.id);l->connection->queue({rpc::Operation::stopped,grant.id,{}});}}
        }
    }
    void relay_open(Link& visitor,const rpc::Message& message,const rpc::Open& request){
        const auto decision=policy.open(visitor.connection->identity(),request.owner,request.resource,request.input,now());
        if(!decision.grant){visitor.connection->queue({rpc::Operation::rejected,message.id,rpc::detail(decision.reason)});return;}
        const auto grant=*decision.grant;Link* provider=nullptr;
        for(auto& [id,l]:links)if(!l->incoming&&l->connection->identity().paired&&l->connection->remote_id()==request.owner){provider=l.get();break;}
        if(!grant.delegated||!provider||provider->pending.size()+provider->resources.size()>=256||relays.size()>=256){policy.close(grant.id);visitor.connection->queue({rpc::Operation::rejected,message.id,rpc::detail("Direct paired provider is unavailable")});return;}
        const auto token=next();
        visitor.resources.emplace(grant.id,Resource{{visitor.token,message.id,grant.id,visitor.connection->remote_id(),true,grant.kind},request,true,provider->token,0});
        relays.emplace(token,RelayPending{visitor.token,grant.id,message.id});
        provider->pending.emplace(token,request);
        provider->requests[token]=Clock::now();
        provider->pending_contexts.emplace(token,Context{provider->token,token,0,provider->connection->remote_id(),false,grant.kind});
        try{provider->connection->queue({rpc::Operation::open,token,rpc::encode(request)});}catch(...){provider->pending.erase(token);provider->pending_contexts.erase(token);relays.erase(token);stop_resource(visitor,grant.id);throw;}
    }
    bool relay_granted(Link& provider,Token request,const rpc::Granted& grant){
        auto found=relays.find(request);if(found==relays.end())return false;
        const auto pending=found->second;relays.erase(found);provider.requests.erase(request);
        auto open=provider.pending.at(request);provider.pending.erase(request);auto context=provider.pending_contexts.at(request);provider.pending_contexts.erase(request);context.grant=grant.grant;
        auto visitor=links.find(pending.visitor);
        if(visitor==links.end()||!visitor->second->resources.contains(pending.grant)){provider.connection->queue({rpc::Operation::close,grant.grant,{}});return true;}
        auto& v=*visitor->second;auto& local=v.resources.at(pending.grant);local.other_grant=grant.grant;
        provider.resources.emplace(grant.grant,Resource{context,std::move(open),true,v.token,pending.grant});
        try{
            if(local.context.kind==Kind::existing_screen){auto port=provider.bridge->listen(grant.grant);v.bridge->connect(pending.grant,port);}
            else {auto port=v.bridge->listen(pending.grant);provider.bridge->connect(grant.grant,port);}
            provider.used_resources=true;v.used_resources=true;
            v.connection->queue({rpc::Operation::granted,pending.request,rpc::encode(rpc::Granted{pending.grant,grant.native_credential})});
            granted(v,pending.grant,0);
        }catch(...){stop_resource(v,pending.grant);throw;}
        return true;
    }
    void message(Link& l,rpc::Message m){using O=rpc::Operation;l.activity=Clock::now();
        if(m.operation==O::catalog_reply||m.operation==O::granted||m.operation==O::rejected)l.requests.erase(m.id);
        if(m.operation>=O::stream_open&&m.operation<=O::stream_close){l.bridge->receive(m);return;}
        if(m.operation==O::catalog_reply){need(l.catalogs.erase(m.id)>0,"Unsolicited catalog response");auto catalogs=rpc::decode_catalogs(m.payload);for(const auto& c:catalogs){need(c.device==l.connection->remote_id()||l.connection->identity().paired,"Unverified delegated catalog rejected");if(policy.gateway_enabled()&&!l.incoming&&l.connection->identity().paired&&c.device==l.connection->remote_id())revoked(policy.remote(l.connection->identity(),c,0,now()));}l.catalog_cache=catalogs;event({EventKind::catalog,{l.token,m.id,0,l.connection->remote_id(),l.incoming},0,{},std::move(catalogs)});return;}
        if(m.operation==O::granted){auto p=l.pending.find(m.id);need(!l.incoming&&p!=l.pending.end(),"Unsolicited resource grant");auto grant=rpc::decode_granted(m.payload);need(!l.resources.contains(grant.grant),"Duplicate resource grant");if(relay_granted(l,m.id,grant))return;auto context=l.pending_contexts.at(m.id);context.grant=grant.grant;Resource r{context,std::move(p->second)};l.pending.erase(p);l.pending_contexts.erase(m.id);if(!grant.native_credential.empty())r.request.native_credential=grant.native_credential;l.resources.emplace(grant.grant,std::move(r));try{auto port=prepare(l,l.resources.at(grant.grant));granted(l,grant.grant,port);}catch(...){stop_resource(l,grant.grant);l.connection->queue({O::close,grant.grant,{}});throw;}return;}
        if(m.operation==O::rejected){cancel_request(l,m.id);l.catalogs.erase(m.id);event({EventKind::error,{l.token,m.id,0,l.connection->remote_id(),l.incoming},0,rpc::detail(m.payload)});return;}
        if(m.operation==O::stopped){stop_resource(l,m.id);return;}
        if(m.operation==O::pong)return;
        if(m.operation==O::open&&!l.incoming){l.connection->queue({O::rejected,m.id,rpc::detail("Open a separate connection to request this device's resources")});return;}
        if(m.operation==O::open){auto request=rpc::decode_open(m.payload);if(request.owner!=store.identity()){relay_open(l,m,request);return;}}
        if(m.operation==O::close&&l.incoming){auto resource=l.resources.find(m.id);if(resource!=l.resources.end()&&resource->second.relay){stop_resource(l,m.id);l.connection->queue({O::stopped,m.id,{}});return;}}
        l.current_request=m.id;l.connection->queue(l.dispatcher->handle(m,now()));
    }
    void run(){while(!ending){std::deque<std::function<void()>> work;{std::lock_guard lock(mutex);work.swap(commands);}for(auto& fn:work)try{fn();}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}
        for(auto i=dialing.begin();i!=dialing.end();){if(i->wait_for(std::chrono::seconds(0))!=std::future_status::ready){++i;continue;}try{add(i->get());}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}i=dialing.erase(i);}
        std::vector<std::pair<Token,std::string>> dead;bool progress=false;
        for(auto& [id,l]:links)try{for(const auto& [request,began]:l->requests)need(Clock::now()-began<std::chrono::seconds(15),"Peer request timed out");for(unsigned turn=0;turn<16;++turn){bool moved=l->connection->pump(deadline());while(auto m=l->connection->next()){message(*l,std::move(*m));moved=true;}if(!l->connection->pending_output())moved=l->bridge->pump()||moved;progress=progress||moved;if(!moved)break;}if(l->used_resources&&!policy.gateway_enabled()&&l->resources.empty()&&l->pending.empty()&&!l->connection->pending_output()){dead.emplace_back(id,"All resources disconnected");continue;}if(Clock::now()-l->activity>std::chrono::seconds(30))throw std::runtime_error("Device connection timed out");if(Clock::now()-l->last_ping>std::chrono::seconds(10)){l->connection->queue({rpc::Operation::ping,next(),{}});l->last_ping=Clock::now();}}catch(const std::exception& e){dead.emplace_back(id,e.what());}
        for(auto& [id,reason]:dead)drop(id,std::move(reason));
        try{revoked(policy.expire(now()));if(policy.gateway_enabled())for(auto& [id,l]:links)if(!l->incoming&&l->connection->identity().paired&&l->catalogs.empty()&&Clock::now()-l->last_catalog>std::chrono::seconds(10)){const auto request=next();l->catalogs.insert(request);l->requests[request]=Clock::now();l->connection->queue({rpc::Operation::catalog,request,{}});l->last_catalog=Clock::now();}}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}
        if(hooks.poll&&Clock::now()-last_poll>=std::chrono::milliseconds(50)){last_poll=Clock::now();try{for(const auto& context:hooks.poll()){auto f=links.find(context.connection);if(f==links.end())continue;auto& l=*f->second;if(!context.grant){cancel_request(l,context.request);continue;}if(!l.resources.contains(context.grant))continue;if(l.incoming)l.connection->queue(l.dispatcher->handle({rpc::Operation::close,context.grant,{}},now()));else {stop_resource(l,context.grant);l.connection->queue({rpc::Operation::close,context.grant,{}});}}}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}}
        if(progress)std::this_thread::yield();else {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(2),[this]{return ending||!commands.empty();});}
    }while(!links.empty())drop(links.begin()->first);for(auto& task:dialing)task.wait();dialing.clear();}
    void reap_listeners(){for(auto i=retired.begin();i!=retired.end();)if(*i->done){i->thread.join();i=retired.erase(i);}else ++i;}
    void stop_listener(){accepting=false;++listener_serial;auto current=listener;if(current)current->cancel();if(acceptor.joinable())retired.push_back({std::move(acceptor),std::move(acceptor_done)});listener.reset();reap_listeners();}
};
Service::Service(Options options,Hooks hooks):state_(std::make_unique<State>(std::move(options),std::move(hooks))){}
Service::~Service(){auto& s=*state_;s.stop_listener();s.auth.stop();s.ending=true;s.wake.notify_all();if(s.worker.joinable())s.worker.join();for(auto& retired:s.retired)if(retired.thread.joinable())retired.thread.join();}
DeviceId Service::identity()const{return state_->store.identity();}
std::string Service::pairing_code()const{return state_->auth.code();}
std::vector<RememberedDevice> Service::remembered()const{return state_->store.devices();}
void Service::start(std::string bind,std::uint16_t port){
    auto& s=*state_;std::lock_guard lifecycle(s.lifecycle_mutex);
    need(!s.listener,"Sharing listener is already running");s.reap_listeners();need(s.retired.size()<16,"Previous sharing listeners are still stopping");
    auto listener=std::make_shared<transport::Listener>(bind,port);
    s.post([&s,port=listener->port()]{s.policy.sharing(true);s.event({EventKind::listening,{},port});});
    s.listener=listener;s.accepting=true;auto done=std::make_shared<std::atomic<bool>>(false);s.acceptor_done=done;const auto serial=s.listener_serial.load();
    s.acceptor=std::thread([&s,listener,done,serial]{
        while(s.accepting&&s.listener_serial==serial){
            try{
                auto stream=listener->accept(deadline());const auto generation=s.epoch.load();
                auto connection=std::make_shared<std::unique_ptr<AuthenticatedConnection>>(std::make_unique<AuthenticatedConnection>(AuthenticatedConnection::server(std::move(stream),s.auth,s.lookup(),s.remember({},0,generation),deadline())));
                auto token=s.next();s.post([&s,connection,token,generation,serial]{if(s.accepting&&s.listener_serial==serial)s.add({token,true,std::move(*connection),{},generation});});
            }catch(const std::exception&){/* Timeout/rejected handshake never enters application state. */}
        }
        *done=true;
    });
}
void Service::stop(){auto& s=*state_;std::lock_guard lifecycle(s.lifecycle_mutex);s.stop_listener();s.post([&s]{s.revoked(s.policy.sharing(false));std::vector<Token> incoming;for(const auto& [id,link]:s.links)if(link->incoming)incoming.push_back(id);for(auto id:incoming)s.drop(id,"Sharing stopped");});}
Service::Token Service::connect(ConnectOptions options){auto& s=*state_;const auto token=s.next();const auto generation=s.epoch.load();s.post([&s,token,generation,options=std::move(options)]()mutable{if(s.dialing.size()>=4){s.event({EventKind::error,{token},0,"Too many pending peer connections"});return;}s.dialing.push_back(std::async(std::launch::async,[&s,token,generation,options=std::move(options)]{try{need(s.epoch==generation,"Connection cancelled");dm::peer_auth::ClientOptions auth;auth.local=s.store.identity();auth.expected_peer=options.expected_peer;auth.code=options.code;auth.mode=options.open_mode?dm::peer_auth::Mode::open:dm::peer_auth::Mode::pair;if(!options.open_mode&&valid_id(options.expected_peer)){auto key=s.store.secret(options.expected_peer);if(key){auth.mode=dm::peer_auth::Mode::remembered;dm::peer_auth::Secret secret;secret.bytes=*key;auth.remembered=secret;}}auto connection=AuthenticatedConnection::client(transport::Stream::connect(options.address,options.port,options.local,deadline()),auth,s.remember(options.address,options.port,generation),deadline());return State::Result{token,false,std::make_unique<AuthenticatedConnection>(std::move(connection)),{},generation};}catch(const std::exception& e){return State::Result{token,false,{},e.what(),generation};}}));});return token;}
Service::Token Service::catalog(Token connection){auto& s=*state_;auto request=s.next();s.post([&s,connection,request]{auto& l=s.link(connection);need(l.catalogs.size()<64,"Too many catalog requests");l.catalogs.insert(request);l.requests[request]=Clock::now();l.connection->queue({rpc::Operation::catalog,request,{}});});return request;}
Service::Token Service::open(Token connection,rpc::Open request){auto& s=*state_;auto token=s.next();s.post([&s,connection,token,request=std::move(request)]()mutable{try{auto& l=s.link(connection);need(!l.incoming&&l.pending.size()+l.resources.size()<256,"Resource connection budget exhausted");need(request.owner==l.connection->remote_id()||l.connection->identity().paired,"A paired gateway is required for delegated resources");const Offer* offer=nullptr;for(const auto& catalog:l.catalog_cache)if(catalog.device==request.owner)for(const auto& item:catalog.offers)if(item.id==request.resource)offer=&item;need(offer&&offer->available,"Refresh this device's available resources before connecting");need(!request.input||offer->input_allowed,"Remote input is not permitted");rpc::encode(request);Context context{connection,token,0,l.connection->remote_id(),false,offer->kind};l.pending_contexts.emplace(token,context);try{if(s.hooks.prepare_request)s.hooks.prepare_request(context,request);auto payload=rpc::encode(request);l.pending.emplace(token,std::move(request));l.requests[token]=Clock::now();l.connection->queue({rpc::Operation::open,token,std::move(payload)});}catch(...){s.cancel_request(l,token);throw;}}catch(const std::exception& e){s.event({EventKind::error,{connection,token},0,e.what()});}});return token;}
void Service::close(Token connection,Token grant){auto& s=*state_;s.post([&s,connection,grant]{auto& l=s.link(connection);if(l.incoming){auto f=l.resources.find(grant);if(f!=l.resources.end()&&f->second.relay){s.stop_resource(l,grant);l.connection->queue({rpc::Operation::stopped,grant,{}});}else l.connection->queue(l.dispatcher->handle({rpc::Operation::close,grant,{}},now()));}else{s.stop_resource(l,grant);l.connection->queue({rpc::Operation::close,grant,{}});}});}
void Service::disconnect(Token connection){auto& s=*state_;s.post([&s,connection]{if(!s.links.contains(connection)){need(s.cancelled.size()<256,"Cancellation budget exhausted");s.cancelled.insert(connection);}s.drop(connection);});}
void Service::offers(std::vector<Offer> offers){auto& s=*state_;s.post([&s,offers=std::move(offers)]()mutable{s.revoked(s.policy.offers(std::move(offers)));});}
void Service::forget(DeviceId peer){auto& s=*state_;s.post([&s,peer]{{std::lock_guard lock(s.trust_mutex);++s.epoch;s.store.forget(peer);}s.revoked(s.policy.forget(peer));std::vector<Token> ids;for(const auto& [id,l]:s.links)if(l->connection->remote_id()==peer)ids.push_back(id);for(auto id:ids)s.drop(id,"Device was forgotten");s.event({EventKind::trust_changed,{}});});}
void Service::pairing_required(bool required){auto& s=*state_;s.post([&s,required]{s.auth.set_required(required);s.revoked(s.policy.pairing_required(required));if(required){std::vector<Token> ids;for(const auto& [id,l]:s.links)if(!l->connection->identity().paired)ids.push_back(id);for(auto id:ids)s.drop(id,"Pairing is now required");}});}
void Service::gateway(bool enabled){auto& s=*state_;s.post([&s,enabled]{s.revoked(s.policy.gateway(enabled));if(enabled)for(auto& [id,l]:s.links)if(!l->incoming&&l->connection->identity().paired){l->last_catalog=Clock::now()-std::chrono::seconds(11);for(const auto& catalog:l->catalog_cache)if(catalog.device==l->connection->remote_id())s.revoked(s.policy.remote(l->connection->identity(),catalog,0,now()));}});}
std::vector<Service::Event> Service::poll(){auto& s=*state_;std::lock_guard lock(s.mutex);std::vector<Event> result;while(!s.events.empty()){result.push_back(std::move(s.events.front()));s.events.pop_front();}return result;}
}
