#include <dm/peer_auth.hpp>
#include <condition_variable>
#include <deque>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace dm::peer_auth;
void check(bool ok){if(!ok)throw std::runtime_error("Peer auth check failed");}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(...){failed=true;}check(failed);}
struct Pipe{std::mutex mutex;std::condition_variable changed;std::deque<uint8_t> bytes;bool closed=false,tamper=false;std::vector<uint8_t> captured;
 void write(std::span<const uint8_t> b){std::lock_guard lock(mutex);if(closed)throw std::runtime_error("closed");for(auto c:b){bytes.push_back(c);captured.push_back(c);}if(tamper&&!b.empty())bytes.back()^=1;changed.notify_all();}
 void read(std::span<uint8_t> b){std::unique_lock lock(mutex);for(auto& c:b){if(!changed.wait_for(lock,std::chrono::seconds(2),[&]{return closed||!bytes.empty();})||bytes.empty())throw std::runtime_error("timeout");c=bytes.front();bytes.pop_front();}}
 void close(){std::lock_guard lock(mutex);closed=true;changed.notify_all();}
};
using Store=std::map<DeviceId,Secret>;
struct Pair{Pipe cs,sc;Channel c{[&](auto b){sc.read(b);},[&](auto b){cs.write(b);}},s{[&](auto b){cs.read(b);},[&](auto b){sc.write(b);}};Identity ci,si;
 bool run(Policy& p,ClientOptions o,Store& server,Store& client,DeviceId binding,Lookup custom={}){bool a=false,b=false;auto lookup=custom?custom:Lookup([&](auto id)->std::optional<Secret>{auto i=server.find(id);if(i==server.end())return {};return i->second;});
 std::thread t([&]{try{si=s.server(p,lookup,[&](auto id,auto key){check(!server.contains(id));server.emplace(id,key);},true,binding);b=true;}catch(...){cs.close();sc.close();}});
 try{ci=c.client(o,[&](auto id,auto key){check(!client.contains(id));client.emplace(id,key);});a=true;}catch(...){cs.close();sc.close();}t.join();return a&&b;}
};
int main(){try{auto host=random_identity(),device=random_identity(),binding=random_identity();Policy policy(host);Store server,client;ClientOptions o;o.local=device;o.binding=binding;o.outer_encrypted=true;o.code=policy.code();auto code=o.code;policy.set_required(false);check(!policy.required()&&policy.code()==code);policy.set_required(true);check(policy.required()&&policy.code()==code);
 Pair pair;check(pair.run(policy,o,server,client,binding));check(pair.ci.verified_peer==host&&pair.si.verified_peer==device&&pair.ci.newly_paired&&server.at(device).bytes==client.at(host).bytes);check(policy.code()==code);
 std::vector<uint8_t> data(150000,42),out(data.size());pair.c.write(data);pair.s.read(out);check(out==data);pair.s.write(data);pair.c.read(out);check(out==data);
 pair.cs.captured.clear();std::array<uint8_t,1> x{7},y{};pair.c.write(x);auto replay=pair.cs.captured;pair.s.read(y);check(y==x);pair.cs.write(replay);rejects([&]{pair.s.read(y);});rejects([&]{pair.s.read(y);});
 ClientOptions remembered=o;remembered.mode=Mode::remembered;remembered.expected_peer=host;remembered.remembered=client.at(host);Pair again;check(again.run(policy,remembered,server,client,binding));check(!again.ci.newly_paired);again.cs.tamper=true;again.c.write(x);rejects([&]{again.s.read(y);});
 {Pair bad;auto wrong=remembered;wrong.remembered->bytes[0]^=1;check(!bad.run(policy,wrong,server,client,binding));}
 {Pair bad;auto wrong=remembered;wrong.expected_peer=random_identity();check(!bad.run(policy,wrong,server,client,binding));}
 {Pair bad;check(!bad.run(policy,remembered,server,client,random_identity()));}
 {Pair bad;check(!bad.run(policy,o,server,client,binding));check(server.at(device).bytes==client.at(host).bytes);}
 {Pair bad;unsigned lookups=0;check(!bad.run(policy,remembered,server,client,binding,[&](auto)->std::optional<Secret>{if(++lookups>1)return {};return server.at(device);}));}
 // New clients reuse the startup code, without consuming or rotating it.
 {Policy p(host);Store a,b;auto opt=o;opt.code=p.code();Pair first;check(first.run(p,opt,a,b,binding));Store other;opt.local=random_identity();Pair second;check(second.run(p,opt,a,other,binding));check(p.code()==opt.code);}
 {Policy p(host);Store a,b;auto opt=o;opt.code=p.code();opt.code[0]=opt.code[0]=='0'?'1':'0';Pair bad;check(!bad.run(p,opt,a,b,binding));check(a.empty()&&b.empty());}
 {Policy p(host);Store a,b;auto opt=o;opt.local=host;opt.code=p.code();Pair self;check(self.run(p,opt,a,b,binding));check(a.empty()&&b.empty()&&self.ci.verified_peer==host&&!self.ci.newly_paired);}
 {Policy p(host,false);Store a,b;auto opt=o;opt.mode=Mode::open;Pair open;check(open.run(p,opt,a,b,binding));check(!open.ci.verified_peer&&!open.si.verified_peer&&a.empty()&&b.empty());}
 {Policy p(host);Store a,b;auto opt=o;opt.mode=Mode::open;Pair bad;check(!bad.run(p,opt,a,b,binding));}
 {Policy p(host,false);Store a,b;auto opt=o;opt.mode=Mode::open;opt.outer_encrypted=false;Pair bad;check(!bad.run(p,opt,a,b,binding));}
 {Policy p(host);Store a,b;Pair missing;check(!missing.run(p,remembered,a,b,binding));}
 {Policy p(host);Store a,b;auto opt=o;opt.binding={};Pair bad;check(!bad.run(p,opt,a,b,binding));}
 {Policy p(host);std::vector<DeviceId> pending;for(unsigned i=0;i<5;++i){pending.push_back(random_identity());p.begin(pending.back(),Mode::pair);}rejects([&]{p.begin(random_identity(),Mode::pair);});for(auto id:pending)p.finish(id,Mode::pair,true);p.begin(device,Mode::pair);p.finish(device,Mode::pair,true);}
 auto now=dm::access::Clock::now();Policy limited(host,true,[&]{return now;});for(unsigned i=0;i<5;++i){limited.begin(device,Mode::pair);limited.finish(device,Mode::pair,false);}check(!limited.pairing_available());now+=std::chrono::minutes(2);rejects([&]{limited.begin(random_identity(),Mode::pair);});limited.begin(device,Mode::remembered);limited.finish(device,Mode::remembered,true);limited.stop();rejects([&]{limited.begin(device,Mode::remembered);});
 std::cout<<"Peer auth checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
