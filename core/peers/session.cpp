#include <extrapane/peer_session.hpp>
#include <algorithm>
#include <stdexcept>

namespace extrapane::peers {
namespace {
[[noreturn]] void fail(const char* text){throw std::runtime_error(text);}
rpc::Message rejected(std::uint64_t id,const std::string& reason){return {rpc::Operation::rejected,id,rpc::detail(reason)};}
}
AuthenticatedConnection::AuthenticatedConnection(transport::Stream stream,dm::peer_auth::Identity auth)
    :stream_(std::move(stream)),identity_{auth.verified_peer.value_or(auth.claimed_peer),auth.verified_peer.has_value()},revocation_only_(auth.revocation_only),revocation_secret_(std::move(auth.revocation_secret)){
    if(!valid_id(identity_.device))fail("Authentication returned no peer identity");
}
AuthenticatedConnection AuthenticatedConnection::client(transport::Stream stream,dm::peer_auth::ClientOptions options,const dm::peer_auth::Remember& remember,transport::Deadline deadline){
    options.outer_encrypted=true;options.binding=stream.binding();
    dm::peer_auth::Channel auth([&](auto b){stream.read(b,deadline);},[&](auto b){stream.write(b,deadline);});
    auto identity=auth.client(std::move(options),remember);return AuthenticatedConnection(std::move(stream),std::move(identity));
}
AuthenticatedConnection AuthenticatedConnection::server(transport::Stream stream,dm::peer_auth::Policy& policy,const dm::peer_auth::Lookup& lookup,const dm::peer_auth::Remember& remember,transport::Deadline deadline,const dm::peer_auth::Lookup& revocation_lookup){
    dm::peer_auth::Channel auth([&](auto b){stream.read(b,deadline);},[&](auto b){stream.write(b,deadline);});
    auto identity=auth.server(policy,lookup,remember,true,stream.binding(),revocation_lookup);return AuthenticatedConnection(std::move(stream),std::move(identity));
}
void AuthenticatedConnection::queue(const rpc::Message& message){
    auto bytes=rpc::encode(message);
    if(output_.size()>=max_messages || bytes.size()>max_queued-output_bytes_)fail("Peer output queue is full");
    output_bytes_+=bytes.size();output_.push_back(std::move(bytes));
}
bool AuthenticatedConnection::pump(transport::Deadline deadline){
    try{
        if(std::chrono::steady_clock::now()>=deadline)throw transport::Error(transport::ErrorReason::timeout,"Peer session timed out");
        bool progress=false;
        const int read=stream_.read_some(*read_buffer_);
        if(read==0)throw transport::Error(transport::ErrorReason::network,"Peer closed the session");
        if(read>0){
            for(auto& message:decoder_.feed(std::span(*read_buffer_).first(read))){
                const auto size=message.payload.size()+rpc::header_size;
                if(input_.size()>=max_messages || size>max_queued-input_bytes_)fail("Peer input queue is full");
                input_bytes_+=size;input_.push_back(std::move(message));
            }
            progress=true;
        }
        if(!output_.empty()){
            auto data=std::span<const std::uint8_t>(output_.front()).subspan(offset_);
            data=data.first(std::min(data.size(),rpc::data_chunk));
            const int written=stream_.write_some(data);
            if(written==0)throw transport::Error(transport::ErrorReason::network,"Peer closed the session");
            if(written>0){offset_+=written;output_bytes_-=written;progress=true;if(offset_==output_.front().size()){output_.pop_front();offset_=0;}}
        }
        return progress;
    }catch(...){cancel();throw;}
}
std::optional<rpc::Message> AuthenticatedConnection::next(){
    if(input_.empty())return {};
    auto message=std::move(input_.front());input_.pop_front();input_bytes_-=message.payload.size()+rpc::header_size;return message;
}
void AuthenticatedConnection::wait(transport::Deadline deadline){stream_.wait_io(deadline);}
void AuthenticatedConnection::send(const rpc::Message& message,transport::Deadline deadline){queue(message);while(pending_output())if(!pump(deadline))wait(deadline);}
rpc::Message AuthenticatedConnection::receive(transport::Deadline deadline){for(;;){if(auto message=next())return std::move(*message);if(!pump(deadline))wait(deadline);}}
SessionDispatcher::SessionDispatcher(SharingPolicy& policy,const AuthenticatedConnection& connection,Adapter adapter)
    :policy_(policy),identity_(connection.identity()),adapter_(std::move(adapter)){if(connection.revocation_only())fail("Revocation connection cannot access resources");}
SessionDispatcher::~SessionDispatcher(){close();}
void SessionDispatcher::stop(const Grant& grant)noexcept{try{if(adapter_.stop)adapter_.stop(grant);}catch(...){/* Stop all remaining resources even if an adapter cleanup fails. */}}
bool SessionDispatcher::owns(std::uint64_t grant)const noexcept{return !closed_&&owned_.contains(grant);}
void SessionDispatcher::close()noexcept{if(closed_)return;closed_=true;for(const auto& [id,grant]:owned_){policy_.close(id);stop(grant);}owned_.clear();}
std::vector<rpc::Message> SessionDispatcher::revoked(std::span<const Grant> grants){
    std::vector<rpc::Message> messages;
    for(const auto& grant:grants){auto it=owned_.find(grant.id);if(it==owned_.end())continue;stop(it->second);owned_.erase(it);messages.push_back({rpc::Operation::stopped,grant.id,{}});}
    return messages;
}
rpc::Message SessionDispatcher::handle(const rpc::Message& message,Milliseconds now){
    if(closed_)fail("Peer session is closed");
    using O=rpc::Operation;
    switch(message.operation){
    case O::catalog:
        if(!message.id || !message.payload.empty())fail("Invalid catalog request");
        return {O::catalog_reply,message.id,rpc::encode_catalogs(policy_.visible(identity_,now))};
    case O::open:{
        if(!message.id)fail("Missing request identity");
        const auto request=rpc::decode_open(message.payload);
        if(owned_.size()>=256)return rejected(message.id,"Connection resource budget exhausted");
        auto decision=policy_.open(identity_,request.owner,request.resource,request.input,now);
        if(!decision.grant)return rejected(message.id,decision.reason);
        const auto grant=*decision.grant;
        if(!adapter_.start || !adapter_.stop){policy_.close(grant.id);return rejected(message.id,"Resource adapter is unavailable");}
        std::string reason;bool started=false;
        try{started=adapter_.start(grant,request,reason);}catch(...){reason="Resource preparation failed";}
        if(!started){policy_.close(grant.id);stop(grant);return rejected(message.id,reason.empty()?"Resource preparation failed":reason);}
        try{owned_.emplace(grant.id,grant);}catch(...){policy_.close(grant.id);stop(grant);throw;}
        try{return {O::granted,message.id,rpc::encode(rpc::Granted{grant.id,adapter_.credential?adapter_.credential(grant):std::string{}})};}
        catch(...){policy_.close(grant.id);owned_.erase(grant.id);stop(grant);return rejected(message.id,"Resource credential preparation failed");}
    }
    case O::close:{
        if(!message.id || !message.payload.empty())fail("Invalid close request");
        const auto owned=owned_.find(message.id);
        if(owned==owned_.end())return rejected(message.id,"Resource does not belong to this connection");
        policy_.close(message.id);stop(owned->second);owned_.erase(owned);return {O::stopped,message.id,{}};
    }
    case O::ping:
        if(!message.payload.empty())fail("Invalid keepalive");
        return {O::pong,message.id,{}};
    default:fail("Unexpected peer request");
    }
}
}
