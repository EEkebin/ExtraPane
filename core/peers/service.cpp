#include <extrapane/service.hpp>
#include <extrapane/media_bridge.hpp>
#include <atomic>
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
    struct Link {
        Token token;bool incoming;std::unique_ptr<AuthenticatedConnection> connection;
        std::optional<ConnectOptions> route;
        std::unique_ptr<MediaBridge> bridge;std::unique_ptr<SessionDispatcher> dispatcher;
        std::map<Token,rpc::Open> pending;std::set<Token> catalogs,abandoned;
        std::vector<Catalog> catalog_cache;
        std::map<Token,Context> pending_contexts;
        std::map<Token,NativeCancel> pending_native;
        std::map<Token,Token> deferred_streams;
        std::deque<rpc::Message> deferred;
        std::deque<Token> discarded_streams,retired_grants;
        std::size_t deferred_bytes=0;
        std::map<Token,Clock::time_point> requests;
        std::map<Token,Resource> resources;Token current_request=0;
        bool used_resources=false;
        Clock::time_point activity=Clock::now(),last_ping=Clock::now();
        Clock::time_point last_catalog=Clock::now()-std::chrono::seconds(11);
        Link(Token t,bool in,std::unique_ptr<AuthenticatedConnection> c):token(t),incoming(in),connection(std::move(c)){}
    };
    struct Result {Token token;bool incoming;std::unique_ptr<AuthenticatedConnection> connection;std::string error;std::uint64_t epoch=0;std::optional<ConnectOptions> route;bool transient=false;};
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
    std::vector<std::future<Result>> dialing;std::map<Token,std::unique_ptr<Link>> links;
    std::map<Token,RelayPending> relays;
    struct GatewayRoute {RememberedDevice device;Reconnect retry{Kind::camera};Token pending=0;bool attempted=false,disabled=false;};
    std::map<DeviceId,GatewayRoute> gateway_routes;
    std::map<Token,DeviceId> gateway_dials;
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
        if(message.operation==rpc::Operation::stream_open){const auto grant=rpc::integer(message.payload);if(std::find(l.retired_grants.begin(),l.retired_grants.end(),grant)!=l.retired_grants.end()){l.discarded_streams.push_back(message.id);while(l.discarded_streams.size()>512)l.discarded_streams.pop_front();l.connection->queue({rpc::Operation::stream_close,message.id,{}});return;}const auto resource=l.resources.find(grant);need(resource!=l.resources.end(),"Unapproved media stream");
            if(!resource->second.relay&&!resource->second.native_ready){need(l.deferred_streams.size()<256&&!l.deferred_streams.contains(message.id),"Too many pending media streams");l.deferred_streams.emplace(message.id,grant);}
        }
        if(l.deferred_streams.contains(message.id)){const auto bytes=rpc::header_size+message.payload.size();need(l.deferred.size()<4096&&bytes<=1024*1024-l.deferred_bytes,"Pending media exceeds its budget");l.deferred.push_back(message);l.deferred_bytes+=bytes;return;}
        l.bridge->receive(message);
    }
    void native_failure(Context context,const std::string& reason){
        auto linkIt=links.find(context.connection);if(linkIt==links.end())return;auto& l=*linkIt->second;auto found=l.resources.find(context.grant);if(found==l.resources.end())return;found->second.failure=reason;
        const bool announced=found->second.native_ready;
        if(context.incoming){try{auto response=l.dispatcher->handle({rpc::Operation::close,context.grant,{}},now());if(announced)l.connection->queue(response);}catch(...){stop_resource(l,context.grant);l.connection->cancel();}}
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
    dm::peer_auth::Remember remember(std::string address={},std::uint16_t port=0,std::uint64_t generation=0){if(!generation)generation=epoch.load();return [this,address=std::move(address),port,generation](const DeviceId& id,const dm::peer_auth::Secret& key){std::lock_guard lock(trust_mutex);need(epoch==generation,"Pairing was cancelled");store.remember({id,"Paired device",address,port},key.bytes);};}
    void launch_dial(Token token,ConnectOptions options,std::uint64_t generation){
        need(dialing.size()<4,"Too many pending peer connections");
        dialing.push_back(std::async(std::launch::async,[this,token,generation,options=std::move(options)]{
            try{need(epoch==generation,"Connection cancelled");dm::peer_auth::ClientOptions proof;proof.local=store.identity();proof.expected_peer=options.expected_peer;proof.code=options.code;proof.mode=options.open_mode?dm::peer_auth::Mode::open:dm::peer_auth::Mode::pair;
                if(!options.open_mode&&valid_id(options.expected_peer)){auto key=store.secret(options.expected_peer);if(key){proof.mode=dm::peer_auth::Mode::remembered;dm::peer_auth::Secret secret;secret.bytes=*key;proof.remembered=secret;}}
                auto connection=AuthenticatedConnection::client(transport::Stream::connect(options.address,options.port,options.local,deadline()),proof,remember(options.address,options.port,generation),deadline());
                ConnectOptions route{options.address,options.port,options.local,connection.remote_id(),{},false};
                return Result{token,false,std::make_unique<AuthenticatedConnection>(std::move(connection)),{},generation,std::move(route)};
            }catch(const transport::Error& e){return Result{token,false,{},e.what(),generation,{},e.reason==transport::ErrorReason::network||e.reason==transport::ErrorReason::timeout};}
            catch(const std::exception& e){return Result{token,false,{},e.what(),generation};}
        }));
    }
    void refresh_routes(){
        if(!policy.gateway_enabled()||dialing.size()>=4)return;
        for(auto& [id,route]:gateway_routes){
            if(route.disabled||route.pending||!store.secret(id))continue;
            bool connected=false;for(const auto& [token,link]:links)if(!link->incoming&&link->connection->identity().paired&&link->connection->remote_id()==id){connected=true;break;}
            if(connected){route.retry.connected();route.attempted=false;continue;}
            if(route.attempted&&!route.retry.ready(now()))continue;
            const auto token=next();route.pending=token;route.attempted=true;gateway_dials[token]=id;
            try{launch_dial(token,{route.device.address_hint,route.device.port_hint,"",id,"",false},epoch.load());}
            catch(...){gateway_dials.erase(token);route.pending=0;route.retry.failed(Failure::transport_lost,now());}
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
    void drop(Token id,std::string reason={},bool transient=false){auto f=links.find(id);if(f==links.end())return;auto& l=*f->second;const bool retry=transient&&schedule_retry(l,reason);if(!retry)retries.erase(id);l.connection->cancel();if(l.dispatcher)l.dispatcher->close();while(!l.resources.empty())stop_resource(l,l.resources.begin()->first);while(!l.pending_contexts.empty())cancel_request(l,l.pending_contexts.begin()->first);Context c{id,0,0,l.connection->remote_id(),l.incoming};const bool wasProvider=!l.incoming;links.erase(f);if(wasProvider)revoked(policy.remote_removed(c.peer));if(!retry)event({EventKind::disconnected,c,0,std::move(reason)});}

    void granted(Link& l,Token grant,std::uint16_t port){const auto& resource=l.resources.at(grant);auto public_request=resource.request;public_request.native_credential.clear();Token previous=0;auto retry=retries.find(l.token);if(retry!=retries.end())for(const auto& saved:retry->second.resources)if(saved.restoring_request==resource.context.request)previous=saved.previous_grant;
        event({EventKind::granted,resource.context,port,{},{},l.connection->identity().paired,std::move(public_request),previous});retry_done(l.token,resource.context.request,true);}
    void add(Result result){
        auto retry=retries.find(result.token);
        if(retry!=retries.end()){
            retry->second.dialing=false;
            if(result.epoch!=epoch||cancelled.contains(result.token)){retries.erase(retry);}
            else if(!result.connection){
                if(result.transient&&retry->second.budget.failed(Failure::transport_lost,now()))retry_event(result.token,retry->second,"Device is unreachable; reconnecting");
                else{retries.erase(retry);event({EventKind::disconnected,{result.token},0,result.error});}return;
            }
        }
        const auto background=gateway_dials.find(result.token);
        if(background!=gateway_dials.end()){
            const auto id=background->second;gateway_dials.erase(background);auto route=gateway_routes.find(id);
            if(route!=gateway_routes.end()){route->second.pending=0;if(!result.connection){
                // Retry network reachability only, never rejected identity/code proofs.
                const bool network=result.transient;
                route->second.retry.failed(network?Failure::transport_lost:Failure::identity_changed,now());
            }}
            if(!policy.gateway_enabled()||route==gateway_routes.end()||route->second.disabled)return;
        }
        if(result.epoch!=epoch||cancelled.erase(result.token)){event({EventKind::disconnected,{result.token},0,"Connection cancelled"});return;}if(!result.connection){event({EventKind::error,{result.token},0,result.error});return;}need(links.size()<64,"Peer service connection budget exhausted");auto object=std::make_unique<Link>(result.token,result.incoming,std::move(result.connection));auto& l=*object;l.route=std::move(result.route);const auto identity=l.connection->identity();if(identity.paired&&identity.device!=store.identity()){need(store.secret(identity.device).has_value(),"Device was forgotten during authentication");policy.remember(identity.device);}
        l.bridge=std::make_unique<MediaBridge>(!l.incoming,[&l](Token grant){return l.resources.contains(grant);},[&l](const rpc::Message& m){l.connection->queue(m);});
        Adapter adapter;
        adapter.start=[this,&l](const Grant& grant,const rpc::Open& request,std::string& reason){if(!l.incoming||grant.delegated){reason="Delegated relay is not available";return false;}Resource r{{l.token,l.current_request,grant.id,l.connection->remote_id(),true,grant.kind},request};l.resources.emplace(grant.id,std::move(r));try{prepare(l,l.resources.at(grant.id));return true;}catch(const std::exception& e){reason=e.what();return false;}};
        adapter.stop=[this,&l](const Grant& g){stop_resource(l,g.id);};
        adapter.credential=[&l](const Grant& g){return l.resources.at(g.id).request.native_credential;};
        l.dispatcher=std::make_unique<SessionDispatcher>(policy,*l.connection,std::move(adapter));Context c{l.token,0,0,identity.device,l.incoming};links.emplace(l.token,std::move(object));event({EventKind::connected,c,0,{},{},identity.paired});if(identity.paired)event({EventKind::trust_changed,c});
        auto restoring=retries.find(l.token);if(restoring!=retries.end()){const auto request=next();restoring->second.catalog_request=request;l.catalogs.insert(request);l.requests[request]=Clock::now();l.connection->queue({rpc::Operation::catalog,request,{}});}
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
        if(m.operation>=O::stream_open&&m.operation<=O::stream_close){stream_message(l,m);return;}
        if(m.operation==O::catalog_reply){need(l.catalogs.erase(m.id)>0,"Unsolicited catalog response");auto catalogs=rpc::decode_catalogs(m.payload);for(const auto& c:catalogs){need(c.device==l.connection->remote_id()||l.connection->identity().paired,"Unverified delegated catalog rejected");if(policy.gateway_enabled()&&!l.incoming&&l.connection->identity().paired&&c.device!=store.identity()&&c.device==l.connection->remote_id())revoked(policy.remote(l.connection->identity(),c,0,now()));}l.catalog_cache=catalogs;retry_catalog(l,m.id,catalogs);event({EventKind::catalog,{l.token,m.id,0,l.connection->remote_id(),l.incoming},0,{},std::move(catalogs)});return;}
        if(m.operation==O::granted&&l.abandoned.erase(m.id)){auto grant=rpc::decode_granted(m.payload);discard_deferred(l,grant.grant);l.connection->queue({O::close,grant.grant,{}});return;}
        if(m.operation==O::rejected&&l.abandoned.erase(m.id))return;
        if(m.operation==O::granted){auto p=l.pending.find(m.id);need(!l.incoming&&p!=l.pending.end(),"Unsolicited resource grant");auto grant=rpc::decode_granted(m.payload);need(!l.resources.contains(grant.grant),"Duplicate resource grant");if(relay_granted(l,m.id,grant))return;auto context=l.pending_contexts.at(m.id);context.grant=grant.grant;Resource r{context,std::move(p->second)};auto native=l.pending_native.find(m.id);if(native!=l.pending_native.end()){r.native_cancel=native->second;l.pending_native.erase(native);}l.pending.erase(p);l.pending_contexts.erase(m.id);if(!grant.native_credential.empty())r.request.native_credential=grant.native_credential;l.resources.emplace(grant.grant,std::move(r));try{prepare(l,l.resources.at(grant.grant));}catch(...){stop_resource(l,grant.grant);l.connection->queue({O::close,grant.grant,{}});throw;}return;}
        if(m.operation==O::rejected){cancel_request(l,m.id);retry_done(l.token,m.id,false);l.catalogs.erase(m.id);event({EventKind::error,{l.token,m.id,0,l.connection->remote_id(),l.incoming},0,rpc::detail(m.payload)});return;}
        if(m.operation==O::stopped){stop_resource(l,m.id);return;}
        if(m.operation==O::pong)return;
        if(m.operation==O::open&&!l.incoming){l.connection->queue({O::rejected,m.id,rpc::detail("Open a separate connection to request this device's resources")});return;}
        if(m.operation==O::open){auto request=rpc::decode_open(m.payload);if(request.owner!=store.identity()){relay_open(l,m,request);return;}}
        if(m.operation==O::close&&l.incoming){auto resource=l.resources.find(m.id);if(resource!=l.resources.end()&&resource->second.relay){stop_resource(l,m.id);l.connection->queue({O::stopped,m.id,{}});return;}}
        if(m.operation==O::open)for(const auto& [grant,resource]:l.resources)need(resource.context.request!=m.id,"Duplicate resource request identity");
        l.current_request=m.id;auto response=l.dispatcher->handle(m,now());if(m.operation!=O::open||response.operation!=O::granted)l.connection->queue(response);
    }
    void run(){while(!ending){std::deque<std::function<void()>> completions;{std::lock_guard lock(native_mutex);completions.swap(native_completions);}for(auto& done:completions)try{done();}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}
        std::deque<std::function<void()>> work;{std::lock_guard lock(mutex);work.swap(commands);}for(auto& fn:work)try{fn();}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}
        for(auto i=dialing.begin();i!=dialing.end();){if(i->wait_for(std::chrono::seconds(0))!=std::future_status::ready){++i;continue;}try{add(i->get());}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}i=dialing.erase(i);}
        std::vector<std::tuple<Token,std::string,bool>> dead;bool progress=false;
        for(auto& [id,l]:links)try{for(const auto& [request,began]:l->requests)need(Clock::now()-began<std::chrono::seconds(l->pending.contains(request)?60:15),"Peer request timed out");for(unsigned turn=0;turn<16;++turn){bool moved=l->connection->pump(deadline());while(auto m=l->connection->next()){message(*l,std::move(*m));moved=true;}if(!l->connection->pending_output())moved=l->bridge->pump()||moved;progress=progress||moved;if(!moved)break;}if(l->used_resources&&!policy.gateway_enabled()&&l->resources.empty()&&l->pending.empty()&&!l->connection->pending_output()){dead.emplace_back(id,"All resources disconnected",false);continue;}if(Clock::now()-l->activity>std::chrono::seconds(30))throw transport::Error(transport::ErrorReason::timeout,"Device connection timed out");if(Clock::now()-l->last_ping>std::chrono::seconds(10)){l->connection->queue({rpc::Operation::ping,next(),{}});l->last_ping=Clock::now();}}catch(const transport::Error& e){dead.emplace_back(id,e.what(),e.reason==transport::ErrorReason::network||e.reason==transport::ErrorReason::timeout);}catch(const std::exception& e){dead.emplace_back(id,e.what(),false);}
        for(auto& [id,reason,retry]:dead)drop(id,std::move(reason),retry);
        try{revoked(policy.expire(now()));if(policy.gateway_enabled())for(auto& [id,l]:links)if(!l->incoming&&l->connection->identity().paired&&l->catalogs.empty()&&Clock::now()-l->last_catalog>std::chrono::seconds(10)){const auto request=next();l->catalogs.insert(request);l->requests[request]=Clock::now();l->connection->queue({rpc::Operation::catalog,request,{}});l->last_catalog=Clock::now();}}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}
        if(Clock::now()-last_routes>=std::chrono::milliseconds(100)){last_routes=Clock::now();try{refresh_routes();refresh_retries();}catch(const std::exception& e){event({EventKind::error,{},0,e.what()});}}
        for(auto& [id,l]:links){std::vector<Context> timedout;for(const auto& [grant,r]:l->resources)if(!r.relay&&!r.native_ready&&Clock::now()-r.native_started>std::chrono::seconds(60))timedout.push_back(r.context);for(const auto& context:timedout)native_failure(context,"Native resource preparation timed out");}
        if(hooks.poll&&!native_poll_pending&&Clock::now()-last_poll>=std::chrono::milliseconds(50)){
            last_poll=Clock::now();native_poll_pending=true;
            try{native_post([this]{std::vector<Context> ended;std::string failure;try{ended=hooks.poll();}catch(const std::exception& e){failure=e.what();}catch(...){failure="Native resource status failed";}
                complete([this,ended=std::move(ended),failure=std::move(failure)]{native_poll_pending=false;if(!failure.empty()){event({EventKind::error,{},0,failure});return;}for(const auto& context:ended){auto f=links.find(context.connection);if(f==links.end())continue;auto& l=*f->second;if(!context.grant){cancel_request(l,context.request);continue;}if(!l.resources.contains(context.grant))continue;if(l.incoming)l.connection->queue(l.dispatcher->handle({rpc::Operation::close,context.grant,{}},now()));else{stop_resource(l,context.grant);l.connection->queue({rpc::Operation::close,context.grant,{}});}}});
            });}catch(const std::exception& e){native_poll_pending=false;event({EventKind::error,{},0,e.what()});}
        }
        if(progress)std::this_thread::yield();else {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(2),[this]{return ending||!commands.empty();});}
    }while(!links.empty())drop(links.begin()->first);retries.clear();for(auto& task:dialing)task.wait();dialing.clear();}
    void reap_listeners(){for(auto i=retired.begin();i!=retired.end();)if(*i->done){i->thread.join();i=retired.erase(i);}else ++i;}
    void stop_listener(){accepting=false;++listener_serial;auto current=listener;if(current)current->cancel();if(acceptor.joinable())retired.push_back({std::move(acceptor),std::move(acceptor_done)});listener.reset();reap_listeners();}
};
Service::Service(Options options,Hooks hooks):state_(std::make_unique<State>(std::move(options),std::move(hooks))){}
Service::~Service(){auto& s=*state_;s.stop_listener();s.auth.stop();s.ending=true;s.wake.notify_all();if(s.worker.joinable())s.worker.join();{std::lock_guard lock(s.native_mutex);s.native_ending=true;}s.native_wake.notify_all();if(s.native_worker.joinable())s.native_worker.join();for(auto& retired:s.retired)if(retired.thread.joinable())retired.thread.join();}
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
Service::Token Service::connect(ConnectOptions options){auto& s=*state_;const auto token=s.next();const auto generation=s.epoch.load();s.post([&s,token,generation,options=std::move(options)]()mutable{try{s.launch_dial(token,std::move(options),generation);}catch(const std::exception& e){s.event({EventKind::error,{token},0,e.what()});}});return token;}
Service::Token Service::catalog(Token connection){auto& s=*state_;auto request=s.next();s.post([&s,connection,request]{auto& l=s.link(connection);need(l.catalogs.size()<64,"Too many catalog requests");l.catalogs.insert(request);l.requests[request]=Clock::now();l.connection->queue({rpc::Operation::catalog,request,{}});});return request;}
Service::Token Service::open(Token connection,rpc::Open request){auto& s=*state_;auto token=s.next();s.post([&s,connection,token,request=std::move(request)]()mutable{try{auto& l=s.link(connection);need(!l.incoming&&l.pending.size()+l.resources.size()<256,"Resource connection budget exhausted");need(request.owner==l.connection->remote_id()||l.connection->identity().paired,"A paired gateway is required for delegated resources");l.used_resources=true;const Offer* offer=nullptr;for(const auto& catalog:l.catalog_cache)if(catalog.device==request.owner)for(const auto& item:catalog.offers)if(item.id==request.resource)offer=&item;need(offer&&offer->available,"Refresh this device's available resources before connecting");need(!request.input||offer->input_allowed,"Remote input is not permitted");rpc::encode(request);Context context{connection,token,0,l.connection->remote_id(),false,offer->kind};s.prepare_request(l,context,std::move(request));}catch(const std::exception& e){s.event({EventKind::error,{connection,token},0,e.what()});}});return token;}
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
    if(l.pending.contains(request)){need(l.abandoned.size()<256,"Cancelled request budget exhausted");l.abandoned.insert(request);s.cancel_request(l,request);s.retry_done(connection,request,false);s.event({EventKind::stopped,{connection,request},0,"Connection cancelled"});}
    std::vector<Token> active;for(const auto& [id,resource]:l.resources)if(resource.context.request==request)active.push_back(id);
    for(auto grant:active){s.stop_resource(l,grant);l.connection->queue({rpc::Operation::close,grant,{}});}
});}
void Service::disconnect(Token connection){auto& s=*state_;s.post([&s,connection]{if(s.retries.erase(connection))s.event({EventKind::disconnected,{connection},0,"Reconnect cancelled"});auto live=s.links.find(connection);if(live!=s.links.end()){auto route=s.gateway_routes.find(live->second->connection->remote_id());if(route!=s.gateway_routes.end())route->second.disabled=true;}auto dialing=s.gateway_dials.find(connection);if(dialing!=s.gateway_dials.end()){auto route=s.gateway_routes.find(dialing->second);if(route!=s.gateway_routes.end())route->second.disabled=true;}
    if(!s.links.contains(connection)){need(s.cancelled.size()<256,"Cancellation budget exhausted");s.cancelled.insert(connection);}s.drop(connection);});}
void Service::offers(std::vector<Offer> offers){auto& s=*state_;s.post([&s,offers=std::move(offers)]()mutable{s.revoked(s.policy.offers(std::move(offers)));});}
void Service::forget(DeviceId peer){auto& s=*state_;s.post([&s,peer]{{std::lock_guard lock(s.trust_mutex);++s.epoch;s.store.forget(peer);}s.gateway_routes.erase(peer);std::vector<Token> retrying;for(const auto& [token,plan]:s.retries)if(plan.route.expected_peer==peer)retrying.push_back(token);for(auto token:retrying){s.retries.erase(token);s.cancelled.insert(token);s.event({EventKind::disconnected,{token},0,"Device was forgotten"});}s.revoked(s.policy.forget(peer));std::vector<Token> ids;for(const auto& [id,l]:s.links)if(l->connection->remote_id()==peer)ids.push_back(id);for(auto id:ids)s.drop(id,"Device was forgotten");s.event({EventKind::trust_changed,{}});});}
void Service::pairing_required(bool required){auto& s=*state_;s.post([&s,required]{s.auth.set_required(required);s.revoked(s.policy.pairing_required(required));if(required){std::vector<Token> ids;for(const auto& [id,l]:s.links)if(!l->connection->identity().paired)ids.push_back(id);for(auto id:ids)s.drop(id,"Pairing is now required");}});}
void Service::gateway(bool enabled){auto& s=*state_;s.post([&s,enabled]{s.revoked(s.policy.gateway(enabled));
    for(const auto& [token,id]:s.gateway_dials)s.cancelled.insert(token);s.gateway_dials.clear();s.gateway_routes.clear();
    if(enabled)for(auto device:s.store.devices())if(device.id!=s.store.identity()&&!device.address_hint.empty()&&device.port_hint){State::GatewayRoute route;route.device=std::move(device);s.gateway_routes.emplace(route.device.id,std::move(route));}
    if(enabled)for(auto& [id,l]:s.links)if(!l->incoming&&l->connection->identity().paired){l->last_catalog=Clock::now()-std::chrono::seconds(11);for(const auto& catalog:l->catalog_cache)if(catalog.device!=s.store.identity()&&catalog.device==l->connection->remote_id())s.revoked(s.policy.remote(l->connection->identity(),catalog,0,now()));}});}
std::vector<Service::Event> Service::poll(){auto& s=*state_;std::lock_guard lock(s.mutex);std::vector<Event> result;while(!s.events.empty()){result.push_back(std::move(s.events.front()));s.events.pop_front();}return result;}
}
