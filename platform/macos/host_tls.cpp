#include <algorithm>
#include "host_tls.hpp"
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <psa/crypto.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <array>
#include <stdexcept>
namespace dm::mac {
namespace {std::recursive_mutex crypto;using Clock=std::chrono::steady_clock;
void check(int code){if(!code)return;char text[256]{};mbedtls_strerror(code,text,sizeof(text));throw std::runtime_error(std::string("TLS: ")+text);}
int sendBytes(void* p,const unsigned char* data,size_t size){int n=int(send(*static_cast<int*>(p),data,size,0));if(n<0)return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR?MBEDTLS_ERR_SSL_WANT_WRITE:MBEDTLS_ERR_SSL_INTERNAL_ERROR;return n;}
int receiveBytes(void* p,unsigned char* data,size_t size){int n=int(recv(*static_cast<int*>(p),data,size,0));if(n<0)return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR?MBEDTLS_ERR_SSL_WANT_READ:MBEDTLS_ERR_SSL_INTERNAL_ERROR;return n;}
}
struct HostTls::State {
    int fd;std::function<bool()> cancelled;mbedtls_ssl_context ssl{};mbedtls_ssl_config config{};mbedtls_x509_crt cert{};mbedtls_pk_context key{};mbedtls_svc_key_id_t keyID=0;
    State(int socket,std::function<bool()> stop):fd(socket),cancelled(std::move(stop)){mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);mbedtls_x509_crt_init(&cert);mbedtls_pk_init(&key);}
    ~State(){std::lock_guard lock(crypto);mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_x509_crt_free(&cert);mbedtls_pk_free(&key);if(keyID)psa_destroy_key(keyID);close(fd);}
    void retry(int result,Clock::time_point deadline){
        if(cancelled())throw std::runtime_error("Server stopped");if(Clock::now()>=deadline)throw std::runtime_error("Client timed out");
        if(result!=MBEDTLS_ERR_SSL_WANT_READ && result!=MBEDTLS_ERR_SSL_WANT_WRITE)check(result);
        pollfd p{fd,short(result==MBEDTLS_ERR_SSL_WANT_WRITE?POLLOUT:POLLIN),0};if(poll(&p,1,50)<0 && errno!=EINTR)throw std::runtime_error("Client socket failed");
    }
    void configure(){std::lock_guard lock(crypto);if(psa_crypto_init()!=PSA_SUCCESS)throw std::runtime_error("Crypto initialization failed");
        check(mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_SERVER,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT));
        mbedtls_ssl_conf_min_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_3);mbedtls_ssl_conf_max_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_tls13_key_exchange_modes(&config,MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL);mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_session_tickets(&config,MBEDTLS_SSL_SESSION_TICKETS_DISABLED);mbedtls_ssl_conf_new_session_tickets(&config,0);
        psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));psa_set_key_bits(&attributes,256);psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_SIGN_HASH);psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_256));
        if(psa_generate_key(&attributes,&keyID)!=PSA_SUCCESS)throw std::runtime_error("Certificate key generation failed");check(mbedtls_pk_wrap_psa(&key,keyID));
        mbedtls_x509write_cert writer;mbedtls_x509write_crt_init(&writer);struct Cleanup{mbedtls_x509write_cert* p;~Cleanup(){mbedtls_x509write_crt_free(p);}}cleanup{&writer};
        mbedtls_x509write_crt_set_version(&writer,MBEDTLS_X509_CRT_VERSION_3);mbedtls_x509write_crt_set_md_alg(&writer,MBEDTLS_MD_SHA256);mbedtls_x509write_crt_set_subject_key(&writer,&key);mbedtls_x509write_crt_set_issuer_key(&writer,&key);
        check(mbedtls_x509write_crt_set_subject_name(&writer,"CN=ExtraPane LAN"));check(mbedtls_x509write_crt_set_issuer_name(&writer,"CN=ExtraPane LAN"));check(mbedtls_x509write_crt_set_validity(&writer,"20260101000000","20491231235959"));
        check(mbedtls_x509write_crt_set_basic_constraints(&writer,0,-1));check(mbedtls_x509write_crt_set_key_usage(&writer,MBEDTLS_X509_KU_DIGITAL_SIGNATURE));
        std::array<unsigned char,16> serial{};if(psa_generate_random(serial.data(),serial.size())!=PSA_SUCCESS)throw std::runtime_error("Certificate serial generation failed");serial[0]=1;check(mbedtls_x509write_crt_set_serial_raw(&writer,serial.data(),serial.size()));
        std::array<unsigned char,2048> der{};int size=mbedtls_x509write_crt_der(&writer,der.data(),der.size());if(size<0)check(size);check(mbedtls_x509_crt_parse_der(&cert,der.data()+der.size()-size,size));check(mbedtls_ssl_conf_own_cert(&config,&cert,&key));check(mbedtls_ssl_setup(&ssl,&config));mbedtls_ssl_set_bio(&ssl,&fd,sendBytes,receiveBytes,nullptr);
    }
};
HostTls::HostTls(int socket,std::function<bool()> cancelled,access::Gate* gate):state_(std::make_unique<State>(socket,std::move(cancelled))){auto& s=*state_;fcntl(socket,F_SETFL,fcntl(socket,F_GETFL,0)|O_NONBLOCK);int yes=1;setsockopt(socket,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));s.configure();auto deadline=Clock::now()+std::chrono::seconds(10);for(;;){int r;{std::lock_guard lock(crypto);r=mbedtls_ssl_handshake(&s.ssl);}if(!r)break;s.retry(r,deadline);}
    gate_=gate;if(gate){protected_=std::make_unique<access::Stream>([this](std::span<uint8_t> out){auto bytes=readRaw(out.size(),seconds_);std::copy(bytes.begin(),bytes.end(),out.begin());},[this](std::span<const uint8_t> bytes){writeRaw(bytes,seconds_);});protected_->server(*gate);}
}
HostTls::~HostTls(){if(gate_&&control_)gate_->close_control(control_);}
bool HostTls::openControl(){if(control_)throw std::runtime_error("Control already open");if(!gate_)return false;control_=protected_->open_control(*gate_);return true;}
std::vector<access::ChannelCredential> HostTls::controlChannels(unsigned kind,std::optional<bool> input){if(kind>4)throw std::runtime_error("Invalid resource kind");if(!gate_)return {};if(!control_)throw std::runtime_error("Control must open first");return gate_->control_channels(1u<<kind,control_,input);}
std::optional<bool> HostTls::authorizedDisplayInput()const{return protected_?protected_->authorized_display_input():std::optional<bool>{};}
void HostTls::requireResource(unsigned kind)const{if(protected_){auto allowed=protected_->authorized_kind();if(allowed&&*allowed!=kind)throw std::runtime_error("Credential does not authorize this resource");}}
std::vector<uint8_t> HostTls::readRaw(size_t count,int seconds){if(count>65536+16)throw std::runtime_error("Client input exceeds bounds");auto& s=*state_;std::vector<uint8_t> bytes(count);size_t offset=0;auto deadline=Clock::now()+std::chrono::seconds(seconds);while(offset<count){if(Clock::now()>=deadline)throw std::runtime_error("Read timed out");if(s.cancelled())throw std::runtime_error("Server stopped");int n;{std::lock_guard lock(crypto);n=mbedtls_ssl_read(&s.ssl,bytes.data()+offset,count-offset);}if(n>0)offset+=n;else if(!n || n==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)throw std::runtime_error("Client disconnected");else s.retry(n,deadline);}return bytes;}
void HostTls::writeRaw(std::span<const uint8_t> data,int seconds){auto& s=*state_;auto deadline=Clock::now()+std::chrono::seconds(seconds);while(!data.empty()){if(Clock::now()>=deadline)throw std::runtime_error("Write timed out");if(s.cancelled())throw std::runtime_error("Server stopped");int n;{std::lock_guard lock(crypto);n=mbedtls_ssl_write(&s.ssl,data.data(),std::min(data.size(),size_t(16384)));}if(n>0)data=data.subspan(n);else s.retry(n,deadline);}}
}

namespace dm::mac {
std::vector<uint8_t> HostTls::read(size_t count,int seconds){if(count>8192+32)throw std::runtime_error("Client input exceeds bounds");seconds_=seconds;if(!protected_)return readRaw(count,seconds);std::vector<uint8_t> bytes(count);protected_->read(bytes);return bytes;}
void HostTls::write(std::span<const uint8_t> bytes,int seconds){seconds_=seconds;if(protected_)protected_->write(bytes);else writeRaw(bytes,seconds);}
}
