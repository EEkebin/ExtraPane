#include <extrapane/service.hpp>
#include <extrapane/media_bridge.hpp>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <future>
#include <thread>
#include <tuple>

namespace extrapane::peers {
namespace {
using Clock=std::chrono::steady_clock;
auto deadline(){return Clock::now()+std::chrono::seconds(3);}
Milliseconds now(){return static_cast<Milliseconds>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
void need(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
struct Service::State {
    using NativeCancel=std::shared_ptr<std::atomic<bool>>;
    struct Resource {Context context;rpc::Open request;bool relay=false;Token other_connection=0,other_grant=0;
        NativeCancel native_cancel;bool native_ready=false;Clock::time_point native_started=Clock::now();std::string failure;
    };
    struct RelayPending {Token visitor=0,grant=0,request=0;bool cancelled=false;};
    struct Tunnel {Token other_connection=0,other_grant=0,inner_token=0;std::shared_ptr<transport::Listener> listener;Clock::time_point expires=Clock::time_point::max();Token request=0;};
    struct Link {
        Token token;bool incoming;Token via_connection=0,via_grant=0;
        std::map<Token,Tunnel> tunnels;std::unique_ptr<AuthenticatedConnection> connection;
        std::optional<ConnectOptions> route;
        std::unique_ptr<MediaBridge> bridge;std::unique_ptr<SessionDispatcher> dispatcher;
        std::map<Token,rpc::Open> pending;std::set<Token> catalogs,automatic_catalogs,abandoned;
        std::vector<Catalog> catalog_cache;
        std::map<Token,Context> pending_contexts;
        std::map<Token,NativeCancel> pending_native;
        std::map<Token,Token> deferred_streams;
        std::deque<rpc::Message> deferred;
        std::deque<Token> discarded_streams,retired_grants;
        std::size_t deferred_bytes=0;
        std::map<Token,Clock::time_point> requests;
        std::map<Token,Resource> resources;Token current_request=0;
        bool used_resources=false,request_cancel_supported=false,automatic_presence=false,endpoint_supported=false,catalog_dirty=false;
        Clock::time_point activity=Clock::now(),last_ping=Clock::now();
        Clock::time_point last_catalog=Clock::now()-std::chrono::seconds(11);
        Link(Token t,bool in,std::unique_ptr<AuthenticatedConnection> c):token(t),incoming(in),connection(std::move(c)){}
    };
    struct Result {Token token;bool incoming;std::unique_ptr<AuthenticatedConnection> connection;std::string error;std::uint64_t epoch=0;std::optional<ConnectOptions> route;bool transient=false;Token via_connection=0,via_grant=0;bool revocation_complete=false;};
    struct SavedResource {rpc::Open request;Kind kind;Token previous_grant=0,restoring_request=0;};
    struct RetryPlan {ConnectOptions route;Reconnect budget{Kind::camera};std::vector<SavedResource> resources;bool dialing=false;Token catalog_request=0;Milliseconds began=0;std::set<Token> waiting;};
    std::map<Token,RetryPlan> retries;
    std::mutex native_mutex;std::condition_variable native_wake;
    std::deque<std::function<void()>> native_jobs,native_completions;
    std::thread native_worker;bool native_ending=false,native_poll_pending=false;
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
    std::string listener_address;
    std::vector<std::future<Result>> dialing;std::map<Token,std::unique_ptr<Link>> links;
    std::map<Token,RelayPending> relays;
    struct GatewayRoute {RememberedDevice device;Token pending=0;unsigned failures=0;Milliseconds due=0;bool attempted=false,disabled=false,identity_failed=false;};
    std::map<DeviceId,GatewayRoute> gateway_routes;
    std::map<Token,DeviceId> gateway_dials,dial_targets;
    struct EndpointPending {Token connection=0,visitor=0,visitor_request=0;DeviceId owner{};std::string code;Clock::time_point began=Clock::now();bool revocation=false;};
    std::map<Token,EndpointPending> endpoint_pending;
    std::set<Token> endpoint_inflight;
    std::map<Token,Token> retired_endpoints;
    void retire_endpoint(Token request,Token connection){retired_endpoints[request]=connection;while(retired_endpoints.size()>256)retired_endpoints.erase(retired_endpoints.begin());}
    struct EndpointRoute {Token via=0,pending=0;Milliseconds due=0;};
    std::map<DeviceId,EndpointRoute> endpoint_routes;
    bool auto_connect_enabled=false;
    struct ForgetAttempt {DeviceId peer;Milliseconds due=0;std::future<bool> job;};
    std::map<DeviceId,ForgetAttempt> forget_attempts;
    Milliseconds last_forget_poll=0;
    std::map<DeviceId,std::pair<std::string,std::uint16_t>> forget_routes;
    std::set<DeviceId> suppressed_routes;
    Clock::time_point last_routes=Clock::now();
    State(Options o,Hooks h):options(std::move(o)),hooks(std::move(h)),store(options.private_directory),auth(store.identity(),options.pairing_required),policy(Catalog{store.identity(),static_cast<std::uint64_t>(now())+1,1,options.name,options.offers}){
        policy.pairing_required(options.pairing_required);for(const auto& d:store.devices())policy.remember(d.id);
        retired.reserve(16);native_worker=std::thread([this]{native_run();});
        try{worker=std::thread([this]{run();});}catch(...){ {std::lock_guard lock(native_mutex);native_ending=true;}native_wake.notify_all();native_worker.join();throw;}
    }
    void native_run(){
        for(;;){std::function<void()> job;{std::unique_lock lock(native_mutex);native_wake.wait(lock,[&]{return native_ending||!native_jobs.empty();});if(native_jobs.empty()&&native_ending)break;job=std::move(native_jobs.front());native_jobs.pop_front();}
            try{job();}catch(...){/* Each preparation reports its failure; cleanup must not kill the executor. */}
        }
    }
    void native_post(std::function<void()> job,bool cleanup=false){std::lock_guard lock(native_mutex);need(!native_ending,"Native executor is stopping");if(!cleanup)need(native_jobs.size()<256,"Native preparation queue is full");if(cleanup)native_jobs.push_front(std::move(job));else native_jobs.push_back(std::move(job));native_wake.notify_one();}
    void complete(std::function<void()> result){std::lock_guard lock(native_mutex);native_completions.push_back(std::move(result));wake.notify_one();}
    void native_cleanup(Context context,bool pending,NativeCancel cancel)noexcept{
        if(cancel&&cancel->exchange(true))return;
        try{native_post([this,context,pending]{if(pending){if(hooks.cancel_request)hooks.cancel_request(context);}else if(hooks.stopped)hooks.stopped(context);},true);}catch(...){}
    }
    void discard_deferred(Link& l,Token grant){
        l.retired_grants.push_back(grant);while(l.retired_grants.size()>512)l.retired_grants.pop_front();
        std::set<Token> ids;for(auto it=l.deferred_streams.begin();it!=l.deferred_streams.end();)if(it->second==grant){ids.insert(it->first);l.discarded_streams.push_back(it->first);it=l.deferred_streams.erase(it);}else ++it;
        std::erase_if(l.deferred,[&](const auto& message){if(!ids.contains(message.id))return false;l.deferred_bytes-=rpc::header_size+message.payload.size();return true;});
        while(l.discarded_streams.size()>512)l.discarded_streams.pop_front();
    }
    void drain_deferred(Link& l,Token grant){
        std::set<Token> ids;for(const auto& [id,g]:l.deferred_streams)if(g==grant)ids.insert(id);
        for(auto it=l.deferred.begin();it!=l.deferred.end();)if(ids.contains(it->id)){auto message=std::move(*it);l.deferred_bytes-=rpc::header_size+message.payload.size();it=l.deferred.erase(it);l.bridge->receive(message);}else ++it;
        for(auto id:ids)l.deferred_streams.erase(id);
    }
    void stream_message(Link& l,const rpc::Message& message){
        const auto discarded=std::find(l.discarded_streams.begin(),l.discarded_streams.end(),message.id)!=l.discarded_streams.end();
        if(discarded){need(message.operation!=rpc::Operation::stream_open,"Reused retired stream identity");return;}
        if(message.operation==rpc::Operation::stream_open){const auto grant=rpc::integer(message.payload);if(std::find(l.retired_grants.begin(),l.retired_grants.end(),grant)!=l.retired_grants.end()){l.discarded_streams.push_back(message.id);while(l.discarded_streams.size()>512)l.discarded_streams.pop_front();l.connection->queue({rpc::Operation::stream_close,message.id,{}});return;}const auto resource=l.resources.find(grant);if(l.tunnels.contains(grant)){l.bridge->receive(message);return;}need(resource!=l.resources.end(),"Unapproved media stream");
            if(!resource->second.relay&&!resource->second.native_ready){need(l.deferred_streams.size()<256&&!l.deferred_streams.contains(message.id),"Too many pending media streams");l.deferred_streams.emplace(message.id,grant);}
        }
        if(l.deferred_streams.contains(message.id)){const auto bytes=rpc::header_size+message.payload.size();need(l.deferred.size()<4096&&bytes<=1024*1024-l.deferred_bytes,"Pending media exceeds its budget");l.deferred.push_back(message);l.deferred_bytes+=bytes;return;}
        l.bridge->receive(message);
    }
    void native_failure(Context context,const std::string& input){
        auto reason=input;std::string lower=reason;for(auto& c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for(const auto* secret:{"epct1:","access_code","session_ticket","password","secret","token="})if(lower.find(secret)!=lower.npos){reason="Native component failed (credential-bearing diagnostic withheld)";break;}
        if(reason.size()>768)reason.resize(768);for(auto& c:reason)if(static_cast<unsigned char>(c)<32||c==127)c=' ';
        if(reason.empty())reason="Native resource failed";
        auto linkIt=links.find(context.connection);if(linkIt==links.end())return;auto& l=*linkIt->second;auto found=l.resources.find(context.grant);if(found==l.resources.end())return;found->second.failure=reason;
        const bool announced=found->second.native_ready;
        if(context.incoming){try{if(announced)l.connection->queue({rpc::Operation::rejected,context.request,rpc::detail(reason)});auto response=l.dispatcher->handle({rpc::Operation::close,context.grant,{}},now());if(announced)l.connection->queue(response);}catch(...){stop_resource(l,context.grant);l.connection->cancel();}}
        else {stop_resource(l,context.grant);try{l.connection->queue({rpc::Operation::close,context.grant,{}});}catch(...){l.connection->cancel();}}
        retry_done(context.connection,context.request,false);event({EventKind::error,context,0,reason});
    }
    void begin_ready(Context context,rpc::Open request,std::uint16_t port,NativeCancel cancel){
        native_post([this,context,request=std::move(request),port,cancel]{
            if(*cancel)return;std::string failure;
            try{need(bool(hooks.ready),"Native resource adapter is unavailable");hooks.ready(context,request,port);}catch(const std::exception& e){failure=e.what();}catch(...){failure="Native resource launch failed";}
            complete([this,context,port,cancel,failure=std::move(failure)]{
                if(*cancel)return;auto f=links.find(context.connection);if(f==links.end())return;auto& l=*f->second;auto resource=l.resources.find(context.grant);if(resource==l.resources.end()||resource->second.native_cancel!=cancel)return;
                if(!failure.empty()){native_failure(context,failure);return;}
                try{if(context.incoming)l.connection->queue({rpc::Operation::granted,context.request,rpc::encode(rpc::Granted{context.grant,resource->second.request.native_credential})});resource->second.native_ready=true;l.used_resources=true;l.bridge->resume(context.grant);drain_deferred(l,context.grant);granted(l,context.grant,port);}catch(const std::exception& e){native_failure(context,e.what());}
            });
        });
    }
    void prepare(Link&,Resource& resource){
        need(bool(hooks.prepare)&&bool(hooks.ready),"Native resource adapter is unavailable");if(!resource.native_cancel)resource.native_cancel=std::make_shared<std::atomic<bool>>(false);resource.native_started=Clock::now();
        const auto context=resource.context;const auto cancel=resource.native_cancel;auto request=resource.request;
        native_post([this,context,cancel,request=std::move(request)]()mutable{
            if(*cancel)return;Endpoint endpoint;std::string failure;
            try{endpoint=hooks.prepare(context,request);}catch(const std::exception& e){failure=e.what();}catch(...){failure="Native resource preparation failed";}
            complete([this,context,cancel,endpoint,request=std::move(request),failure=std::move(failure)]()mutable{
                if(*cancel)return;auto f=links.find(context.connection);if(f==links.end())return;auto& l=*f->second;auto resource=l.resources.find(context.grant);if(resource==l.resources.end()||resource->second.native_cancel!=cancel)return;
                if(!failure.empty()){native_failure(context,failure);return;}
                try{resource->second.request=std::move(request);std::uint16_t port=endpoint.port;if(endpoint.role==Role::listen_local)port=l.bridge->listen(context.grant,true);else l.bridge->connect(context.grant,port,true);begin_ready(context,resource->second.request,port,cancel);}catch(const std::exception& e){native_failure(context,e.what());}
            });
        });
    }
    void prepare_request(Link& l,Context context,rpc::Open request){
        auto cancel=std::make_shared<std::atomic<bool>>(false);l.pending_contexts.emplace(context.request,context);l.pending.emplace(context.request,request);l.pending_native.emplace(context.request,cancel);l.requests[context.request]=Clock::now();
        try{native_post([this,context,cancel,request=std::move(request)]()mutable{
            if(*cancel)return;std::string failure;try{if(hooks.prepare_request)hooks.prepare_request(context,request);}catch(const std::exception& e){failure=e.what();}catch(...){failure="Native receiver preparation failed";}
            complete([this,context,cancel,request=std::move(request),failure=std::move(failure)]()mutable{
                if(*cancel)return;auto f=links.find(context.connection);if(f==links.end())return;auto& l=*f->second;auto pending=l.pending_native.find(context.request);if(pending==l.pending_native.end()||pending->second!=cancel)return;
                try{if(!failure.empty())throw std::runtime_error(failure);auto payload=rpc::encode(request);l.pending.at(context.request)=std::move(request);l.requests[context.request]=Clock::now();l.connection->queue({rpc::Operation::open,context.request,std::move(payload)});}catch(const std::exception& e){cancel_request(l,context.request);retry_done(context.connection,context.request,false);event({EventKind::error,context,0,e.what()});}
            });
        });}catch(...){cancel_request(l,context.request);throw;}
    }
    Token next(){auto id=sequence.fetch_add(1);need(id&&id<UINT64_MAX,"Peer request sequence exhausted");return id;}
    void post(std::function<void()> fn){std::lock_guard lock(mutex);need(!ending&&commands.size()<256,"Peer service command queue full or stopped");commands.push_back(std::move(fn));wake.notify_one();}
    void event(Event e){std::lock_guard lock(mutex);if(events.size()>=1024){ending=true;wake.notify_all();return;}events.push_back(std::move(e));}
    dm::peer_auth::Lookup lookup(){return [this](const DeviceId& id)->std::optional<dm::peer_auth::Secret>{auto key=store.secret(id);if(!key)return {};dm::peer_auth::Secret result;result.bytes=*key;return result;};}
    dm::peer_auth::Lookup revocation_lookup(){return [this](const DeviceId& id)->std::optional<dm::peer_auth::Secret>{auto key=store.secret(id);if(!key)key=store.revocation_secret(id);if(!key)return {};dm::peer_auth::Secret result;result.bytes=*key;return result;};}
    void revoke_local(const DeviceId& peer){
        gateway_routes.erase(peer);suppressed_routes.erase(peer);
        std::vector<Token> retrying;for(const auto& [token,plan]:retries)if(plan.route.expected_peer==peer)retrying.push_back(token);
        for(auto token:retrying){retries.erase(token);cancelled.insert(token);event({EventKind::disconnected,{token},0,"Device was forgotten"});}
        revoked(policy.forget(peer));std::vector<Token> ids;for(const auto& [id,l]:links)if(l->connection->remote_id()==peer)ids.push_back(id);for(auto id:ids)drop(id,"Device was forgotten");event({EventKind::trust_changed,{}});
    }
    void receive_revocation(AuthenticatedConnection& revocation){
                    const auto peer=revocation.remote_id();const auto request=revocation.receive(deadline());
                    need(request.operation==rpc::Operation::forget&&request.id&&request.payload.empty()&&revocation.revocation_secret().has_value(),"Revocation connection only accepts Forget");
                    const auto key=revocation.revocation_secret()->bytes;
                    auto completed=std::make_shared<std::promise<bool>>();auto completion=completed->get_future();
                    post([this,peer,key,completed]{try{std::lock_guard lock(trust_mutex);const bool wasTrusted=store.secret(peer).has_value();const auto pending=store.pending_forgets();const bool wasPending=std::any_of(pending.begin(),pending.end(),[&](const auto& item){return item.id==peer;});need(store.receive_forget(peer,key),"Revocation credential changed");if(wasTrusted){++epoch;revoke_local(peer);}else if(wasPending)event({EventKind::trust_changed,{}});completed->set_value(true);}catch(...){completed->set_value(false);}});
                    need(completion.wait_until(deadline())==std::future_status::ready&&completion.get(),"Revocation did not complete");
                    revocation.send({rpc::Operation::forget_ack,request.id,{}},deadline());
    }
    void refresh_forgets(){
        if(now()<last_forget_poll+250)return;last_forget_poll=now();
        unsigned running=0;
        for(auto& [id,attempt]:forget_attempts)if(attempt.job.valid()){
            if(attempt.job.wait_for(std::chrono::seconds(0))!=std::future_status::ready){++running;continue;}
            const bool complete=attempt.job.get();attempt.due=now()+60000;
            if(complete)event({EventKind::trust_changed,{},0,"Device forgotten on both computers"});
        }
        const auto pending=store.pending_forgets();
        const auto isPending=[&](const DeviceId& id){return std::any_of(pending.begin(),pending.end(),[&](const auto& item){return item.id==id;});};
        std::erase_if(forget_attempts,[&](const auto& item){return !item.second.job.valid()&&!isPending(item.first);});
        std::erase_if(forget_routes,[&](const auto& item){return !isPending(item.first);});
        for(const auto& peer:pending){
            auto& attempt=forget_attempts[peer.id];if(attempt.job.valid()||now()<attempt.due||running>=2)continue;
            auto route=forget_routes.find(peer.id);const auto address=route==forget_routes.end()?peer.address_hint:route->second.first;const auto port=route==forget_routes.end()?peer.port_hint:route->second.second;
            if(address.empty()||!port){auto route=endpoint_routes.find(peer.id);if(route!=endpoint_routes.end()&&links.contains(route->second.via)&&!route->second.pending){attempt.due=now()+60000;try{begin_endpoint(next(),route->second.via,peer.id,{},true);}catch(...){} }continue;}
            auto key=store.revocation_secret(peer.id);if(!key)continue;++running;attempt.peer=peer.id;
            attempt.job=std::async(std::launch::async,[this,id=peer.id,address,port,key=*key]{try{
                dm::peer_auth::ClientOptions proof;proof.local=store.identity();proof.expected_peer=id;proof.mode=dm::peer_auth::Mode::revocation;dm::peer_auth::Secret secret;secret.bytes=key;proof.remembered=secret;
                auto connection=AuthenticatedConnection::client(transport::Stream::connect(address,port,{},deadline()),proof,{},deadline());
                const auto request=next();connection.send({rpc::Operation::forget,request,{}},deadline());const auto response=connection.receive(deadline());
                need(response.operation==rpc::Operation::forget_ack&&response.id==request&&response.payload.empty(),"Invalid forget acknowledgement");
                return store.acknowledge_forget(id,key);
            }catch(...){return false;}});
        }
    }
    dm::peer_auth::Remember remember(std::string address={},std::uint16_t port=0,std::uint64_t generation=0){if(!generation)generation=epoch.load();return [this,address=std::move(address),port,generation](const DeviceId& id,const dm::peer_auth::Secret& key){std::lock_guard lock(trust_mutex);need(epoch==generation,"Pairing was cancelled");store.remember({id,"Paired device",address,port},key.bytes);};}
    void resolve_target(ConnectOptions& options){
        const auto normalized=[](std::string address){for(auto& c:address)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return address=="localhost"?std::string("127.0.0.1"):address;};
        const auto matches=[&](const RememberedDevice& device){return device.port_hint==options.port&&!device.address_hint.empty()&&normalized(device.address_hint)==normalized(options.address);};
        for(const auto& device:store.pending_forgets())need(device.id!=options.expected_peer&&!matches(device),"This device has a pending removal; finish or explicitly clear it before connecting");
        if(valid_id(options.expected_peer)&&store.secret(options.expected_peer)){options.open_mode=false;return;}
        std::optional<DeviceId> remembered;
        for(const auto& device:store.devices())if(matches(device)){
            need(!remembered||*remembered==device.id,"Several remembered devices use this address; select a saved device explicitly");remembered=device.id;
        }
        if(remembered){need(!valid_id(options.expected_peer)||options.expected_peer==*remembered,"This address belongs to a different remembered device");options.expected_peer=*remembered;options.open_mode=false;}
    }
    void launch_dial(Token token,ConnectOptions options,std::uint64_t generation){
        need(dialing.size()<4,"Too many pending peer connections");
        {std::lock_guard lock(trust_mutex);resolve_target(options);}
        const auto target=options.expected_peer;
        dialing.push_back(std::async(std::launch::async,[this,token,generation,options=std::move(options)]()mutable{
            try{dm::peer_auth::ClientOptions proof;
                {std::lock_guard lock(trust_mutex);need(epoch==generation,"Connection cancelled");resolve_target(options);proof.local=store.identity();proof.expected_peer=options.expected_peer;proof.code=options.code;proof.mode=options.open_mode?dm::peer_auth::Mode::open:dm::peer_auth::Mode::pair;
                if(valid_id(options.expected_peer)){auto key=store.secret(options.expected_peer);if(key){proof.mode=dm::peer_auth::Mode::remembered;dm::peer_auth::Secret secret;secret.bytes=*key;proof.remembered=secret;}}}
                auto connection=AuthenticatedConnection::client(transport::Stream::connect(options.address,options.port,options.local,deadline()),proof,remember(options.address,options.port,generation),deadline());
                ConnectOptions route{options.address,options.port,options.local,connection.remote_id(),{},false};
                return Result{token,false,std::make_unique<AuthenticatedConnection>(std::move(connection)),{},generation,std::move(route)};
            }catch(const transport::Error& e){return Result{token,false,{},e.what(),generation,{},e.reason==transport::ErrorReason::network||e.reason==transport::ErrorReason::timeout};}
            catch(const std::exception& e){return Result{token,false,{},e.what(),generation};}
        }));dial_targets[token]=target;
    }
    void route_failed(GatewayRoute& route,bool network){
        if(!network){route.identity_failed=true;route.due=now()+60000;return;}
        constexpr Milliseconds delays[]{1000,2000,4000,8000,30000,60000};
        const auto index=std::min<unsigned>(route.failures,5);if(route.failures<6)++route.failures;route.due=now()+delays[index];
    }
    void sync_routes(){
        for(auto device:store.devices())if(device.id!=store.identity()&&!device.address_hint.empty()&&device.port_hint){
            auto [entry,inserted]=gateway_routes.try_emplace(device.id);if(inserted)entry->second.device=std::move(device);entry->second.disabled=suppressed_routes.contains(entry->first);
        }
    }
    void refresh_routes(){
        if((!policy.gateway_enabled()&&!auto_connect_enabled)||dialing.size()>=4)return;
        for(auto& [id,route]:gateway_routes){
            if(route.disabled||route.identity_failed||route.pending||!store.secret(id))continue;
            if(std::any_of(dial_targets.begin(),dial_targets.end(),[&](const auto& dial){return dial.second==id;}))continue;
            if(std::any_of(retries.begin(),retries.end(),[&](const auto& retry){return retry.second.route.expected_peer==id;}))continue;
            bool connected=false;for(const auto& [token,link]:links)if(!link->incoming&&link->connection->identity().paired&&link->connection->remote_id()==id){connected=true;break;}
            if(connected){route.failures=0;route.due=0;route.attempted=false;continue;}
            if(route.attempted&&now()<route.due)continue;
            const auto token=next();route.pending=token;route.attempted=true;gateway_dials[token]=id;
            try{launch_dial(token,{route.device.address_hint,route.device.port_hint,"",id,"",false},epoch.load());}
            catch(...){gateway_dials.erase(token);route.pending=0;route_failed(route,true);}
            if(dialing.size()>=4)break;
        }
    }
    void retry_event(Token token,const RetryPlan& plan,const std::string& detail){event({EventKind::reconnecting,{token},0,detail+" (attempt "+std::to_string(plan.budget.attempts())+" of 5)"});}
    bool schedule_retry(Link& link,const std::string& reason){
        if(link.incoming||!link.route||!link.connection->identity().paired||!store.secret(link.connection->remote_id()))return false;
        auto found=retries.find(link.token);
        if(found==retries.end()){
            RetryPlan plan;plan.route=*link.route;plan.began=now();
            for(const auto& [grant,resource]:link.resources)if(!resource.relay&&resource.native_ready&&resource.context.kind!=Kind::usb){auto request=resource.request;request.native_credential.clear();plan.resources.push_back({std::move(request),resource.context.kind,grant});}
            if(plan.resources.empty())return false;found=retries.emplace(link.token,std::move(plan)).first;
        }
        auto& plan=found->second;plan.dialing=false;plan.catalog_request=0;plan.waiting.clear();for(auto& saved:plan.resources)saved.restoring_request=0;
        if(plan.resources.empty()||!plan.budget.failed(Failure::transport_lost,now())){retries.erase(found);return false;}
        retry_event(link.token,plan,"Connection lost; reconnecting");return true;
    }
    void retry_done(Token connection,Token request,bool success){
        auto found=retries.find(connection);if(found==retries.end()||!found->second.waiting.erase(request))return;
        if(!success){for(const auto& saved:found->second.resources)if(saved.restoring_request==request)event({EventKind::stopped,{connection,request,saved.previous_grant},0,"Reconnect ended"});std::erase_if(found->second.resources,[&](const auto& saved){return saved.restoring_request==request;});}
        if(success){auto live=links.find(connection);if(live!=links.end())for(const auto& [grant,resource]:live->second->resources)if(resource.context.request==request)for(auto& saved:found->second.resources)if(saved.restoring_request==request)saved.previous_grant=grant;}
        if(found->second.waiting.empty())retries.erase(found);
    }
    void retry_catalog(Link& l,Token request,const std::vector<Catalog>& catalogs){
        auto found=retries.find(l.token);if(found==retries.end()||found->second.catalog_request!=request)return;
        auto& plan=found->second;plan.catalog_request=0;std::vector<SavedResource> valid;
        for(auto saved:plan.resources){const Offer* offer=nullptr;for(const auto& catalog:catalogs)if(catalog.device==saved.request.owner)for(const auto& item:catalog.offers)if(item.id==saved.request.resource)offer=&item;
            if(!offer||!offer->available||offer->kind!=saved.kind||(saved.request.input&&!offer->input_allowed)){event({EventKind::stopped,{l.token,0,saved.previous_grant},0,"Reconnect ended"});event({EventKind::error,{l.token,0,saved.previous_grant},0,"Resource is no longer permitted after reconnect: "+saved.request.resource});}
            else valid.push_back(std::move(saved));
        }
        plan.resources=std::move(valid);l.used_resources=true;
        if(plan.resources.empty()){retries.erase(found);return;}
        // Register every restoration before callbacks can finish, retaining the
        // original resource set until all requests either restore or fail.
        for(auto& saved:plan.resources){saved.restoring_request=next();plan.waiting.insert(saved.restoring_request);}
        const auto saved=plan.resources;
        for(const auto& item:saved)try{Context context{l.token,item.restoring_request,0,l.connection->remote_id(),false,item.kind};prepare_request(l,context,item.request);}catch(const std::exception& e){retry_done(l.token,item.restoring_request,false);event({EventKind::error,{l.token,item.restoring_request},0,e.what()});}
    }
    void refresh_retries(){
        std::vector<Token> exhausted;
        for(auto& [token,plan]:retries){
            if(links.contains(token)||plan.dialing)continue;
            if(now()-plan.began>=30000||!store.secret(plan.route.expected_peer)){exhausted.push_back(token);continue;}
            if(!plan.budget.ready(now())||dialing.size()>=4)continue;
            try{plan.dialing=true;launch_dial(token,plan.route,epoch.load());}catch(const std::exception& e){plan.dialing=false;if(!plan.budget.failed(Failure::transport_lost,now()))exhausted.push_back(token);else retry_event(token,plan,e.what());}
        }
        for(auto token:exhausted){retries.erase(token);cancelled.insert(token);event({EventKind::disconnected,{token},0,"Automatic reconnect ended; connect again when the device is available"});}
    }
    Link& link(Token id){auto f=links.find(id);need(f!=links.end(),"Device connection is not active");return *f->second;}
    void stop_resource(Link& l,Token grant)noexcept{
        auto f=l.resources.find(grant);if(f==l.resources.end())return;
        auto resource=std::move(f->second);l.resources.erase(f);
        try{l.bridge->revoke(grant);}catch(...){}
        discard_deferred(l,grant);
        if(!resource.relay&&resource.context.incoming&&!resource.native_ready)try{l.connection->queue({rpc::Operation::rejected,resource.context.request,rpc::detail(resource.failure.empty()?"Resource preparation cancelled":resource.failure)});}catch(...){}
        if(resource.relay){
            if(l.incoming)policy.close(grant);
            if(l.incoming&&!resource.other_grant)for(auto& [request,pending]:relays)if(pending.visitor==l.token&&pending.grant==grant&&!pending.cancelled){pending.cancelled=true;try{l.connection->queue({rpc::Operation::rejected,pending.request,rpc::detail("Delegated resource was withdrawn")});}catch(...){}}
            auto other=links.find(resource.other_connection);
            if(other!=links.end()&&resource.other_grant){
                stop_resource(*other->second,resource.other_grant);
                try{other->second->connection->queue({other->second->incoming?rpc::Operation::stopped:rpc::Operation::close,resource.other_grant,{}});}catch(...){}
            }
        }else native_cleanup(resource.context,false,resource.native_cancel);
        try{event({EventKind::stopped,resource.context});}catch(...){}
    }
    void cancel_request(Link& l,Token request)noexcept{
        auto relay=relays.find(request);
        if(relay!=relays.end()){
            auto pending=relay->second;relays.erase(relay);auto visitor=links.find(pending.visitor);
            if(visitor!=links.end()){stop_resource(*visitor->second,pending.grant);if(!pending.cancelled)try{visitor->second->connection->queue({rpc::Operation::rejected,pending.request,rpc::detail("Provider request ended")});}catch(...){}}
            l.pending_contexts.erase(request);
        }else {auto f=l.pending_contexts.find(request);if(f!=l.pending_contexts.end()){auto native=l.pending_native.find(request);native_cleanup(f->second,true,native==l.pending_native.end()?NativeCancel{}:native->second);l.pending_contexts.erase(f);}}
        l.pending.erase(request);l.pending_native.erase(request);l.requests.erase(request);
    }
    void end_pending(Link& l,Token request,const std::string& reason){
        if(!l.pending.contains(request))return;
        if(l.requests.contains(request)){need(l.abandoned.size()<256,"Cancelled request budget exhausted");l.abandoned.insert(request);}
        if(l.request_cancel_supported&&!l.incoming)l.connection->queue({rpc::Operation::cancel_request,request,{}});
        cancel_request(l,request);retry_done(l.token,request,false);
        event({EventKind::stopped,{l.token,request},0,reason});
    }
    void drop(Token id,std::string reason={},bool transient=false){auto f=links.find(id);if(f==links.end())return;auto& l=*f->second;const auto parent=l.via_connection,parentGrant=l.via_grant;l.via_connection=0;
        while(!l.tunnels.empty())close_tunnel(l,l.tunnels.begin()->first);
        for(auto it=endpoint_pending.begin();it!=endpoint_pending.end();)if(it->second.connection==id||it->second.visitor==id){if(!it->second.visitor){endpoint_routes[it->second.owner].pending=0;event({EventKind::error,{it->first},0,"Gateway disconnected"});}retire_endpoint(it->first,it->second.connection);it=endpoint_pending.erase(it);}else ++it;
        const bool retry=!parent&&transient&&schedule_retry(l,reason);if(!retry)retries.erase(id);l.connection->cancel();if(l.dispatcher)l.dispatcher->close();while(!l.resources.empty())stop_resource(l,l.resources.begin()->first);while(!l.pending_contexts.empty()){auto context=l.pending_contexts.begin()->second;cancel_request(l,context.request);event({EventKind::stopped,context,0,"Connection ended before resource was ready"});}Context c{id,0,0,l.connection->remote_id(),l.incoming};const bool wasProvider=!l.incoming;links.erase(f);if(wasProvider)revoked(policy.remote_removed(c.peer));if(!retry)event({EventKind::disconnected,c,0,std::move(reason)});if(parent){auto outer=links.find(parent);if(outer!=links.end())close_tunnel(*outer->second,parentGrant);}}

    void granted(Link& l,Token grant,std::uint16_t port){const auto& resource=l.resources.at(grant);auto public_request=resource.request;public_request.native_credential.clear();Token previous=0;auto retry=retries.find(l.token);if(retry!=retries.end())for(const auto& saved:retry->second.resources)if(saved.restoring_request==resource.context.request)previous=saved.previous_grant;
        event({EventKind::granted,resource.context,port,{},{},l.connection->identity().paired,std::move(public_request),previous});retry_done(l.token,resource.context.request,true);}
    void add(Result result){
        dial_targets.erase(result.token);endpoint_inflight.erase(result.token);
        if(result.via_connection){
            auto parent=links.find(result.via_connection);if(parent==links.end()||!parent->second->tunnels.contains(result.via_grant)){for(auto& [owner,route]:endpoint_routes)if(route.pending==result.token)route.pending=0;if(!result.incoming)event({EventKind::error,{result.token},0,"Gateway disconnected before endpoint authentication completed"});cancelled.erase(result.token);return;}
            for(auto& [owner,route]:endpoint_routes)if(route.pending==result.token)route.pending=0;
            if(result.revocation_complete){if(result.incoming){auto& tunnel=parent->second->tunnels.at(result.via_grant);tunnel.inner_token=0;tunnel.expires=Clock::now()+std::chrono::seconds(5);}else{close_tunnel(*parent->second,result.via_grant);event({EventKind::trust_changed,{},0,"Device forgotten on both computers"});}return;}
            if(!result.connection||result.epoch!=epoch||cancelled.contains(result.token)){close_tunnel(*parent->second,result.via_grant);if(!result.incoming)event({EventKind::error,{result.token},0,result.error.empty()?"Endpoint connection cancelled":result.error});cancelled.erase(result.token);return;}
        }
        auto retry=retries.find(result.token);
        if(retry!=retries.end()){
            retry->second.dialing=false;
            if(result.epoch!=epoch||cancelled.contains(result.token)){retries.erase(retry);}
            else if(!result.connection){
                if(result.transient&&retry->second.budget.failed(Failure::transport_lost,now()))retry_event(result.token,retry->second,"Device is unreachable; reconnecting");
                else{retries.erase(retry);event({EventKind::disconnected,{result.token},0,result.error});}return;
            }
        }
        const auto background=gateway_dials.find(result.token);const bool automaticDial=background!=gateway_dials.end();
        if(background!=gateway_dials.end()){
            const auto id=background->second;gateway_dials.erase(background);auto route=gateway_routes.find(id);
            if(route!=gateway_routes.end()){route->second.pending=0;if(!result.connection){
                // Retry network reachability only, never rejected identity/code proofs.
                const bool network=result.transient;
                route_failed(route->second,network);
            }}
            if((!policy.gateway_enabled()&&!auto_connect_enabled)||route==gateway_routes.end()||route->second.disabled){cancelled.erase(result.token);return;}
            if(!result.connection){cancelled.erase(result.token);return;}
        }
        // Commit admission against current trust, after the network handshake.
        // A new pairing may have completed while an open handshake was in flight.
        std::lock_guard admission(trust_mutex);
        if(result.connection&&!result.incoming&&!result.connection->identity().paired){try{
            need(!store.secret(result.connection->remote_id()),"Device became paired during connection; reconnect using remembered authentication");
            if(result.route){auto route=*result.route;route.open_mode=true;resolve_target(route);need(route.open_mode,"Device became paired during connection; reconnect using remembered authentication");}
            for(const auto& peer:store.pending_forgets())need(peer.id!=result.connection->remote_id(),"Device removal is pending");
        }catch(const std::exception& e){event({EventKind::error,{result.token},0,e.what()});return;}}
        if(!automaticDial&&result.connection){std::vector<Token> duplicates;for(const auto& [id,link]:links)if(link->automatic_presence&&!link->incoming&&link->resources.empty()&&link->pending.empty()&&link->connection->remote_id()==result.connection->remote_id())duplicates.push_back(id);for(auto id:duplicates)drop(id,"Replaced by explicit connection");}
        if(automaticDial&&result.connection&&std::any_of(links.begin(),links.end(),[&](const auto& item){return !item.second->incoming&&item.second->connection->remote_id()==result.connection->remote_id();}))return;
        if(result.epoch!=epoch||cancelled.erase(result.token)){event({EventKind::disconnected,{result.token},0,"Connection cancelled"});return;}if(!result.connection){event({EventKind::error,{result.token},0,result.error});return;}need(!result.connection->revocation_only(),"Revocation connections cannot access resources");need(links.size()<64,"Peer service connection budget exhausted");auto object=std::make_unique<Link>(result.token,result.incoming,std::move(result.connection));auto& l=*object;l.automatic_presence=automaticDial;l.via_connection=result.via_connection;l.via_grant=result.via_grant;l.route=std::move(result.route);const auto identity=l.connection->identity();if(identity.paired&&identity.device!=store.identity()){need(store.secret(identity.device).has_value(),"Device was forgotten during authentication");policy.remember(identity.device);if(l.incoming){auto route=gateway_routes.find(identity.device);if(route!=gateway_routes.end()){route->second.identity_failed=false;route->second.failures=0;route->second.due=0;}}}
        l.bridge=std::make_unique<MediaBridge>(!l.incoming,[&l](Token grant){return l.resources.contains(grant)||l.tunnels.contains(grant);},[&l](const rpc::Message& m){l.connection->queue(m);});
        Adapter adapter;
        adapter.start=[this,&l](const Grant& grant,const rpc::Open& request,std::string& reason){if(!l.incoming||grant.delegated){reason="Delegated relay is not available";return false;}if(l.tunnels.contains(grant.id)){reason="Resource grant conflicts with endpoint tunnel";return false;}Resource r{{l.token,l.current_request,grant.id,l.connection->remote_id(),true,grant.kind},request};l.resources.emplace(grant.id,std::move(r));try{prepare(l,l.resources.at(grant.id));return true;}catch(const std::exception& e){reason=e.what();return false;}};
        adapter.stop=[this,&l](const Grant& g){stop_resource(l,g.id);};
        adapter.credential=[&l](const Grant& g){return l.resources.at(g.id).request.native_credential;};
        l.dispatcher=std::make_unique<SessionDispatcher>(policy,*l.connection,std::move(adapter));Context c{l.token,0,0,identity.device,l.incoming};links.emplace(l.token,std::move(object));l.connection->queue({rpc::Operation::ping,rpc::cancel_probe,{}});if(!l.via_connection)l.connection->queue({rpc::Operation::ping,rpc::endpoint_probe,{}});
        Event connected{EventKind::connected,c,0,{},{},identity.paired};connected.via_connection=l.via_connection;event(std::move(connected));
        if(l.via_connection&&identity.paired){endpoint_routes[identity.device]={l.via_connection,0,0};if(!l.incoming){const auto request=next();l.catalogs.insert(request);l.requests[request]=Clock::now();l.connection->queue({rpc::Operation::catalog,request,{}});}}if(identity.paired){if(!l.incoming&&l.route)for(auto device:store.devices())if(device.id==identity.device){device.address_hint=l.route->address;device.port_hint=l.route->port;store.update_hint(device);auto route=gateway_routes.find(device.id);if(route!=gateway_routes.end())route->second.device=device;break;}sync_routes();event({EventKind::trust_changed,c});}
        if(auto_connect_enabled&&!l.incoming&&identity.paired&&automaticDial){const auto request=next();l.catalogs.insert(request);l.requests[request]=Clock::now();l.connection->queue({rpc::Operation::catalog,request,{}});}
        auto restoring=retries.find(l.token);if(restoring!=retries.end()){const auto request=next();restoring->second.catalog_request=request;l.catalogs.insert(request);l.requests[request]=Clock::now();l.connection->queue({rpc::Operation::catalog,request,{}});}
    }
    void revoked(const std::vector<Grant>& grants){
        for(auto& [id,l]:links){
            for(auto& message:l->dispatcher->revoked(grants))l->connection->queue(message);
            for(const auto& grant:grants){auto f=l->resources.find(grant.id);if(l->incoming&&f!=l->resources.end()&&f->second.relay){stop_resource(*l,grant.id);l->connection->queue({rpc::Operation::stopped,grant.id,{}});}}
        }
    }
    void close_tunnel(Link& link,Token grant,bool notify=true){
        auto found=link.tunnels.find(grant);if(found==link.tunnels.end())return;auto tunnel=std::move(found->second);link.tunnels.erase(found);link.bridge->revoke(grant);discard_deferred(link,grant);
        if(tunnel.listener)tunnel.listener->cancel();
        if(notify)try{link.connection->queue({rpc::Operation::endpoint_close,grant,{}});}catch(...){}
        if(tunnel.other_connection){auto other=links.find(tunnel.other_connection);if(other!=links.end())close_tunnel(*other->second,tunnel.other_grant);}
        if(tunnel.inner_token&&links.contains(tunnel.inner_token))drop(tunnel.inner_token,"Endpoint tunnel closed");
        if(tunnel.inner_token&&endpoint_inflight.contains(tunnel.inner_token))cancelled.insert(tunnel.inner_token);
    }
    void begin_endpoint(Token token,Token gateway,const DeviceId& owner,std::string code,bool revocation=false){
        auto& link=this->link(gateway);need(!link.via_connection&&link.connection->identity().paired&&link.endpoint_supported,"This gateway does not support authenticated endpoint tunnels");
        need(valid_id(owner)&&owner!=store.identity()&&owner!=link.connection->remote_id(),"Invalid remote device");
        need(endpoint_pending.size()<32&&link.tunnels.size()<32,"Endpoint tunnel budget exhausted");
        if(!revocation)for(const auto& peer:store.pending_forgets())need(peer.id!=owner,"Endpoint removal is pending");
        need((revocation&&store.revocation_secret(owner).has_value())||store.secret(owner).has_value()||dm::access::validCode(code),"Enter the actual endpoint device's pairing code");
        endpoint_pending.emplace(token,EndpointPending{gateway,0,0,owner,std::move(code),Clock::now(),revocation});
        endpoint_routes[owner]={gateway,token,now()+60000};
        link.connection->queue({rpc::Operation::endpoint_open,token,Bytes(owner.begin(),owner.end())});
    }
    void endpoint_dial(Token token,Link& outer,Token grant,DeviceId owner,std::string code,bool revocation){
        need(dialing.size()<8,"Endpoint authentication budget exhausted");const auto generation=epoch.load();
        const auto port=outer.bridge->listen(grant);outer.tunnels.at(grant).inner_token=token;endpoint_inflight.insert(token);const auto parent=outer.token;
        dialing.push_back(std::async(std::launch::async,[this,token,parent,grant,generation,port,owner,revocation,code=std::move(code)]()mutable{
            try{dm::peer_auth::ClientOptions proof;{std::lock_guard lock(trust_mutex);need(epoch==generation,"Endpoint authentication cancelled");if(!revocation)for(const auto& peer:store.pending_forgets())need(peer.id!=owner,"Endpoint removal is pending");proof.local=store.identity();proof.expected_peer=owner;proof.code=std::move(code);proof.mode=dm::peer_auth::Mode::pair;if(auto key=revocation?store.revocation_secret(owner):store.secret(owner)){proof.mode=revocation?dm::peer_auth::Mode::revocation:dm::peer_auth::Mode::remembered;dm::peer_auth::Secret secret;secret.bytes=*key;proof.remembered=secret;}}
                auto connection=AuthenticatedConnection::client(transport::Stream::connect("127.0.0.1",port,{},deadline()),proof,remember({},0,generation),Clock::now()+std::chrono::seconds(15));
                need(connection.identity().paired&&connection.remote_id()==owner,"Endpoint identity verification failed");
                if(revocation){const auto request=next();connection.send({rpc::Operation::forget,request,{}},deadline());const auto response=connection.receive(deadline());need(response.operation==rpc::Operation::forget_ack&&response.id==request,"Invalid endpoint forget acknowledgement");need(proof.remembered&&store.acknowledge_forget(owner,proof.remembered->bytes),"Endpoint revocation credential changed");return Result{token,false,{}, {},generation,{},false,parent,grant,true};}
                return Result{token,false,std::make_unique<AuthenticatedConnection>(std::move(connection)),{},generation,{},false,parent,grant};
            }catch(const std::exception& e){return Result{token,false,{},e.what(),generation,{},false,parent,grant};}
        }));dial_targets[token]=owner;
    }
    void endpoint_message(Link& link,const rpc::Message& message){using O=rpc::Operation;
        need(!link.via_connection&&link.connection->identity().paired&&link.endpoint_supported,"Endpoint tunnels require a directly paired upgraded peer");
        if(message.operation==O::endpoint_close){close_tunnel(link,message.id,false);return;}
        if(message.operation==O::endpoint_cancel){
            for(auto it=endpoint_pending.begin();it!=endpoint_pending.end();)if(it->second.visitor==link.token&&it->second.visitor_request==message.id){retire_endpoint(it->first,it->second.connection);it=endpoint_pending.erase(it);}else ++it;
            std::vector<Token> grants;for(const auto& [grant,tunnel]:link.tunnels)if(tunnel.request==message.id)grants.push_back(grant);for(auto grant:grants)close_tunnel(link,grant);return;
        }
        if(message.operation==O::endpoint_open||message.operation==O::endpoint_forward){
            try{
                DeviceId owner{};std::copy(message.payload.begin(),message.payload.end(),owner.begin());need(valid_id(owner)&&owner!=store.identity(),"Invalid tunnel endpoint identity");need(link.tunnels.size()<32&&endpoint_pending.size()<32,"Endpoint tunnel budget exhausted");
                if(message.operation==O::endpoint_open){
                    need(policy.gateway_enabled(),"Gateway sharing is disabled");Link* provider=nullptr;
                    for(auto& [id,candidate]:links)if(!candidate->via_connection&&candidate->connection->identity().paired&&candidate->endpoint_supported&&candidate->connection->remote_id()==owner){provider=candidate.get();break;}
                    need(provider&&provider->token!=link.token&&store.secret(owner).has_value(),"Direct paired endpoint is unavailable");
                    const auto request=next();endpoint_pending.emplace(request,EndpointPending{provider->token,link.token,message.id,owner,{}});
                    const auto visitor=link.connection->remote_id();provider->connection->queue({O::endpoint_forward,request,Bytes(visitor.begin(),visitor.end())});return;
                }
                need(dialing.size()<8,"Endpoint authentication budget exhausted");
                // Opposite directions own disjoint tunnel IDs on the same control link.
                const auto grant=(next()<<1)|(Token{1}<<63)|Token(link.incoming),inner=next(),parent=link.token,generation=epoch.load();auto listener=std::make_shared<transport::Listener>("127.0.0.1",0);
                link.tunnels.emplace(grant,Tunnel{0,0,inner,listener});link.tunnels.at(grant).request=message.id;link.bridge->connect(grant,listener->port());endpoint_inflight.insert(inner);
                dialing.push_back(std::async(std::launch::async,[this,listener,inner,parent,grant,generation,owner]{try{
                    auto checkIdentity=[this,owner](const DeviceId& peer){need(peer==owner,"Endpoint visitor identity mismatch");return lookup()(peer);};
                    auto rememberEndpoint=[this,owner,generation](const DeviceId& peer,const dm::peer_auth::Secret& key){need(peer==owner,"Endpoint visitor identity mismatch");remember({},0,generation)(peer,key);};
                    auto revokeIdentity=[this,owner](const DeviceId& peer){need(peer==owner,"Endpoint visitor identity mismatch");return revocation_lookup()(peer);};
                    auto connection=AuthenticatedConnection::server(listener->accept(Clock::now()+std::chrono::seconds(15)),auth,checkIdentity,rememberEndpoint,Clock::now()+std::chrono::seconds(15),revokeIdentity);
                    if(connection.revocation_only()){receive_revocation(connection);return Result{inner,true,{}, {},generation,{},false,parent,grant,true};}
                    need(connection.identity().paired&&!connection.revocation_only()&&connection.remote_id()==owner,"Endpoint authentication did not match requesting device");
                    return Result{inner,true,std::make_unique<AuthenticatedConnection>(std::move(connection)),{},generation,{},false,parent,grant};
                }catch(const std::exception& e){return Result{inner,true,{},e.what(),generation,{},false,parent,grant};}}));
                link.connection->queue({O::endpoint_ready,message.id,rpc::integer(grant)});return;
            }catch(const std::exception& e){link.connection->queue({O::endpoint_rejected,message.id,rpc::detail(e.what())});return;}
        }
        auto found=endpoint_pending.find(message.id);if(found==endpoint_pending.end()&&retired_endpoints.contains(message.id)&&retired_endpoints.at(message.id)==link.token){if(message.operation==O::endpoint_ready)link.connection->queue({O::endpoint_close,rpc::integer(message.payload),{}});return;}need(found!=endpoint_pending.end()&&found->second.connection==link.token,"Unknown endpoint tunnel response");auto pending=std::move(found->second);endpoint_pending.erase(found);
        if(message.operation==O::endpoint_rejected){
            if(pending.visitor){auto visitor=links.find(pending.visitor);if(visitor!=links.end())visitor->second->connection->queue({O::endpoint_rejected,pending.visitor_request,message.payload});}
            else{endpoint_routes[pending.owner].pending=0;event({EventKind::error,{message.id,0,0,pending.owner},0,rpc::detail(message.payload)});}return;
        }
        need(message.operation==O::endpoint_ready,"Invalid endpoint response");const auto grant=rpc::integer(message.payload);need((grant&(Token{1}<<63))&&bool(grant&1)!=link.incoming&&!link.tunnels.contains(grant)&&!link.resources.contains(grant),"Invalid or duplicate endpoint grant");link.tunnels.emplace(grant,Tunnel{});
        if(pending.visitor){auto visitor=links.find(pending.visitor);if(visitor==links.end()||!policy.gateway_enabled()){close_tunnel(link,grant);return;}
            auto& target=*visitor->second;const auto visitorGrant=(next()<<1)|(Token{1}<<63)|Token(target.incoming);target.tunnels.emplace(visitorGrant,Tunnel{link.token,grant});target.tunnels.at(visitorGrant).request=pending.visitor_request;link.tunnels.at(grant)={target.token,visitorGrant};
            const auto port=link.bridge->listen(grant);target.bridge->connect(visitorGrant,port);target.connection->queue({O::endpoint_ready,pending.visitor_request,rpc::integer(visitorGrant)});
        }else{
            if(cancelled.contains(message.id)){close_tunnel(link,grant);endpoint_routes[pending.owner].pending=0;return;}
            try{endpoint_dial(message.id,link,grant,pending.owner,std::move(pending.code),pending.revocation);}catch(const std::exception& e){close_tunnel(link,grant);endpoint_routes[pending.owner].pending=0;event({EventKind::error,{message.id},0,e.what()});}
        }
    }
    void refresh_endpoints(){
        for(auto& [id,link]:links){std::vector<Token> expired;for(const auto& [grant,tunnel]:link->tunnels)if(Clock::now()>tunnel.expires)expired.push_back(grant);for(auto grant:expired)close_tunnel(*link,grant);}
        const auto pendingRemoval=store.pending_forgets();
        const auto removing=[&](const DeviceId& owner){return std::any_of(pendingRemoval.begin(),pendingRemoval.end(),[&](const auto& peer){return peer.id==owner;});};
        std::erase_if(endpoint_routes,[&](const auto& entry){return !entry.second.pending&&!store.secret(entry.first)&&!removing(entry.first);});
        // Only authenticated direct-gateway catalogs can supply an endpoint route.
        for(const auto& [token,link]:links)if(!link->via_connection&&link->connection->identity().paired&&link->endpoint_supported)for(const auto& catalog:link->catalog_cache)if(catalog.device!=link->connection->remote_id()&&catalog.device!=store.identity()&&(store.secret(catalog.device)||removing(catalog.device))){auto [route,inserted]=endpoint_routes.try_emplace(catalog.device,EndpointRoute{token});if(!links.contains(route->second.via)){route->second.via=token;route->second.pending=0;route->second.due=0;}}
        std::vector<Token> expired;for(const auto& [id,pending]:endpoint_pending)if(Clock::now()-pending.began>std::chrono::seconds(20))expired.push_back(id);
        for(auto id:expired){auto pending=std::move(endpoint_pending.at(id));retire_endpoint(id,pending.connection);endpoint_pending.erase(id);if(!pending.visitor){endpoint_routes[pending.owner].pending=0;event({EventKind::error,{id},0,"Endpoint tunnel request timed out"});}}
        if(!auto_connect_enabled)return;
        for(auto& [owner,route]:endpoint_routes){
            if(route.pending||now()<route.due||!store.secret(owner)||!links.contains(route.via)||dialing.size()>=8)continue;
            bool connected=false;for(const auto& [token,link]:links)if(!link->incoming&&link->connection->identity().paired&&link->connection->remote_id()==owner){connected=true;break;}if(connected)continue;
            route.due=now()+60000;try{begin_endpoint(next(),route.via,owner,{});}catch(...){}
        }

    }
    void message(Link& l,rpc::Message m){using O=rpc::Operation;l.activity=Clock::now();
        if(m.operation==O::catalog_reply||m.operation==O::granted||m.operation==O::rejected)l.requests.erase(m.id);
        if(m.operation>=O::endpoint_open&&m.operation<=O::endpoint_cancel){endpoint_message(l,m);return;}
        if(m.operation>=O::stream_open&&m.operation<=O::stream_close){stream_message(l,m);return;}
        if(m.operation==O::catalog_reply){need(l.catalogs.erase(m.id)>0,"Unsolicited catalog response");auto catalogs=rpc::decode_catalogs(m.payload);for(const auto& c:catalogs){need(c.device==l.connection->remote_id()||l.connection->identity().paired,"Unverified delegated catalog rejected");if(policy.gateway_enabled()&&!l.incoming&&l.connection->identity().paired&&c.device!=store.identity()&&c.device==l.connection->remote_id())revoked(policy.remote(l.connection->identity(),c,0,now()));}const bool unchanged=l.automatic_catalogs.erase(m.id)>0&&rpc::encode_catalogs(l.catalog_cache)==m.payload;l.catalog_cache=catalogs;retry_catalog(l,m.id,catalogs);if(unchanged)return;event({EventKind::catalog,{l.token,m.id,0,l.connection->remote_id(),l.incoming},0,{},std::move(catalogs)});return;}
        if(m.operation==O::granted&&l.abandoned.erase(m.id)){auto grant=rpc::decode_granted(m.payload);discard_deferred(l,grant.grant);l.connection->queue({O::close,grant.grant,{}});return;}
        if(m.operation==O::rejected&&l.abandoned.erase(m.id))return;
        if(m.operation==O::granted){auto p=l.pending.find(m.id);need(!l.incoming&&p!=l.pending.end(),"Unsolicited resource grant");auto grant=rpc::decode_granted(m.payload);need(!l.resources.contains(grant.grant)&&!l.tunnels.contains(grant.grant),"Duplicate resource grant");auto context=l.pending_contexts.at(m.id);context.grant=grant.grant;Resource r{context,std::move(p->second)};auto native=l.pending_native.find(m.id);if(native!=l.pending_native.end()){r.native_cancel=native->second;l.pending_native.erase(native);}l.pending.erase(p);l.pending_contexts.erase(m.id);if(!grant.native_credential.empty())r.request.native_credential=grant.native_credential;l.resources.emplace(grant.grant,std::move(r));try{prepare(l,l.resources.at(grant.grant));}catch(const std::exception& e){native_failure(context,e.what());}return;}
        if(m.operation==O::rejected){cancel_request(l,m.id);retry_done(l.token,m.id,false);l.catalogs.erase(m.id);l.automatic_catalogs.erase(m.id);event({EventKind::error,{l.token,m.id,0,l.connection->remote_id(),l.incoming},0,rpc::detail(m.payload)});return;}
        if(m.operation==O::stopped){stop_resource(l,m.id);return;}
        if(m.operation==O::ping){if(m.id==rpc::catalog_changed&&!l.incoming&&l.connection->identity().paired)l.catalog_dirty=true;l.connection->queue({O::pong,m.id==rpc::cancel_probe?rpc::cancel_ack:(!l.via_connection&&m.id==rpc::endpoint_probe?rpc::endpoint_ack:m.id),{}});return;}
        if(m.operation==O::pong){if(m.id==rpc::cancel_ack)l.request_cancel_supported=true;if(!l.via_connection&&m.id==rpc::endpoint_ack)l.endpoint_supported=true;return;}
        if(m.operation==O::cancel_request){
            need(l.incoming,"Only the requester can cancel resource preparation");std::vector<Token> grants;
            for(const auto& [id,resource]:l.resources)if(resource.context.request==m.id)grants.push_back(id);
            if(grants.empty()){l.connection->queue({O::rejected,m.id,rpc::detail("Resource preparation cancelled")});return;}
            for(auto grant:grants){auto found=l.resources.find(grant);if(found==l.resources.end())continue;if(found->second.relay){stop_resource(l,grant);continue;}l.connection->queue(l.dispatcher->handle({O::close,grant,{}},now()));}return;
        }
        if(m.operation==O::open&&!l.incoming){l.connection->queue({O::rejected,m.id,rpc::detail("Open a separate connection to request this device's resources")});return;}
        if(m.operation==O::open){auto request=rpc::decode_open(m.payload);if(request.owner!=store.identity()){l.connection->queue({O::rejected,m.id,rpc::detail("Pair with the actual endpoint through an authenticated tunnel first")});return;}}
        if(m.operation==O::close&&l.incoming){auto resource=l.resources.find(m.id);if(resource!=l.resources.end()&&resource->second.relay){stop_resource(l,m.id);l.connection->queue({O::stopped,m.id,{}});return;}}
        if(m.operation==O::open)for(const auto& [grant,resource]:l.resources)need(resource.context.request!=m.id,"Duplicate resource request identity");
        l.current_request=m.id;auto response=l.dispatcher->handle(m,now());if(m.operation!=O::open||response.operation!=O::granted)l.connection->queue(response);
    }
    void run(){while(!ending){std::deque<std::function<void()>> completions;{std::lock_guard lock(native_mutex);completions.swap(native_completions);}for(auto& done:completions)try{done();}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}
        std::deque<std::function<void()>> work;{std::lock_guard lock(mutex);work.swap(commands);}for(auto& fn:work)try{fn();}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}
        for(auto i=dialing.begin();i!=dialing.end();){if(i->wait_for(std::chrono::seconds(0))!=std::future_status::ready){++i;continue;}try{add(i->get());}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}i=dialing.erase(i);}
        std::vector<std::tuple<Token,std::string,bool>> dead;bool progress=false;
        for(auto& [id,l]:links)try{for(const auto& [request,began]:l->requests)need(Clock::now()-began<std::chrono::seconds(l->pending.contains(request)?60:15),"Peer request timed out");for(unsigned turn=0;turn<16;++turn){bool moved=l->connection->pump(deadline());while(auto m=l->connection->next()){message(*l,std::move(*m));moved=true;}if(!l->connection->pending_output())moved=l->bridge->pump()||moved;progress=progress||moved;if(!moved)break;}if(Clock::now()-l->activity>std::chrono::seconds(30))throw transport::Error(transport::ErrorReason::timeout,"Device connection timed out");if(Clock::now()-l->last_ping>std::chrono::seconds(10)){l->connection->queue({rpc::Operation::ping,next(),{}});l->last_ping=Clock::now();}}catch(const transport::Error& e){dead.emplace_back(id,e.what(),e.reason==transport::ErrorReason::network||e.reason==transport::ErrorReason::timeout);}catch(const std::exception& e){dead.emplace_back(id,e.what(),false);}
        for(auto& [id,reason,retry]:dead)drop(id,std::move(reason),retry);
        try{refresh_forgets();revoked(policy.expire(now()));for(auto& [id,l]:links)if(!l->incoming&&l->connection->identity().paired&&l->catalogs.empty()&&((l->catalog_dirty&&Clock::now()-l->last_catalog>std::chrono::milliseconds(250))||((policy.gateway_enabled()||auto_connect_enabled)&&Clock::now()-l->last_catalog>std::chrono::seconds(10)))){const auto request=next();l->catalogs.insert(request);l->automatic_catalogs.insert(request);l->requests[request]=Clock::now();l->connection->queue({rpc::Operation::catalog,request,{}});l->last_catalog=Clock::now();l->catalog_dirty=false;}}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}
        if(Clock::now()-last_routes>=std::chrono::milliseconds(100)){last_routes=Clock::now();try{refresh_routes();refresh_retries();refresh_endpoints();}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}}
        for(auto& [id,l]:links){std::vector<Context> timedout;for(const auto& [grant,r]:l->resources)if(!r.relay&&!r.native_ready&&Clock::now()-r.native_started>std::chrono::seconds(60))timedout.push_back(r.context);for(const auto& context:timedout)native_failure(context,"Native resource preparation timed out");}
        if(hooks.poll&&!native_poll_pending&&Clock::now()-last_poll>=std::chrono::milliseconds(50)){
            last_poll=Clock::now();native_poll_pending=true;
            try{native_post([this]{std::vector<Context> ended;std::string failure;try{ended=hooks.poll();}catch(const std::exception& e){failure=e.what();}catch(...){failure="Native resource status failed";}
                complete([this,ended=std::move(ended),failure=std::move(failure)]{native_poll_pending=false;if(!failure.empty()){event({EventKind::error,{},0,failure});return;}for(const auto& context:ended){auto f=links.find(context.connection);if(f==links.end())continue;auto& l=*f->second;if(!context.grant){end_pending(l,context.request,"Native preparation ended");continue;}if(!l.resources.contains(context.grant))continue;if(!context.failure_detail.empty()){native_failure(context,context.failure_detail);continue;}if(l.incoming)l.connection->queue(l.dispatcher->handle({rpc::Operation::close,context.grant,{}},now()));else{stop_resource(l,context.grant);l.connection->queue({rpc::Operation::close,context.grant,{}});}}});
            });}catch(const std::exception& e){native_poll_pending=false;event({EventKind::error,{},0,e.what()});}
        }
        if(progress)std::this_thread::yield();else {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(2),[this]{return ending||!commands.empty();});}
    }while(!links.empty())drop(links.begin()->first);retries.clear();for(auto& task:dialing)task.wait();dialing.clear();for(auto& [id,attempt]:forget_attempts)if(attempt.job.valid())attempt.job.wait();}
    Token start_listener(std::shared_ptr<transport::Listener> listener,std::string bind){
        auto& s=*this;s.listener_address=std::move(bind);
    const auto serial=s.listener_serial.load();
    s.post([&s,port=listener->port(),serial]{if(s.listener_serial!=serial)return;try{s.policy.sharing(true);Event event{EventKind::listening,{},port};event.sharing_generation=serial;s.event(std::move(event));}catch(const std::exception& e){Event event{EventKind::error,{},0,e.what()};event.sharing_generation=serial;s.event(std::move(event));}});
    s.listener=listener;s.accepting=true;auto done=std::make_shared<std::atomic<bool>>(false);s.acceptor_done=done;
    s.acceptor=std::thread([&s,listener,done,serial]{
        while(s.accepting&&s.listener_serial==serial){
            try{
                auto stream=listener->accept(deadline());const auto generation=s.epoch.load();
                auto connection=std::make_shared<std::unique_ptr<AuthenticatedConnection>>(std::make_unique<AuthenticatedConnection>(AuthenticatedConnection::server(std::move(stream),s.auth,s.lookup(),s.remember({},0,generation),deadline(),s.revocation_lookup())));
                if((*connection)->revocation_only()){
                    s.receive_revocation(**connection);continue;
                }
                auto token=s.next();s.post([&s,connection,token,generation,serial]{if(s.accepting&&s.listener_serial==serial)s.add({token,true,std::move(*connection),{},generation});});
            }catch(const std::exception&){/* Timeout/rejected handshake never enters application state. */}
        }
        *done=true;
    });
    return serial;
    }
    void reap_listeners(){for(auto i=retired.begin();i!=retired.end();)if(*i->done){i->thread.join();i=retired.erase(i);}else ++i;}
    void stop_listener(){accepting=false;++listener_serial;auto current=listener;if(current)current->cancel();if(acceptor.joinable())retired.push_back({std::move(acceptor),std::move(acceptor_done)});listener.reset();reap_listeners();}
};
Service::Service(Options options,Hooks hooks):state_(std::make_unique<State>(std::move(options),std::move(hooks))){}
Service::~Service(){auto& s=*state_;s.stop_listener();s.auth.stop();s.ending=true;s.wake.notify_all();if(s.worker.joinable())s.worker.join();{std::lock_guard lock(s.native_mutex);s.native_ending=true;}s.native_wake.notify_all();if(s.native_worker.joinable())s.native_worker.join();for(auto& retired:s.retired)if(retired.thread.joinable())retired.thread.join();}
DeviceId Service::identity()const{return state_->store.identity();}
std::string Service::pairing_code()const{return state_->auth.code();}
std::vector<RememberedDevice> Service::remembered()const{return state_->store.devices();}
std::vector<RememberedDevice> Service::pending_forgets()const{return state_->store.pending_forgets();}
Service::Token Service::start(std::string bind,std::uint16_t port){
    auto& s=*state_;std::lock_guard lifecycle(s.lifecycle_mutex);
    need(!s.listener,"Sharing listener is already running");s.reap_listeners();need(s.retired.size()<16,"Previous sharing listeners are still stopping");
    auto listener=std::make_shared<transport::Listener>(bind,port);
    return s.start_listener(std::move(listener),std::move(bind));
}
Service::ListenerChange Service::rebind(std::string bind,std::uint16_t port){
    auto& s=*state_;std::lock_guard lifecycle(s.lifecycle_mutex);
    const auto oldAddress=s.listener_address;const std::uint16_t oldPort=s.listener?s.listener->port():0;
    if(s.listener&&bind==oldAddress&&port==oldPort)return {s.listener_serial.load(),oldAddress,oldPort,{}};
    s.reap_listeners();need(s.retired.size()<16,"Previous sharing listeners are still stopping");
    std::shared_ptr<transport::Listener> next;std::string failure;
    try{next=std::make_shared<transport::Listener>(bind,port);}catch(const std::exception& e){failure=e.what();}
    if(!next){
        // Different ports/non-overlapping addresses can be tested without ever
        // closing the old listener. Only wildcard/specific same-port transitions
        // need a short listener-only interruption; existing streams stay alive.
        if(!s.listener||oldPort!=port||(oldAddress!="0.0.0.0"&&bind!="0.0.0.0"))return {s.listener_serial.load(),oldPort?oldAddress:std::string{},oldPort,failure};
        s.stop_listener();
        const auto until=Clock::now()+std::chrono::seconds(4);
        while(!s.retired.empty()&&Clock::now()<until){s.reap_listeners();if(!s.retired.empty())std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        try{next=std::make_shared<transport::Listener>(bind,port);}catch(const std::exception& e){
            failure=e.what();try{auto restored=std::make_shared<transport::Listener>(oldAddress,oldPort);const auto generation=s.start_listener(std::move(restored),oldAddress);return {generation,oldAddress,oldPort,failure};}
            catch(const std::exception& restore){return {s.listener_serial.load(),{},0,failure+"; previous listener could not be restored: "+restore.what()};}
        }
    }else if(s.listener)s.stop_listener();
    const auto actualPort=next->port();const auto generation=s.start_listener(std::move(next),bind);return {generation,std::move(bind),actualPort,{}};
}
void Service::stop(){auto& s=*state_;std::lock_guard lifecycle(s.lifecycle_mutex);s.stop_listener();s.post([&s]{s.revoked(s.policy.sharing(false));std::vector<Token> incoming;for(const auto& [id,link]:s.links)if(link->incoming)incoming.push_back(id);for(auto id:incoming)s.drop(id,"Sharing stopped");});}
Service::Token Service::connect(ConnectOptions options){auto& s=*state_;const auto token=s.next();const auto generation=s.epoch.load();s.post([&s,token,generation,options=std::move(options)]()mutable{try{
    const auto normalized=[](std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value=="localhost"?std::string("127.0.0.1"):value;};
    for(const auto& device:s.store.devices())if((valid_id(options.expected_peer)&&device.id==options.expected_peer)||(!valid_id(options.expected_peer)&&(normalized(device.address_hint)==normalized(options.address))&&device.port_hint==options.port)){
        s.suppressed_routes.erase(device.id);auto route=s.gateway_routes.find(device.id);if(route!=s.gateway_routes.end()){route->second.disabled=false;route->second.failures=0;route->second.due=0;route->second.identity_failed=false;route->second.attempted=false;}
    }
    s.launch_dial(token,std::move(options),generation);}catch(const std::exception& e){s.event({EventKind::error,{token},0,e.what()});}});return token;}
Service::Token Service::connect_via(Token gateway,DeviceId owner,std::string code){auto& s=*state_;const auto token=s.next();s.post([&s,token,gateway,owner,code=std::move(code)]()mutable{try{s.begin_endpoint(token,gateway,owner,std::move(code));}catch(const std::exception& e){s.event({EventKind::error,{token},0,e.what()});}});return token;}
Service::Token Service::catalog(Token connection){auto& s=*state_;auto request=s.next();s.post([&s,connection,request]{auto& l=s.link(connection);need(l.catalogs.size()<64,"Too many catalog requests");l.catalogs.insert(request);l.requests[request]=Clock::now();l.connection->queue({rpc::Operation::catalog,request,{}});});return request;}
Service::Token Service::open(Token connection,rpc::Open request){auto& s=*state_;auto token=s.next();s.post([&s,connection,token,request=std::move(request)]()mutable{try{auto& l=s.link(connection);need(!l.incoming&&l.pending.size()+l.resources.size()<256,"Resource connection budget exhausted");need(request.owner==l.connection->remote_id(),"Connect to the actual endpoint through an authenticated tunnel before using this resource");l.used_resources=true;const Offer* offer=nullptr;for(const auto& catalog:l.catalog_cache)if(catalog.device==request.owner)for(const auto& item:catalog.offers)if(item.id==request.resource)offer=&item;need(offer&&offer->available,"Refresh this device's available resources before connecting");need(!request.input||offer->input_allowed,"Remote input is not permitted");
    if(offer->kind==Kind::existing_screen){
        const auto same=[&](const rpc::Open& other){return other.owner==request.owner&&other.resource==request.resource;};
        for(const auto& [id,peer]:s.links)if(!peer->incoming){for(const auto& [pending,other]:peer->pending)need(s.relays.contains(pending)||!same(other),"This screen is already opening on this device");for(const auto& [grant,other]:peer->resources)need(other.relay||!same(other.request),"This screen is already open on this device");}
        for(const auto& [id,retry]:s.retries)for(const auto& saved:retry.resources)need(!same(saved.request),"This screen is reconnecting on this device");
    }
    rpc::encode(request);Context context{connection,token,0,l.connection->remote_id(),false,offer->kind};s.prepare_request(l,context,std::move(request));}catch(const std::exception& e){s.event({EventKind::error,{connection,token},0,e.what()});}});return token;}
void Service::close(Token connection,Token grant){auto& s=*state_;s.post([&s,connection,grant]{
    auto retry=s.retries.find(connection);
    if(retry!=s.retries.end()){
        auto& plan=retry->second;auto saved=std::find_if(plan.resources.begin(),plan.resources.end(),[&](const auto& item){return item.previous_grant==grant;});
        if(saved!=plan.resources.end()){
            const auto request=saved->restoring_request;plan.waiting.erase(request);plan.resources.erase(saved);
            auto live=s.links.find(connection);if(live!=s.links.end()&&request){auto& l=*live->second;
                if(l.pending.contains(request)){l.abandoned.insert(request);s.cancel_request(l,request);}
                std::vector<Token> active;for(const auto& [id,r]:l.resources)if(r.context.request==request)active.push_back(id);
                for(auto id:active){s.stop_resource(l,id);l.connection->queue({rpc::Operation::close,id,{}});}
            }
            s.event({EventKind::stopped,{connection,0,grant},0,"Reconnect cancelled"});
            if(plan.resources.empty()){s.retries.erase(retry);if(live==s.links.end()){s.cancelled.insert(connection);s.event({EventKind::disconnected,{connection},0,"Reconnect cancelled"});}}
            return;
        }
    }
    auto& l=s.link(connection);if(l.incoming){auto f=l.resources.find(grant);if(f!=l.resources.end()&&f->second.relay){s.stop_resource(l,grant);l.connection->queue({rpc::Operation::stopped,grant,{}});}else l.connection->queue(l.dispatcher->handle({rpc::Operation::close,grant,{}},now()));}else{s.stop_resource(l,grant);l.connection->queue({rpc::Operation::close,grant,{}});}});}
void Service::cancel(Token connection,Token request){auto& s=*state_;s.post([&s,connection,request]{
    auto f=s.links.find(connection);if(f==s.links.end())return;auto& l=*f->second;
    s.end_pending(l,request,"Connection cancelled");
    std::vector<Token> active;for(const auto& [id,resource]:l.resources)if(resource.context.request==request)active.push_back(id);
    for(auto grant:active){s.stop_resource(l,grant);l.connection->queue({rpc::Operation::close,grant,{}});}
});}
void Service::disconnect(Token connection){auto& s=*state_;s.post([&s,connection]{
    auto endpoint=s.endpoint_pending.find(connection);if(endpoint!=s.endpoint_pending.end()&&!endpoint->second.visitor){auto outer=s.links.find(endpoint->second.connection);if(outer!=s.links.end())outer->second->connection->queue({rpc::Operation::endpoint_cancel,connection,{}});s.endpoint_routes[endpoint->second.owner].pending=0;s.retire_endpoint(connection,endpoint->second.connection);s.endpoint_pending.erase(endpoint);s.event({EventKind::disconnected,{connection},0,"Endpoint connection cancelled"});return;}
    if(s.endpoint_inflight.contains(connection)){for(auto& [id,link]:s.links){std::vector<Token> grants;for(const auto& [grant,tunnel]:link->tunnels)if(tunnel.inner_token==connection)grants.push_back(grant);for(auto grant:grants)s.close_tunnel(*link,grant);}s.cancelled.insert(connection);s.event({EventKind::disconnected,{connection},0,"Endpoint connection cancelled"});return;}

    auto suppress=[&](const DeviceId& peer){if(!valid_id(peer))return;s.suppressed_routes.insert(peer);auto route=s.gateway_routes.find(peer);if(route!=s.gateway_routes.end())route->second.disabled=true;};
    auto retry=s.retries.find(connection);if(retry!=s.retries.end())suppress(retry->second.route.expected_peer);
    auto target=s.dial_targets.find(connection);if(target!=s.dial_targets.end())suppress(target->second);
    if(s.retries.erase(connection))s.event({EventKind::disconnected,{connection},0,"Reconnect cancelled"});auto live=s.links.find(connection);if(live!=s.links.end()){s.suppressed_routes.insert(live->second->connection->remote_id());auto route=s.gateway_routes.find(live->second->connection->remote_id());if(route!=s.gateway_routes.end())route->second.disabled=true;}auto dialing=s.gateway_dials.find(connection);if(dialing!=s.gateway_dials.end()){s.suppressed_routes.insert(dialing->second);auto route=s.gateway_routes.find(dialing->second);if(route!=s.gateway_routes.end())route->second.disabled=true;}
    if(!s.links.contains(connection)){need(s.cancelled.size()<256,"Cancellation budget exhausted");s.cancelled.insert(connection);}s.drop(connection);});}
void Service::offers(std::vector<Offer> offers){auto& s=*state_;s.post([&s,offers=std::move(offers)]()mutable{s.revoked(s.policy.offers(std::move(offers)));for(auto& [id,l]:s.links)if(l->incoming&&l->connection->identity().paired)l->connection->queue({rpc::Operation::ping,rpc::catalog_changed,{}});});}
void Service::forget(DeviceId peer){auto& s=*state_;s.post([&s,peer]{
    {std::lock_guard lock(s.trust_mutex);++s.epoch;s.store.begin_forget(peer);}
    s.revoke_local(peer);s.refresh_forgets();
});}
void Service::dismiss_forget(DeviceId peer){auto& s=*state_;s.post([&s,peer]{std::lock_guard lock(s.trust_mutex);s.store.dismiss_forget(peer);s.event({EventKind::trust_changed,{},0,"Local removal cleared; remote removal was not confirmed"});});}
void Service::pairing_required(bool required){auto& s=*state_;s.post([&s,required]{s.auth.set_required(required);s.revoked(s.policy.pairing_required(required));if(required){std::vector<Token> ids;for(const auto& [id,l]:s.links)if(!l->connection->identity().paired)ids.push_back(id);for(auto id:ids)s.drop(id,"Pairing is now required");}});}
void Service::auto_connect(bool enabled){auto& s=*state_;s.post([&s,enabled]{s.auto_connect_enabled=enabled;s.sync_routes();if(!enabled&&!s.policy.gateway_enabled()){for(const auto& [token,id]:s.gateway_dials)s.cancelled.insert(token);} });}
void Service::observe_route(DeviceId peer,std::string address,std::uint16_t port){auto& s=*state_;s.post([&s,peer,address=std::move(address),port]{
    if(!valid_id(peer)||peer==s.store.identity()||address.empty()||!port)return;
    for(const auto& pending:s.store.pending_forgets())if(pending.id==peer){const bool firstRoute=pending.address_hint.empty()&&!s.forget_routes.contains(peer);s.forget_routes[peer]={address,port};if(firstRoute)s.forget_attempts[peer].due=0;return;}
    if(!s.store.secret(peer))return;
    // Observed LAN source is merely a route candidate; identity remains pinned.
    // Never persist it, clear explicit suppression or replenish the retry budget.
    for(auto device:s.store.devices())if(device.id==peer){auto [entry,inserted]=s.gateway_routes.try_emplace(peer);const bool changed=entry->second.device.address_hint!=address||entry->second.device.port_hint!=port;if(changed)entry->second.identity_failed=false;device.address_hint=address;device.port_hint=port;entry->second.device=std::move(device);entry->second.disabled=s.suppressed_routes.contains(peer);break;}
});}
void Service::gateway(bool enabled){auto& s=*state_;s.post([&s,enabled]{s.revoked(s.policy.gateway(enabled));s.sync_routes();
    if(!enabled)for(auto& [id,link]:s.links){std::vector<Token> relay;for(const auto& [grant,tunnel]:link->tunnels)if(tunnel.other_connection)relay.push_back(grant);for(auto grant:relay)s.close_tunnel(*link,grant);}
    if(!enabled&&!s.auto_connect_enabled)for(const auto& [token,id]:s.gateway_dials)s.cancelled.insert(token);
    if(enabled)for(auto& [id,l]:s.links)if(!l->incoming&&l->connection->identity().paired){l->last_catalog=Clock::now()-std::chrono::seconds(11);for(const auto& catalog:l->catalog_cache)if(catalog.device!=s.store.identity()&&catalog.device==l->connection->remote_id())s.revoked(s.policy.remote(l->connection->identity(),catalog,0,now()));}});}
std::vector<Service::Event> Service::poll(){auto& s=*state_;std::lock_guard lock(s.mutex);std::vector<Event> result;while(!s.events.empty()){result.push_back(std::move(s.events.front()));s.events.pop_front();}return result;}
}
