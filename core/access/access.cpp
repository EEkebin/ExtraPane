#include <dm/access.hpp>
#include <openssl/curve25519.h>
#include <openssl/aead.h>
#include <openssl/hkdf.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/mem.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace dm::access {
namespace {
constexpr size_t maxRecord=65536;
constexpr std::array<uint8_t,32> hello={'E','P','A','C',1};
constexpr char clientName[]="ExtraPane/access/1/client",serverName[]="ExtraPane/access/1/server";
void need(bool ok){if(!ok)throw std::runtime_error("Access-code authentication failed or expired");}
void wipe(std::string& value){if(!value.empty())OPENSSL_cleanse(value.data(),value.size());value.clear();}
void put(std::span<uint8_t> out,uint64_t value){for(size_t i=out.size();i>0;--i){out[i-1]=uint8_t(value);value>>=8;}}
uint64_t get(std::span<const uint8_t> in){uint64_t n=0;for(auto c:in)n=(n<<8)|c;return n;}
struct Exchange {
    bssl::UniquePtr<SPAKE2_CTX> ctx;
    std::array<uint8_t,SPAKE2_MAX_MSG_SIZE> message{};
    size_t size=0;
    Exchange(bool server,const std::string& code):ctx(SPAKE2_CTX_new(server?spake2_role_bob:spake2_role_alice,
        reinterpret_cast<const uint8_t*>(server?serverName:clientName),server?sizeof(serverName)-1:sizeof(clientName)-1,
        reinterpret_cast<const uint8_t*>(server?clientName:serverName),server?sizeof(clientName)-1:sizeof(serverName)-1)){
        need(ctx && validCode(code));need(SPAKE2_generate_msg(ctx.get(),message.data(),&size,message.size(),reinterpret_cast<const uint8_t*>(code.data()),code.size())==1);need(size==32);
    }
};
}
bool validCode(const std::string& code){return code.size()==6 && std::all_of(code.begin(),code.end(),[](char c){return c>='0' && c<='9';});}
Gate::Status Gate::status(){std::lock_guard lock(mutex_);if(used_)return Status::used;if(now_()>=expires_)return Status::expired;if(attempts_>=5)return Status::exhausted;return Status::active;}
Gate::~Gate(){revoke();}
std::string Gate::generate(){
    uint32_t value=0;constexpr uint32_t limit=4294000000u;
    do{need(RAND_bytes(reinterpret_cast<uint8_t*>(&value),sizeof(value))==1);}while(value>=limit);
    auto code=std::to_string(value%1000000);code.insert(0,6-code.size(),'0');
    std::lock_guard lock(mutex_);wipe(code_);code_=code;expires_=now_()+std::chrono::minutes(5);++generation_;attempts_=0;used_=false;return code;
}
Gate::Ticket Gate::begin(){std::lock_guard lock(mutex_);need(!used_ && now_()<expires_ && attempts_<5);++attempts_;return {code_,generation_};}
void Gate::consume(uint64_t generation){std::lock_guard lock(mutex_);need(!used_ && generation==generation_ && now_()<expires_);used_=true;wipe(code_);}
void Gate::revoke(){std::lock_guard lock(mutex_);used_=true;++generation_;wipe(code_);}
struct Stream::State {
    Read readRaw;Write writeRaw;bool ready=false,attempted=false;uint64_t sent=0,received=0;
    std::array<uint8_t,128> keys{};bssl::UniquePtr<EVP_AEAD_CTX> tx,rx;Bytes buffered;size_t offset=0;
    State(Read r,Write w):readRaw(std::move(r)),writeRaw(std::move(w)){}
    ~State(){OPENSSL_cleanse(keys.data(),keys.size());if(!buffered.empty())OPENSSL_cleanse(buffered.data(),buffered.size());}
    std::array<uint8_t,32> proof(bool server){std::array<uint8_t,32> p{};unsigned n=0;const char* label=server?serverName:clientName;
        need(HMAC(EVP_sha256(),keys.data()+(server?96:64),32,reinterpret_cast<const uint8_t*>(label),strlen(label),p.data(),&n)!=nullptr && n==p.size());return p;}
    void derive(Exchange& exchange,std::span<const uint8_t> peer,bool server){
        std::array<uint8_t,SPAKE2_MAX_KEY_SIZE> key{};size_t n=0;
        const int ok=SPAKE2_process_msg(exchange.ctx.get(),key.data(),&n,key.size(),peer.data(),peer.size());
        if(ok!=1){OPENSSL_cleanse(key.data(),key.size());need(false);}
        const char info[]="ExtraPane/access/1/record-and-confirmation-keys";
        const int derived=HKDF(keys.data(),keys.size(),EVP_sha256(),key.data(),n,hello.data(),hello.size(),reinterpret_cast<const uint8_t*>(info),sizeof(info)-1);
        OPENSSL_cleanse(key.data(),key.size());need(derived==1);
        tx.reset(EVP_AEAD_CTX_new(EVP_aead_aes_256_gcm(),keys.data()+(server?32:0),32,16));
        rx.reset(EVP_AEAD_CTX_new(EVP_aead_aes_256_gcm(),keys.data()+(server?0:32),32,16));need(tx && rx);
    }
};
Stream::Stream(Read r,Write w):state_(std::make_unique<State>(std::move(r),std::move(w))){}
Stream::~Stream()=default;
void Stream::client(const std::string& code){auto& s=*state_;need(!s.attempted);s.attempted=true;Exchange exchange(false,code);
    s.writeRaw(hello);s.writeRaw(std::span(exchange.message).first(exchange.size));
    std::array<uint8_t,32> response{},peer{};s.readRaw(response);need(response==hello);s.readRaw(peer);s.derive(exchange,peer,false);
    s.writeRaw(s.proof(false));std::array<uint8_t,32> proof{};s.readRaw(proof);auto expected=s.proof(true);need(CRYPTO_memcmp(proof.data(),expected.data(),proof.size())==0);s.ready=true;
}
void Stream::server(Gate& gate){auto& s=*state_;need(!s.attempted);s.attempted=true;auto ticket=gate.begin();
    struct Clean {std::string& code;~Clean(){wipe(code);}} clean{ticket.code};
    std::array<uint8_t,32> request{},peer{};s.readRaw(request);need(request==hello);s.readRaw(peer);Exchange exchange(true,ticket.code);wipe(ticket.code);
    s.writeRaw(hello);s.writeRaw(std::span(exchange.message).first(exchange.size));s.derive(exchange,peer,true);
    std::array<uint8_t,32> proof{};s.readRaw(proof);auto expected=s.proof(false);need(CRYPTO_memcmp(proof.data(),expected.data(),proof.size())==0);
    gate.consume(ticket.generation);s.writeRaw(s.proof(true));s.ready=true;
}
void Stream::write(std::span<const uint8_t> bytes){auto& s=*state_;need(s.ready);struct Guard{bool& ready;bool ok=false;~Guard(){if(!ok)ready=false;}}guard{s.ready};
    while(!bytes.empty()){
        need(s.sent!=std::numeric_limits<uint64_t>::max());size_t n=std::min(bytes.size(),maxRecord);
        std::array<uint8_t,12> header{},nonce{};put(std::span(header).first(8),s.sent);put(std::span(header).last(4),n);put(std::span(nonce).last(8),s.sent);
        Bytes encrypted(n+16);size_t written=0;need(EVP_AEAD_CTX_seal(s.tx.get(),encrypted.data(),&written,encrypted.size(),nonce.data(),nonce.size(),bytes.data(),n,header.data(),header.size())==1 && written==encrypted.size());
        s.writeRaw(header);s.writeRaw(encrypted);++s.sent;bytes=bytes.subspan(n);
    }guard.ok=true;
}
void Stream::read(std::span<uint8_t> bytes){auto& s=*state_;need(s.ready);struct Guard{bool& ready;bool ok=false;~Guard(){if(!ok)ready=false;}}guard{s.ready};
    while(!bytes.empty()){
        if(s.offset==s.buffered.size()){
            need(s.received!=std::numeric_limits<uint64_t>::max());std::array<uint8_t,12> header{},nonce{};s.readRaw(header);
            need(get(std::span(header).first(8))==s.received);const auto n=get(std::span(header).last(4));need(n>0 && n<=maxRecord);
            Bytes encrypted(n+16);s.readRaw(encrypted);put(std::span(nonce).last(8),s.received);s.buffered.resize(n);size_t written=0;
            need(EVP_AEAD_CTX_open(s.rx.get(),s.buffered.data(),&written,s.buffered.size(),nonce.data(),nonce.size(),encrypted.data(),encrypted.size(),header.data(),header.size())==1 && written==n);
            ++s.received;s.offset=0;
        }
        auto n=std::min(bytes.size(),s.buffered.size()-s.offset);std::copy_n(s.buffered.data()+s.offset,n,bytes.data());s.offset+=n;bytes=bytes.subspan(n);
    }guard.ok=true;
}
}
