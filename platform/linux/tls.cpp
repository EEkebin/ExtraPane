#include "tls.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace dm::linux_client {
namespace {
[[noreturn]] void failure(const char* operation) {
    char message[256]{};
    const auto error=ERR_get_error();
    if(error) ERR_error_string_n(error,message,sizeof(message));
    throw std::runtime_error(std::string(operation)+(error?": "+std::string(message):": connection closed or TLS failure"));
}
sockaddr_in address(const std::string& text,std::uint16_t port) {
    sockaddr_in result{}; result.sin_family=AF_INET; result.sin_port=htons(port);
    if(inet_pton(AF_INET,text.c_str(),&result.sin_addr)!=1)
        throw std::runtime_error("Enter a numeric IPv4 address (DNS and discovery are not used).");
    return result;
}
}
struct Tls::State {
    int socket=-1;
    SSL_CTX* context=nullptr;
    SSL* ssl=nullptr;
    std::function<bool()> cancelled;
    ~State(){if(ssl)SSL_free(ssl);if(context)SSL_CTX_free(context);if(socket>=0)::close(socket);}
    void check(Deadline deadline) const {
        if(cancelled())throw std::runtime_error("Disconnected");
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Connection timed out");
    }
    void wait(bool write,Deadline deadline) {
        for(;;){
            check(deadline);
            pollfd descriptor{socket,static_cast<short>(write?POLLOUT:POLLIN),0};
            const int result=poll(&descriptor,1,50);
            if(result<0){if(errno==EINTR)continue;throw std::runtime_error(std::strerror(errno));}
            if(result>0){
                if(descriptor.revents&(POLLERR|POLLNVAL))throw std::runtime_error("Socket failed");
                // Let SSL consume pending bytes, or report EOF, when HUP accompanies POLLIN.
                if(descriptor.revents&(descriptor.events|POLLHUP))return;
            }
        }
    }
    void retry(int result,Deadline deadline,const char* operation){
        const int error=SSL_get_error(ssl,result);
        if(error==SSL_ERROR_WANT_READ)wait(false,deadline);
        else if(error==SSL_ERROR_WANT_WRITE)wait(true,deadline);
        else failure(operation);
    }
};
Tls::Tls(const std::string& peer,const std::string& local,std::uint16_t port,std::function<bool()> cancelled,unsigned interfaceIndex)
    :state_(std::make_unique<State>()) {
    auto& s=*state_;s.cancelled=std::move(cancelled);
    const auto destination=address(peer,port),source=address(local.empty()?"0.0.0.0":local,0);
    s.socket=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(s.socket<0)throw std::runtime_error(std::strerror(errno));
    if(interfaceIndex){
        const auto index=htonl(interfaceIndex);
        if(setsockopt(s.socket,IPPROTO_IP,IP_UNICAST_IF,&index,sizeof(index))<0)
            throw std::runtime_error(std::string("Cannot select outgoing interface: ")+std::strerror(errno));
    }
    const int yes=1;
    if(setsockopt(s.socket,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes))<0 ||
       ::bind(s.socket,reinterpret_cast<const sockaddr*>(&source),sizeof(source))<0)
        throw std::runtime_error(std::strerror(errno));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    if(::connect(s.socket,reinterpret_cast<const sockaddr*>(&destination),sizeof(destination))<0){
        if(errno!=EINPROGRESS)throw std::runtime_error(std::strerror(errno));
        s.wait(true,deadline);int error=0;socklen_t size=sizeof(error);
        if(getsockopt(s.socket,SOL_SOCKET,SO_ERROR,&error,&size)<0)throw std::runtime_error(std::strerror(errno));
        if(error)throw std::runtime_error(std::strerror(error));
    }
    s.context=SSL_CTX_new(TLS_client_method());
    if(!s.context)failure("Create TLS context");
    if(SSL_CTX_set_min_proto_version(s.context,TLS1_3_VERSION)!=1 ||
       SSL_CTX_set_max_proto_version(s.context,TLS1_3_VERSION)!=1)failure("Require TLS 1.3");
    SSL_CTX_set_verify(s.context,SSL_VERIFY_NONE,nullptr);
    SSL_CTX_set_session_cache_mode(s.context,SSL_SESS_CACHE_OFF);
    SSL_CTX_set_options(s.context,SSL_OP_NO_TICKET);
    s.ssl=SSL_new(s.context);
    if(!s.ssl || SSL_set_fd(s.ssl,s.socket)!=1)failure("Create TLS connection");
    for(;;){s.check(deadline);ERR_clear_error();const int result=SSL_connect(s.ssl);if(result==1)break;s.retry(result,deadline,"TLS handshake");}
}
Tls::Tls(int acceptedSocket,std::function<bool()> cancelled):state_(std::make_unique<State>()){
    auto& s=*state_;s.socket=acceptedSocket;s.cancelled=std::move(cancelled);
    const int yes=1;setsockopt(s.socket,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
    s.context=SSL_CTX_new(TLS_server_method());
    if(!s.context)failure("Create server TLS context");
    if(SSL_CTX_set_min_proto_version(s.context,TLS1_3_VERSION)!=1 || SSL_CTX_set_max_proto_version(s.context,TLS1_3_VERSION)!=1)failure("Require TLS 1.3");
    SSL_CTX_set_verify(s.context,SSL_VERIFY_NONE,nullptr);SSL_CTX_set_options(s.context,SSL_OP_NO_TICKET);
    SSL_CTX_set_session_cache_mode(s.context,SSL_SESS_CACHE_OFF);
    std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(EVP_EC_gen("prime256v1"),EVP_PKEY_free);
    std::unique_ptr<X509,decltype(&X509_free)> cert(X509_new(),X509_free);
    if(!key || !cert || X509_set_version(cert.get(),2)!=1 || ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),1)!=1 ||
       !X509_gmtime_adj(X509_getm_notBefore(cert.get()),-60) || !X509_gmtime_adj(X509_getm_notAfter(cert.get()),86400) ||
       X509_set_pubkey(cert.get(),key.get())!=1)failure("Create ephemeral server identity");
    auto* name=X509_get_subject_name(cert.get());
    if(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("ExtraPane LAN"),-1,-1,0)!=1 ||
       X509_set_issuer_name(cert.get(),name)!=1 || X509_sign(cert.get(),key.get(),EVP_sha256())<=0 ||
       SSL_CTX_use_certificate(s.context,cert.get())!=1 || SSL_CTX_use_PrivateKey(s.context,key.get())!=1)failure("Sign ephemeral server identity");
    s.ssl=SSL_new(s.context);if(!s.ssl || SSL_set_fd(s.ssl,s.socket)!=1)failure("Create TLS server connection");
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;){s.check(until);ERR_clear_error();int result=SSL_accept(s.ssl);if(result==1)break;s.retry(result,until,"Server TLS handshake");}
}
Tls::~Tls()=default;
void Tls::read(std::span<std::uint8_t> bytes,Deadline deadline){
    while(!bytes.empty()){
        state_->check(deadline);std::size_t size=0;ERR_clear_error();
        const int result=SSL_read_ex(state_->ssl,bytes.data(),bytes.size(),&size);
        if(result==1){bytes=bytes.subspan(size);continue;}
        state_->retry(result,deadline,"TLS read");
    }
}
void Tls::write(std::span<const std::uint8_t> bytes,Deadline deadline){
    while(!bytes.empty()){
        state_->check(deadline);std::size_t size=0;ERR_clear_error();
        const int result=SSL_write_ex(state_->ssl,bytes.data(),std::min<std::size_t>(bytes.size(),16384),&size);
        if(result==1){bytes=bytes.subspan(size);continue;}
        state_->retry(result,deadline,"TLS write");
    }
}
}
