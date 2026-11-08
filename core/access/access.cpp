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
        need(ctx && (validCode(code)||(code.size()==64&&std::all_of(code.begin(),code.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))));need(SPAKE2_generate_msg(ctx.get(),message.data(),&size,message.size(),reinterpret_cast<const uint8_t*>(code.data()),code.size())==1);need(size==32);
    }
};
}
bool validCode(const std::string& code){return code.size()==6 && std::all_of(code.begin(),code.end(),[](char c){return c>='0' && c<='9';});}
std::optional<ChannelCredential> channelCredential(const std::string& text){
    if(text.size()!=105||text.substr(0,6)!="EPCT1:"||text[6]<'0'||text[6]>'4'||text[7]!=':'||text[40]!=':')return {};
    auto hex=[](std::string_view part){return std::all_of(part.begin(),part.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});};
    if(!hex(std::string_view(text).substr(8,32))||!hex(std::string_view(text).substr(41,64)))return {};
    return ChannelCredential{static_cast<unsigned>(text[6]-'0'),text.substr(8,32),text.substr(41,64)};
}
bool validCredential(const std::string& text){return validCode(text)||channelCredential(text).has_value();}
std::string formatCredential(const ChannelCredential& value){auto text="EPCT1:"+std::to_string(value.kind)+":"+value.id+":"+value.secret;need(channelCredential(text).has_value());return text;}
void Gate::clear_channels(){for(auto& [id,value]:channels_)wipe(value.secret);channels_.clear();}
std::vector<ChannelCredential> Gate::issue_channels(unsigned mask,uint64_t admitted_generation){
    std::lock_guard lock(mutex_);need(mask&&mask<=31&&used_&&admitted_generation&&generation_==admitted_generation&&now_()<expires_);
    return issue_locked(mask,0);
}
std::vector<ChannelCredential> Gate::issue_locked(unsigned mask,uint64_t control,std::optional<bool> display_input){
    need(!display_input.has_value()||mask==1);
    for(auto it=channels_.begin();it!=channels_.end();)if(now_()>=it->second.expires){wipe(it->second.secret);it=channels_.erase(it);}else ++it;
    unsigned requested=0;for(unsigned kind=0;kind<5;++kind)if(mask&(1u<<kind))++requested;
    need(channels_.size()+requested<=128);std::vector<ChannelCredential> result;
    auto random_hex=[](unsigned n){std::vector<uint8_t> bytes(n);need(RAND_bytes(bytes.data(),bytes.size())==1);std::string text;for(auto b:bytes){text+="0123456789abcdef"[b>>4];text+="0123456789abcdef"[b&15];}OPENSSL_cleanse(bytes.data(),bytes.size());return text;};
    for(unsigned kind=0;kind<5;++kind)if(mask&(1u<<kind)){
        ChannelCredential item{kind,random_hex(16),random_hex(32)};need(!channels_.contains(item.id));
        channels_.emplace(item.id,Channel{item.secret,kind,0,now_()+std::chrono::seconds(60),generation_,control,display_input});result.push_back(std::move(item));
    }
    return result;
}
// Authenticated controls live until explicitly closed. Outstanding admission
// tickets remain bounded separately; active sessions are not a ticket backlog.
uint64_t Gate::open_control(uint64_t admitted_generation){
    std::lock_guard lock(mutex_);need(used_&&admitted_generation&&generation_==admitted_generation&&now_()<expires_&&next_control_!=std::numeric_limits<uint64_t>::max());
    const auto id=++next_control_;controls_.emplace(id,true);return id;
}
std::vector<ChannelCredential> Gate::control_channels(unsigned mask,uint64_t control,std::optional<bool> display_input){
    std::lock_guard lock(mutex_);need(mask&&mask<=31&&control&&controls_.contains(control)&&(!display_input.has_value()||mask==1));
    // At most one unused ticket per kind belongs to a control connection.
    for(auto it=channels_.begin();it!=channels_.end();)if(it->second.control==control&&(mask&(1u<<it->second.kind))&&it->second.display_input==display_input){wipe(it->second.secret);it=channels_.erase(it);}else ++it;
    return issue_locked(mask,control,display_input);
}
void Gate::close_control(uint64_t control){
    std::lock_guard lock(mutex_);controls_.erase(control);
    for(auto it=channels_.begin();it!=channels_.end();)if(it->second.control==control){wipe(it->second.secret);it=channels_.erase(it);}else ++it;
}
Gate::Ticket Gate::begin_channel(const std::string& id,unsigned kind){
    std::lock_guard lock(mutex_);const auto found=channels_.find(id);need(found!=channels_.end());auto& value=found->second;
    need(kind==value.kind&&now_()<value.expires&&value.attempts<5&&(value.control?controls_.contains(value.control):value.generation==generation_));++value.attempts;return {value.secret,value.generation,value.display_input};
}
void Gate::consume_channel(const std::string& id,unsigned kind,uint64_t generation){
    std::lock_guard lock(mutex_);const auto found=channels_.find(id);need(found!=channels_.end());auto& value=found->second;
    need(kind==value.kind&&generation==value.generation&&(value.control?controls_.contains(value.control):generation==generation_)&&now_()<value.expires);wipe(value.secret);channels_.erase(found);
}
Gate::Status Gate::status(){std::lock_guard lock(mutex_);if(used_)return Status::used;if(now_()>=expires_)return Status::expired;if(attempts_>=5)return Status::exhausted;return Status::active;}
Gate::~Gate(){revoke();}
std::string Gate::generate(){
    uint32_t value=0;constexpr uint32_t limit=4294000000u;
    do{need(RAND_bytes(reinterpret_cast<uint8_t*>(&value),sizeof(value))==1);}while(value>=limit);
    auto code=std::to_string(value%1000000);code.insert(0,6-code.size(),'0');
    std::lock_guard lock(mutex_);for(auto it=channels_.begin();it!=channels_.end();)if(!it->second.control){wipe(it->second.secret);it=channels_.erase(it);}else ++it;wipe(code_);code_=code;expires_=now_()+std::chrono::minutes(5);++generation_;attempts_=0;used_=false;return code;
}
Gate::Ticket Gate::begin(){std::lock_guard lock(mutex_);need(!used_ && now_()<expires_ && attempts_<5);++attempts_;return {code_,generation_};}
void Gate::consume(uint64_t generation){std::lock_guard lock(mutex_);need(!used_ && generation==generation_ && now_()<expires_);used_=true;wipe(code_);}
void Gate::revoke(){std::lock_guard lock(mutex_);used_=true;++generation_;wipe(code_);clear_channels();controls_.clear();}
struct Stream::State {
    Read readRaw;Write writeRaw;bool ready=false,attempted=false,groupIssued=false;std::optional<unsigned> kind;std::optional<bool> display_input;uint64_t admitted_generation=0;uint64_t sent=0,received=0;
    std::array<uint8_t,128> keys{};bssl::UniquePtr<EVP_AEAD_CTX> tx,rx;Bytes buffered;size_t offset=0;
    State(Read r,Write w):readRaw(std::move(r)),writeRaw(std::move(w)){}
    ~State(){OPENSSL_cleanse(keys.data(),keys.size());if(!buffered.empty())OPENSSL_cleanse(buffered.data(),buffered.size());}
    std::array<uint8_t,32> proof(bool server){std::array<uint8_t,32> p{};unsigned n=0;const char* label=server?serverName:clientName;
        need(HMAC(EVP_sha256(),keys.data()+(server?96:64),32,reinterpret_cast<const uint8_t*>(label),strlen(label),p.data(),&n)!=nullptr && n==p.size());return p;}
    void derive(Exchange& exchange,std::span<const uint8_t> peer,bool server,std::span<const uint8_t> transcript={}){
        std::array<uint8_t,SPAKE2_MAX_KEY_SIZE> key{};size_t n=0;
        const int ok=SPAKE2_process_msg(exchange.ctx.get(),key.data(),&n,key.size(),peer.data(),peer.size());
        if(ok!=1){OPENSSL_cleanse(key.data(),key.size());need(false);}
        const char* info=transcript.empty()?"ExtraPane/access/1/record-and-confirmation-keys":"ExtraPane/peer/1/record-and-confirmation-keys";
        Bytes bound;
        if(!transcript.empty()){bound.assign(transcript.begin(),transcript.end());auto local=std::span(exchange.message).first(exchange.size);
            const auto client=server?peer:std::span<const uint8_t>(local),remote=server?std::span<const uint8_t>(local):peer;
            bound.insert(bound.end(),client.begin(),client.end());bound.insert(bound.end(),remote.begin(),remote.end());}
        const auto salt=transcript.empty()?std::span<const uint8_t>(hello):std::span<const uint8_t>(bound);
        const int derived=HKDF(keys.data(),keys.size(),EVP_sha256(),key.data(),n,salt.data(),salt.size(),reinterpret_cast<const uint8_t*>(info),strlen(info));
        OPENSSL_cleanse(key.data(),key.size());need(derived==1);
        tx.reset(EVP_AEAD_CTX_new(EVP_aead_aes_256_gcm(),keys.data()+(server?32:0),32,16));
        rx.reset(EVP_AEAD_CTX_new(EVP_aead_aes_256_gcm(),keys.data()+(server?0:32),32,16));need(tx && rx);
    }
};
Stream::Stream(Read r,Write w):state_(std::make_unique<State>(std::move(r),std::move(w))){}
Stream::~Stream()=default;
namespace {
std::array<uint8_t,32> ticket_hello(const ChannelCredential& ticket){
    std::array<uint8_t,32> header{'E','P','A','T',1,static_cast<uint8_t>(ticket.kind)};
    auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<16;++i)header[8+i]=static_cast<uint8_t>((nibble(ticket.id[i*2])<<4)|nibble(ticket.id[i*2+1]));return header;
}
std::optional<ChannelCredential> parse_ticket_hello(const std::array<uint8_t,32>& header){
    if(header[0]!='E'||header[1]!='P'||header[2]!='A'||header[3]!='T'||header[4]!=1||header[5]>4||header[6]||header[7]||!std::all_of(header.begin()+24,header.end(),[](uint8_t c){return c==0;}))return {};
    ChannelCredential ticket;ticket.kind=header[5];for(unsigned i=8;i<24;++i){ticket.id+="0123456789abcdef"[header[i]>>4];ticket.id+="0123456789abcdef"[header[i]&15];}return ticket;
}
}
void Stream::client(const std::string& code){auto& s=*state_;need(!s.attempted);s.attempted=true;need(validCredential(code));auto ticket=channelCredential(code);const auto header=ticket?ticket_hello(*ticket):hello;
    Exchange exchange(false,ticket?ticket->secret:code);if(ticket)wipe(ticket->secret);
    s.writeRaw(header);s.writeRaw(std::span(exchange.message).first(exchange.size));
    std::array<uint8_t,32> response{},peer{};s.readRaw(response);need(response==header);s.readRaw(peer);s.derive(exchange,peer,false);
    s.writeRaw(s.proof(false));std::array<uint8_t,32> proof{};s.readRaw(proof);auto expected=s.proof(true);need(CRYPTO_memcmp(proof.data(),expected.data(),proof.size())==0);s.ready=true;if(ticket)s.kind=ticket->kind;
}
void Stream::server(Gate& gate){auto& s=*state_;need(!s.attempted);s.attempted=true;
    std::array<uint8_t,32> request{},peer{};s.readRaw(request);auto channel=parse_ticket_hello(request);need(request==hello||channel.has_value());
    auto ticket=channel?gate.begin_channel(channel->id,channel->kind):gate.begin();
    struct Clean {std::string& code;~Clean(){wipe(code);}} clean{ticket.code};
    s.readRaw(peer);Exchange exchange(true,ticket.code);wipe(ticket.code);
    s.writeRaw(request);s.writeRaw(std::span(exchange.message).first(exchange.size));s.derive(exchange,peer,true);
    std::array<uint8_t,32> proof{};s.readRaw(proof);auto expected=s.proof(false);need(CRYPTO_memcmp(proof.data(),expected.data(),proof.size())==0);
    if(channel){gate.consume_channel(channel->id,channel->kind,ticket.generation);s.kind=channel->kind;s.display_input=ticket.display_input;}else{gate.consume(ticket.generation);s.admitted_generation=ticket.generation;}
    s.writeRaw(s.proof(true));s.ready=true;
}
void Stream::peer_client(const std::string& secret,std::span<const uint8_t> transcript){
    auto& s=*state_;need(!s.attempted);s.attempted=true;need(!transcript.empty()&&transcript.size()<=4096);
    Exchange exchange(false,secret);s.writeRaw(std::span(exchange.message).first(exchange.size));
    std::array<uint8_t,32> peer{};s.readRaw(peer);s.derive(exchange,peer,false,transcript);
    s.writeRaw(s.proof(false));std::array<uint8_t,32> proof{};s.readRaw(proof);const auto expected=s.proof(true);
    need(CRYPTO_memcmp(proof.data(),expected.data(),proof.size())==0);s.ready=true;
}
void Stream::peer_server(const std::string& secret,std::span<const uint8_t> transcript){
    auto& s=*state_;need(!s.attempted);s.attempted=true;need(!transcript.empty()&&transcript.size()<=4096);
    std::array<uint8_t,32> peer{};s.readRaw(peer);Exchange exchange(true,secret);
    s.writeRaw(std::span(exchange.message).first(exchange.size));s.derive(exchange,peer,true,transcript);
    std::array<uint8_t,32> proof{};s.readRaw(proof);const auto expected=s.proof(false);
    need(CRYPTO_memcmp(proof.data(),expected.data(),proof.size())==0);s.writeRaw(s.proof(true));s.ready=true;
}
std::optional<unsigned> Stream::authorized_kind()const{return state_->kind;}
std::optional<bool> Stream::authorized_display_input()const{return state_->display_input;}
std::vector<ChannelCredential> Stream::issue_channels(Gate& gate,unsigned mask){auto& s=*state_;need(s.ready&&!s.kind&&!s.groupIssued&&s.admitted_generation);auto result=gate.issue_channels(mask,s.admitted_generation);s.groupIssued=true;return result;}
uint64_t Stream::open_control(Gate& gate){auto& s=*state_;need(s.ready&&!s.kind&&!s.groupIssued&&s.admitted_generation);const auto id=gate.open_control(s.admitted_generation);s.groupIssued=true;return id;}
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
