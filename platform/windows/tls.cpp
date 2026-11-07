#include <windows.h>
#include <dm/tls.hpp>
#include <dm/probe.hpp>
#include <mbedtls/ssl.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <psa/crypto.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <mutex>

namespace dm::security {
namespace {std::shared_ptr<access::Gate> accessGate;std::string accessCode;std::mutex accessCodeMutex;}
void set_access_client(std::string code){if(!access::validCredential(code))throw std::invalid_argument("Access credential is malformed");std::lock_guard lock(accessCodeMutex);SecureZeroMemory(accessCode.data(),accessCode.size());accessCode=std::move(code);}
std::shared_ptr<access::Gate> enable_access_host(){accessGate=std::make_shared<access::Gate>();return accessGate;}

namespace {
// This dependency build has no native Windows threading backend. Serialize
// crypto calls, never network waits, across independent connection contexts.
std::recursive_mutex crypto_mutex;
using Deadline = std::chrono::steady_clock::time_point;
void check_deadline(Deadline deadline) {
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Secure operation timed out");
}
[[noreturn]] void tls_error(const char* stage, int code) {
    std::array<char, 200> text{};
    mbedtls_strerror(code, text.data(), text.size());
    throw std::runtime_error(std::string(stage) + ": " + text.data());
}
int send_callback(void* ctx, const unsigned char* bytes, std::size_t size) noexcept {
    try {
        const int result = static_cast<net::TcpStream*>(ctx)->write_some({bytes, size});
        return result == -1 ? MBEDTLS_ERR_SSL_WANT_WRITE : result;
    } catch (...) { return MBEDTLS_ERR_NET_SEND_FAILED; }
}
int recv_callback(void* ctx, unsigned char* bytes, std::size_t size) noexcept {
    try {
        const int result = static_cast<net::TcpStream*>(ctx)->read_some({bytes, size});
        return result == -1 ? MBEDTLS_ERR_SSL_WANT_READ : result;
    } catch (...) { return MBEDTLS_ERR_NET_RECV_FAILED; }
}
}
struct TlsStream::State {
    net::TcpStream socket;
    mbedtls_ssl_config config{};
    mbedtls_ssl_context ssl{};
    mbedtls_x509_crt certificate{};
    mbedtls_pk_context certificate_key{};
    mbedtls_svc_key_id_t key_id=0;
    explicit State(net::TcpStream value) : socket(std::move(value)) {
        mbedtls_ssl_config_init(&config);
        mbedtls_ssl_init(&ssl);
        mbedtls_x509_crt_init(&certificate);
        mbedtls_pk_init(&certificate_key);
    }
    ~State() { std::lock_guard lock(crypto_mutex);mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config);mbedtls_x509_crt_free(&certificate);mbedtls_pk_free(&certificate_key);if(key_id)psa_destroy_key(key_id); }
    void ephemeral_certificate() {
        psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
        psa_set_key_bits(&attributes,256);
        psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_SIGN_HASH);
        psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_256));
        if(psa_generate_key(&attributes,&key_id)!=PSA_SUCCESS)throw std::runtime_error("Cannot generate session certificate key");
        auto check=[](int result){if(result)tls_error("Session certificate",result);};
        check(mbedtls_pk_wrap_psa(&certificate_key,key_id));
        mbedtls_x509write_cert writer;mbedtls_x509write_crt_init(&writer);
        struct Cleanup {mbedtls_x509write_cert* p;~Cleanup(){mbedtls_x509write_crt_free(p);}} cleanup{&writer};
        mbedtls_x509write_crt_set_version(&writer,MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&writer,MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&writer,&certificate_key);
        mbedtls_x509write_crt_set_issuer_key(&writer,&certificate_key);
        check(mbedtls_x509write_crt_set_subject_name(&writer,"CN=ExtraPane LAN"));
        check(mbedtls_x509write_crt_set_issuer_name(&writer,"CN=ExtraPane LAN"));
        check(mbedtls_x509write_crt_set_validity(&writer,"20260101000000","99991231235959"));
        std::array<unsigned char,16> serial{};
        if(psa_generate_random(serial.data(),serial.size())!=PSA_SUCCESS)throw std::runtime_error("Cannot generate certificate serial");
        serial[0]=1;check(mbedtls_x509write_crt_set_serial_raw(&writer,serial.data(),serial.size()));
        std::array<unsigned char,2048> der{};const int size=mbedtls_x509write_crt_der(&writer,der.data(),der.size());
        if(size<0)tls_error("Encode session certificate",size);
        check(mbedtls_x509_crt_parse_der(&certificate,der.data()+der.size()-size,static_cast<size_t>(size)));
        check(mbedtls_ssl_conf_own_cert(&config,&certificate,&certificate_key));
    }
    bool retry(int code, Deadline deadline) {
        if (code != MBEDTLS_ERR_SSL_WANT_READ && code != MBEDTLS_ERR_SSL_WANT_WRITE) return false;
        socket.wait(code == MBEDTLS_ERR_SSL_WANT_WRITE, deadline);
        return true;
    }
};
TlsStream::TlsStream(net::TcpStream socket, bool server, const std::string& pair_name,std::optional<std::string> credential)
    : state_(std::make_unique<State>(std::move(socket))) {
    struct EraseCredential{std::optional<std::string>& value;~EraseCredential(){if(value)SecureZeroMemory(value->data(),value->size());}}eraseCredential{credential};
    if(credential&&!credential->empty()&&!access::validCredential(*credential))throw std::invalid_argument("Access credential is malformed");
    server_=server;
    std::lock_guard lock(crypto_mutex);
    if (psa_crypto_init() != PSA_SUCCESS) throw std::runtime_error("Initialize system-backed crypto failed");
    auto key = pair_name.empty()?Key{}:load_pairing(pair_name);
    const auto erase = [&] { SecureZeroMemory(key.data(), key.size()); };
    try {
        int result = mbedtls_ssl_config_defaults(&state_->config, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if (result) tls_error("TLS configuration", result);
        mbedtls_ssl_conf_min_tls_version(&state_->config, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_max_tls_version(&state_->config, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_tls13_key_exchange_modes(&state_->config, pair_name.empty()?MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL:MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL);
        mbedtls_ssl_conf_session_tickets(&state_->config, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
        mbedtls_ssl_conf_new_session_tickets(&state_->config, 0);
        if(pair_name.empty()) {
            // Explicit automatic LAN mode: encrypted, without peer identity
            // verification. No saved trust, codes, or shared application secret.
            mbedtls_ssl_conf_authmode(&state_->config,MBEDTLS_SSL_VERIFY_NONE);
            if(server)state_->ephemeral_certificate();
        } else {
            result = mbedtls_ssl_conf_psk(&state_->config, key.data(), key.size(), reinterpret_cast<const unsigned char*>(pair_name.data()), pair_name.size());
            if (result) tls_error("TLS pairing", result);
        }
        result = mbedtls_ssl_setup(&state_->ssl, &state_->config);
        if (result) tls_error("TLS setup", result);
        mbedtls_ssl_set_bio(&state_->ssl, &state_->socket, send_callback, recv_callback, nullptr);
    } catch (...) { erase(); throw; }
    erase();
    if(!server_){if(credential)credential_=*credential;else{std::lock_guard accessLock(accessCodeMutex);credential_=accessCode;}}
}
TlsStream::~TlsStream(){SecureZeroMemory(credential_.data(),credential_.size());if(control_gate_&&control_)control_gate_->close_control(control_);}
bool TlsStream::open_control(){
    if(!server_||control_)throw std::runtime_error("Invalid control admission");
    if(!protected_)return false;
    if(!accessGate)throw std::runtime_error("Control gate unavailable");
    control_gate_=accessGate;control_=protected_->open_control(*control_gate_);return true;
}
std::vector<access::ChannelCredential> TlsStream::control_channels(unsigned kind){
    if(!server_||kind>3)throw std::runtime_error("Invalid control resource");
    if(!protected_)return {};
    if(!control_gate_||!control_)throw std::runtime_error("Control not admitted");
    return control_gate_->control_channels(1u<<kind,control_);
}
std::vector<access::ChannelCredential> TlsStream::issue_channels(unsigned mask){
    if(!server_||!accessGate||!protected_)throw std::runtime_error("Grouped admission requires a one-time access code");
    return protected_->issue_channels(*accessGate,mask);
}
void TlsStream::require_resource(unsigned kind)const{
    if(protected_){const auto allowed=protected_->authorized_kind();if(allowed&&*allowed!=kind)throw std::runtime_error("Grouped credential does not authorize this resource");}
}
void TlsStream::cancel() noexcept { state_->socket.cancel(); }
void TlsStream::handshake(Deadline deadline) {
    struct Erase{std::string& value;~Erase(){SecureZeroMemory(value.data(),value.size());value.clear();}}erase{credential_};
    for (;;) {
        check_deadline(deadline);
        const int result = [&]{std::lock_guard lock(crypto_mutex);return mbedtls_ssl_handshake(&state_->ssl);}();
        if (result == 0) break;
        if (!state_->retry(result, deadline)) tls_error("TLS authentication failed", result);
    }
    if((server_ && accessGate) || (!server_ && !credential_.empty())){
        access_deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        protected_=std::make_unique<access::Stream>([this](std::span<uint8_t> bytes){read_raw(bytes,access_deadline_);},[this](std::span<const uint8_t> bytes){write_raw(bytes,access_deadline_);});
        if(server_)protected_->server(*accessGate);else protected_->client(credential_);
    }
}
void TlsStream::write_raw(std::span<const std::uint8_t> bytes, Deadline deadline) {
    while (!bytes.empty()) {
        check_deadline(deadline);
        const int result = [&]{std::lock_guard lock(crypto_mutex);return mbedtls_ssl_write(&state_->ssl, bytes.data(), std::min<std::size_t>(bytes.size(), 16384));}();
        if (result > 0) { bytes = bytes.subspan(static_cast<std::size_t>(result)); continue; }
        if (!state_->retry(result, deadline)) tls_error("TLS write", result);
    }
}
void TlsStream::read_raw(std::span<std::uint8_t> bytes, Deadline deadline) {
    while (!bytes.empty()) {
        check_deadline(deadline);
        const int result = [&]{std::lock_guard lock(crypto_mutex);return mbedtls_ssl_read(&state_->ssl, bytes.data(), bytes.size());}();
        if (result > 0) { bytes = bytes.subspan(static_cast<std::size_t>(result)); continue; }
        if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) throw std::runtime_error("TLS peer closed before the complete message");
        if (!state_->retry(result, deadline)) tls_error("TLS read", result);
    }
}
void TlsStream::close(Deadline deadline) {
    for (;;) {
        check_deadline(deadline);
        const int result = [&]{std::lock_guard lock(crypto_mutex);return mbedtls_ssl_close_notify(&state_->ssl);}();
        if (result == 0) return;
        if (!state_->retry(result, deadline)) tls_error("TLS close", result);
    }
}
std::string TlsStream::description() const {
    return std::string(mbedtls_ssl_get_version(&state_->ssl)) + " " + mbedtls_ssl_get_ciphersuite(&state_->ssl);
}

void session_check(const net::Options& options, const std::string& pair_name, bool server) {
    // Resolve trust before opening a port or trying to connect.
    auto key = load_pairing(pair_name);
    SecureZeroMemory(key.data(), key.size());
    auto socket = server ? net::accept_stream(options, [](const std::string& local) { std::cout << "LISTENING " << local << " TLS-only" << std::endl; })
                         : net::connect_stream(options);
    TlsStream stream(std::move(socket), server, pair_name);
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    stream.handshake(deadline);
    // Diagnostic application payload, with no media/input capabilities or grants.
    probe::Packet packet{};
    if (!server) {
        probe::Token token{};
        if (psa_generate_random(token.data(), token.size()) != PSA_SUCCESS) throw std::runtime_error("Random token generation failed");
        packet = probe::encode({probe::Kind::request, token});
        stream.write(packet, deadline);
        stream.read(packet, deadline);
        const auto response = probe::decode(packet);
        if (response.kind != probe::Kind::response || response.token != token) throw std::runtime_error("Secure session response mismatch");
    } else {
        stream.read(packet, deadline);
        const auto request = probe::decode(packet);
        if (request.kind != probe::Kind::request) throw std::runtime_error("Expected secure session request");
        stream.write(probe::encode({probe::Kind::response, request.token}), deadline);
    }
    stream.close(deadline);
    std::cout << "SESSION_OK " << stream.description() << " permissions=none capabilities=diagnostic-only" << std::endl;
}
}

namespace dm::security {
void TlsStream::peek(std::span<uint8_t> bytes,std::chrono::steady_clock::time_point deadline){
    access_deadline_=deadline;
    prefix_.peek(bytes,[&](auto data){if(protected_)protected_->read(data);else read_raw(data,deadline);});
}
void TlsStream::read(std::span<uint8_t> bytes,std::chrono::steady_clock::time_point deadline){
    access_deadline_=deadline;
    prefix_.read(bytes,[&](auto data){if(protected_)protected_->read(data);else read_raw(data,deadline);});
}
void TlsStream::write(std::span<const uint8_t> bytes,std::chrono::steady_clock::time_point deadline){access_deadline_=deadline;if(protected_)protected_->write(bytes);else write_raw(bytes,deadline);}
}
