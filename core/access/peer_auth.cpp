#include <dm/peer_auth.hpp>
#include <openssl/rand.h>
#include <openssl/mem.h>
#include <algorithm>
#include <stdexcept>
namespace dm::peer_auth {
namespace {
void need(bool ok){if(!ok)throw std::runtime_error("Peer authentication failed");}
bool nonzero(const DeviceId& id){return std::any_of(id.begin(),id.end(),[](auto b){return b!=0;});}
void wipe(std::string& s){if(!s.empty())OPENSSL_cleanse(s.data(),s.size());s.clear();}
struct Clean {std::string& s;~Clean(){wipe(s);}};
std::string hex(const Secret& secret){std::string s;for(auto b:secret.bytes){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
using Hello=std::array<uint8_t,72>;
Hello hello(bool server,Mode mode,const DeviceId& local,const DeviceId& peer){Hello h{'E','P','P',uint8_t(server?'S':'A'),1,uint8_t(mode)};std::copy(local.begin(),local.end(),h.begin()+8);std::copy(peer.begin(),peer.end(),h.begin()+40);return h;}
void validate(const Hello& h,bool server){need(h[0]=='E'&&h[1]=='P'&&h[2]=='P'&&h[3]==(server?'S':'A')&&h[4]==1&&h[5]<=3&&!h[6]&&!h[7]);}
DeviceId id(const Hello& h,size_t at){DeviceId r;std::copy_n(h.begin()+at,32,r.begin());return r;}
std::array<uint8_t,176> transcript(const Hello& a,const Hello& b,const DeviceId& binding){std::array<uint8_t,176> r;std::copy(a.begin(),a.end(),r.begin());std::copy(b.begin(),b.end(),r.begin()+72);std::copy(binding.begin(),binding.end(),r.begin()+144);return r;}
constexpr std::array<uint8_t,1> ack{1};
void expect(access::Stream& s){std::array<uint8_t,1> b{};s.read(b);need(b==ack);}
}
Secret::~Secret(){OPENSSL_cleanse(bytes.data(),bytes.size());}
DeviceId random_identity(){DeviceId id;do{need(RAND_bytes(id.data(),id.size())==1);}while(!nonzero(id));return id;}
bool valid_pairing_password(const std::string& value){
 if(value.empty()||value.size()>1024)return false;
 // Strict UTF-8, preserving spaces, punctuation and Unicode without normalization.
 for(size_t i=0;i<value.size();){unsigned c=static_cast<unsigned char>(value[i++]);if(!c)return false;if(c<128)continue;unsigned n=0,min=0,cp=0;
  if(c>=0xc2&&c<=0xdf){n=1;min=0x80;cp=c&31;}else if(c>=0xe0&&c<=0xef){n=2;min=0x800;cp=c&15;}else if(c>=0xf0&&c<=0xf4){n=3;min=0x10000;cp=c&7;}else return false;
  if(value.size()-i<n)return false;while(n--){unsigned next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80)return false;cp=(cp<<6)|(next&63);}if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
 }return true;
}
Policy::Policy(DeviceId local,bool required,std::function<access::Clock::time_point()> now):local_(local),required_(required),clock_(std::move(now)),window_(clock_()),rotated_(window_){need(nonzero(local_));generate_locked();}
void Policy::generate_locked(){
 std::string next;do{uint32_t value;do{need(RAND_bytes(reinterpret_cast<uint8_t*>(&value),sizeof(value))==1);}while(value>=4294000000u);next=std::to_string(value%1000000);next.insert(0,6-next.size(),'0');}while(next==code_);
 wipe(code_);code_=std::move(next);rotated_=clock_();
}
void Policy::refresh_locked(){
 const auto now=clock_();if(!custom_&&now-rotated_>=std::chrono::seconds(60))generate_locked();
 if(now-window_>=std::chrono::seconds(60)){window_=now;attempts_.clear();global_failures_=pair_failures_=0;}
}
Policy::~Policy(){stop();}
std::string Policy::code(){std::lock_guard lock(mutex_);need(active_);refresh_locked();return code_;}
PairingStatus Policy::status(){std::lock_guard lock(mutex_);need(active_);refresh_locked();if(custom_)return {{},true,0};const auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(clock_()-rotated_).count();return {code_,false,static_cast<unsigned>(60-std::clamp<decltype(elapsed)>(elapsed,0,59))};}
void Policy::set_password(std::string value){Clean clean{value};if(!value.empty()&&!valid_pairing_password(value))throw std::invalid_argument("Pairing password must be valid UTF-8 and at most 1024 bytes");std::lock_guard lock(mutex_);need(active_);if(value.empty()){custom_=false;generate_locked();}else{wipe(code_);code_=std::move(value);custom_=true;}}
void Policy::confirm_password(const std::string& value){std::lock_guard lock(mutex_);need(active_);refresh_locked();if(value.size()!=code_.size()||CRYPTO_memcmp(value.data(),code_.data(),value.size())!=0)throw access::PeerConfirmationError();}
bool Policy::pairing_available(){std::lock_guard lock(mutex_);if(!active_)return false;refresh_locked();return pair_failures_+pair_pending_<5;}
void Policy::set_required(bool required){std::lock_guard lock(mutex_);need(active_);required_=required;}
void Policy::stop(){std::lock_guard lock(mutex_);active_=false;wipe(code_);}
void Policy::ensure_active(){std::lock_guard lock(mutex_);need(active_);}
void Policy::begin(const DeviceId& peer,Mode mode){std::lock_guard lock(mutex_);need(active_&&nonzero(peer)&&uint8_t(mode)<=3);refresh_locked();
 need(global_failures_+pending_.size()<20&&pending_.size()<32&&!pending_.contains(peer));need(mode!=Mode::pair||pair_failures_+pair_pending_<5);need(mode!=Mode::open||!required_);
 auto found=attempts_.find(peer);need(found!=attempts_.end()||attempts_.size()<128);need(found==attempts_.end()||found->second.failures<5);attempts_.try_emplace(peer);pending_.insert(peer);if(mode==Mode::pair)++pair_pending_;
}
void Policy::finish(const DeviceId& peer,Mode mode,bool success)noexcept{std::lock_guard lock(mutex_);pending_.erase(peer);if(mode==Mode::pair&&pair_pending_)--pair_pending_;if(!success){if(global_failures_<20)++global_failures_;if(mode==Mode::pair&&pair_failures_<5)++pair_failures_;auto i=attempts_.find(peer);if(i!=attempts_.end()&&i->second.failures<5)++i->second.failures;}}
Channel::Channel(access::Read r,access::Write w):raw_read_(std::move(r)),raw_write_(std::move(w)){need(bool(raw_read_)&&bool(raw_write_));}
Identity Channel::client(ClientOptions options,const Remember& remember){need(!attempted_);attempted_=true;Clean clean{options.code};need(nonzero(options.local)&&nonzero(options.binding)&&options.outer_encrypted&&uint8_t(options.mode)<=3);
 if(options.mode==Mode::pair)need(valid_pairing_password(options.code));if(options.mode==Mode::remembered||options.mode==Mode::revocation)need(options.remembered.has_value()&&nonzero(options.expected_peer)&&options.expected_peer!=options.local);
 auto request=hello(false,options.mode,options.local,options.expected_peer);raw_write_(request);Hello response{};raw_read_(response);validate(response,true);auto peer=id(response,8);need(nonzero(peer)&&response[5]==uint8_t(options.mode)&&id(response,40)==options.local&&(!nonzero(options.expected_peer)||options.expected_peer==peer));Identity result;result.claimed_peer=peer;result.revocation_only=options.mode==Mode::revocation;
 if(options.mode==Mode::open){std::array<uint8_t,1> b{};raw_read_(b);need(b==ack);ready_=true;return result;}
 protected_=std::make_unique<access::Stream>(raw_read_,raw_write_);auto context=transcript(request,response,options.binding);auto secret=options.mode==Mode::pair?options.code:hex(*options.remembered);Clean erase{secret};try{protected_->peer_client(secret,context);}catch(const access::PeerConfirmationError&){if(options.mode!=Mode::pair)throw std::runtime_error("Remembered device authentication failed; forget and pair again");throw;}
 if(options.mode==Mode::pair&&peer!=options.local){need(bool(remember));Secret key;protected_->read(key.bytes);remember(peer,key);protected_->write(ack);expect(*protected_);result.pair_secret=key;result.newly_paired=true;}else{expect(*protected_);protected_->write(ack);expect(*protected_);}
 result.verified_peer=peer;ready_=true;return result;
}
Identity Channel::server(Policy& policy,const Lookup& lookup,const Remember& remember,bool encrypted,const DeviceId& binding,const Lookup& revocation_lookup){need(!attempted_);attempted_=true;need(encrypted&&nonzero(binding)&&bool(lookup));Hello request{};raw_read_(request);validate(request,false);auto peer=id(request,8);auto expected=id(request,40);auto mode=Mode(request[5]);need(!nonzero(expected)||expected==policy.local());policy.begin(peer,mode);
 struct Finish{Policy& p;DeviceId id;Mode mode;bool success=false;~Finish(){p.finish(id,mode,success);}}finish{policy,peer,mode};Identity result;result.claimed_peer=peer;result.revocation_only=mode==Mode::revocation;const auto& selected=mode==Mode::revocation?revocation_lookup:lookup;need(bool(selected));auto stored=selected(peer);if(mode==Mode::pair)need(!stored);if(mode==Mode::remembered||mode==Mode::revocation)need(stored.has_value()&&expected==policy.local()&&peer!=policy.local());auto response=hello(true,mode,policy.local(),peer);raw_write_(response);
 if(mode==Mode::open){policy.ensure_active();need(!policy.required());raw_write_(ack);ready_=true;finish.success=true;return result;}
 auto secret=mode==Mode::pair?policy.code():hex(*stored);Clean clean{secret};protected_=std::make_unique<access::Stream>(raw_read_,raw_write_);auto context=transcript(request,response,binding);try{protected_->peer_server(secret,context,[&]{if(mode==Mode::pair)policy.confirm_password(secret);});}catch(const access::PeerConfirmationError&){if(mode!=Mode::pair)throw std::runtime_error("Remembered device authentication failed; forget and pair again");throw;}policy.ensure_active();
 if(mode==Mode::pair&&peer!=policy.local()){need(bool(remember));Secret key;do{need(RAND_bytes(key.bytes.data(),key.bytes.size())==1);}while(!nonzero(key.bytes));protected_->write(key.bytes);expect(*protected_);need(!lookup(peer));policy.ensure_active();remember(peer,key);policy.ensure_active();protected_->write(ack);result.pair_secret=key;result.newly_paired=true;}else{if(mode==Mode::remembered||mode==Mode::revocation){auto current=selected(peer);need(current&&CRYPTO_memcmp(current->bytes.data(),stored->bytes.data(),32)==0);}protected_->write(ack);expect(*protected_);policy.ensure_active();if(mode==Mode::remembered||mode==Mode::revocation){auto current=selected(peer);need(current&&CRYPTO_memcmp(current->bytes.data(),stored->bytes.data(),32)==0);}protected_->write(ack);}
 if(mode==Mode::revocation)result.revocation_secret=stored;result.verified_peer=peer;ready_=true;finish.success=true;return result;
}
void Channel::read(std::span<uint8_t> bytes){need(ready_);try{if(protected_)protected_->read(bytes);else raw_read_(bytes);}catch(...){ready_=false;throw;}}
void Channel::write(std::span<const uint8_t> bytes){need(ready_);try{if(protected_)protected_->write(bytes);else raw_write_(bytes);}catch(...){ready_=false;throw;}}
}
