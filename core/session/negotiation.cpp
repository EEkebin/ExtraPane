#include <extrapane/negotiation.hpp>
#include <stdexcept>
#include <utility>

namespace extrapane {
Negotiation::Negotiation(Server& server,std::uint64_t admitted_id,Capabilities local,Factory factory)
    :server_(server),id_(admitted_id),local_(std::move(local)),factory_(std::move(factory)){
    if(!server_.has_session(id_)||!factory_)throw std::invalid_argument("An admitted session and factory are required");
    (void)wire::encode_capabilities(local_);
}
wire::Message Negotiation::handle(const wire::Message& message){
    using namespace wire;
    if(!server_.has_session(id_))throw std::runtime_error("Session is closed");
    try{
        const auto& h=message.header;(void)encode(h);
        if(h.payload_bytes!=message.payload.size())throw std::invalid_argument("Resource payload length mismatch");
        if(h.operation==Operation::capabilities){
            if(exchanged_ || h.kind!=ResourceKind::display)throw std::invalid_argument("Invalid capability exchange");
            peer_=decode_capabilities(message.payload);exchanged_=true;
            auto payload=encode_capabilities(local_);
            Header response{Operation::capabilities,ResourceKind::display,0,static_cast<std::uint32_t>(payload.size())};
            return {response,std::move(payload)};
        }
        if(!exchanged_)throw std::invalid_argument("Capability exchange required");
        Availability result;
        if(h.operation==Operation::request){
            auto request=decode_request(h,message.payload);
            result=can_connect(h.kind,peer_,local_);
            if(result.available){
                // Duplicate IDs and session limits must reject BEFORE factory
                // invocation, even though adapters are inert until prepare().
                const auto current=server_.snapshot(id_);
                if(current.size()>=max_session_resources)result={false,"Session resource limit reached"};
                for(const auto& entry:current)if(entry.request.id==h.resource_id)result={false,"Resource ID already used"};
                if(result.available){
                    try{result=server_.add(id_,std::move(request),factory_(h.kind));}
                    catch(...){result={false,"Resource adapter could not be created"};}
                }
            }
        }else if(h.operation==Operation::stop){
            // A mismatched kind must not stop an unrelated resource.
            bool matches=false;for(const auto& item:server_.snapshot(id_))if(item.request.id==h.resource_id&&item.request.kind==h.kind)matches=true;
            result=matches&&server_.stop_resource(id_,h.resource_id)?Availability{true,{}}:Availability{false,"Resource is not active"};
        }else throw std::invalid_argument("Unexpected client control operation");
        // Adapter diagnostic strings are local only. Avoid sending exception,
        // device-path or credential details to a remote peer.
        Bytes payload;
        if(!result.available){const std::string reason="Requested resource is unavailable";payload.assign(reason.begin(),reason.end());}
        Header response{result.available?(h.operation==Operation::stop?Operation::status:Operation::grant):Operation::reject,h.kind,h.resource_id,static_cast<std::uint32_t>(payload.size())};
        return {response,std::move(payload)};
    }catch(...){server_.disconnect(id_);throw;}
}
void Negotiation::serve_one(wire::ControlChannel& channel){
    try{channel.send(handle(channel.receive()));}
    catch(...){server_.disconnect(id_);throw;}
}
}
