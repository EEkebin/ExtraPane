#include "transport.hpp"
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <cstring>
namespace dm::mac {
bool ipv4(const char* value){in_addr a{};if(inet_pton(AF_INET,value,&a)!=1)return false;auto n=ntohl(a.s_addr);return n && (n>>24)!=127 && (n>>24)<224;}
static std::string errorText(nw_error_t error){
    if(!error)return "Connection closed";
    NSError* detail=CFBridgingRelease(nw_error_copy_cf_error(error));
    return std::string(detail.localizedDescription.UTF8String?:"Network operation failed")+" (domain "+std::to_string(int(nw_error_get_error_domain(error)))+", code "+std::to_string(nw_error_get_error_code(error))+")";
}
struct Completion {dispatch_semaphore_t signal=dispatch_semaphore_create(0);bool failed=false;std::string message;std::vector<uint8_t> bytes;};
static void wait(const std::shared_ptr<Completion>& c,int seconds){
    if(dispatch_semaphore_wait(c->signal,dispatch_time(DISPATCH_TIME_NOW,int64_t(seconds)*NSEC_PER_SEC)))throw std::runtime_error("Connection timed out");
    if(c->failed)throw std::runtime_error(c->message.empty()?"Connection closed or network operation failed":c->message);
}
Transport::Transport(const char* peer,const char* local){
    if(!ipv4(peer) || (*local && !ipv4(local)))throw std::runtime_error("Enter a reachable numeric LAN IPv4 address");
    queue_=dispatch_queue_create("io.github.eekebin.extrapane.network",DISPATCH_QUEUE_SERIAL);
    auto params=nw_parameters_create_secure_tcp(^(nw_protocol_options_t tls){
        auto security=nw_tls_copy_sec_protocol_options(tls);
        sec_protocol_options_set_min_tls_protocol_version(security,tls_protocol_version_TLSv13);
        sec_protocol_options_set_max_tls_protocol_version(security,tls_protocol_version_TLSv13);
        // Explicitly disable peer authentication as well as supplying a trust
        // callback. Apple can reject the ephemeral certificate before invoking
        // that callback when authentication remains required (errSSLBadCert).
        sec_protocol_options_set_peer_authentication_required(security,false);
        // Deliberate product automatic-LAN policy, scoped to this connection.
        sec_protocol_options_set_verify_block(security,^(sec_protocol_metadata_t,sec_trust_t,sec_protocol_verify_complete_t complete){NSLog(@"ExtraPane: automatic-LAN certificate callback");complete(true);},queue_);
    },^(nw_protocol_options_t tcp){nw_tcp_options_set_no_delay(tcp,true);});
    if(*local)nw_parameters_set_local_endpoint(params,nw_endpoint_create_host(local,"0"));
    connection_=nw_connection_create(nw_endpoint_create_host(peer,"43824"),params);
    nw_connection_set_queue(connection_,queue_);
}
Transport::~Transport(){cancel();}
void Transport::cancel(){nw_connection_cancel(connection_);}
void Transport::connect(){
    auto done=std::make_shared<Completion>();auto once=std::make_shared<std::atomic_bool>(false);
    nw_connection_set_state_changed_handler(connection_,^(nw_connection_state_t state,nw_error_t error){
        if(error)NSLog(@"ExtraPane: network state=%d domain=%d code=%d",int(state),int(nw_error_get_error_domain(error)),nw_error_get_error_code(error));
        else NSLog(@"ExtraPane: network state=%d",int(state));
        if(state==nw_connection_state_ready || state==nw_connection_state_failed || state==nw_connection_state_cancelled || (state==nw_connection_state_waiting && error)){
            if(!once->exchange(true)){done->failed=state!=nw_connection_state_ready;if(done->failed)done->message=errorText(error);dispatch_semaphore_signal(done->signal);}
        }
    });
    nw_connection_start(connection_);wait(done,10);
}
std::vector<uint8_t> Transport::read(size_t count,int seconds){
    if(count>2*1024*1024)throw std::runtime_error("Receive buffer exceeds protocol limit");
    std::vector<uint8_t> result;result.reserve(count);
    while(result.size()<count){auto done=std::make_shared<Completion>();
        nw_connection_receive(connection_,1,uint32_t(count-result.size()),^(dispatch_data_t data,nw_content_context_t,bool complete,nw_error_t error){
            if(data)dispatch_data_apply(data,^bool(dispatch_data_t,size_t,const void* bytes,size_t size){auto p=static_cast<const uint8_t*>(bytes);done->bytes.insert(done->bytes.end(),p,p+size);return true;});
            done->failed=error || (done->bytes.empty() && complete);if(done->failed)done->message=errorText(error);dispatch_semaphore_signal(done->signal);
        });wait(done,seconds);if(done->bytes.empty())throw std::runtime_error("Connection returned no data");result.insert(result.end(),done->bytes.begin(),done->bytes.end());
    }return result;
}
void Transport::write(std::span<const uint8_t> bytes){
    auto copy=std::make_shared<std::vector<uint8_t>>(bytes.begin(),bytes.end());auto done=std::make_shared<Completion>();
    auto data=dispatch_data_create(copy->data(),copy->size(),queue_,^{(void)copy;});
    nw_connection_send(connection_,data,NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT,true,^(nw_error_t error){done->failed=error!=nullptr;if(error)done->message=errorText(error);dispatch_semaphore_signal(done->signal);});wait(done,10);
}
}
