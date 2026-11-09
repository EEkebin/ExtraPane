#include <dm/access.hpp>
#include <condition_variable>
#include <deque>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <memory>
#include <set>
using namespace dm::access;
void check(bool ok){if(!ok)throw std::runtime_error("Access check failed");}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
struct Pipe {
    std::mutex mutex;std::condition_variable changed;std::deque<uint8_t> bytes;bool closed=false,tamper=false;
    void write(std::span<const uint8_t> b){std::lock_guard lock(mutex);if(closed)throw std::runtime_error("closed");for(auto v:b)bytes.push_back(v);if(tamper&&!b.empty())bytes.back()^=1;changed.notify_all();}
    void read(std::span<uint8_t> b){std::unique_lock lock(mutex);for(auto& value:b){if(!changed.wait_for(lock,std::chrono::seconds(2),[&]{return closed||!bytes.empty();})||bytes.empty())throw std::runtime_error("closed or timed out");value=bytes.front();bytes.pop_front();}}
    void close(){std::lock_guard lock(mutex);closed=true;changed.notify_all();}
};
struct Pair {
    Pipe c2s,s2c;
    Stream client{[&](auto b){s2c.read(b);},[&](auto b){c2s.write(b);}};
    Stream server{[&](auto b){c2s.read(b);},[&](auto b){s2c.write(b);}};
    bool authenticate(Gate& gate,const std::string& code){bool a=false,b=false;
        std::thread t([&]{try{server.server(gate);b=true;}catch(...){c2s.close();s2c.close();}});
        try{client.client(code);a=true;}catch(...){c2s.close();s2c.close();}t.join();return a&&b;
    }
};
int main(){try{
    check(validCode("000001")&&!validCode("12345")&&!validCode("1234567")&&!validCode("12 456"));
    Gate gate;auto code=gate.generate();check(validCode(code));
    Pair good;check(good.authenticate(gate,code));rejects([&]{gate.begin();});
    Bytes original(150000);for(size_t i=0;i<original.size();++i)original[i]=uint8_t(i);
    good.client.write(original);Bytes received(original.size());good.server.read(std::span(received).first(17));good.server.read(std::span(received).subspan(17));check(original==received);
    good.server.write(original);good.client.read(received);check(original==received);
    good.c2s.tamper=true;good.client.write(Bytes{1,2,3});rejects([&]{Bytes out(3);good.server.read(out);});
    code=gate.generate();Pair wrong;auto wrongCode=code;wrongCode[0]=code[0]=='0'?'1':'0';check(!wrong.authenticate(gate,wrongCode));
    auto stale=gate.begin();gate.generate();rejects([&]{gate.consume(stale.generation);});
    gate.generate();for(int i=0;i<5;++i)gate.begin();rejects([&]{gate.begin();});
    gate.generate();auto one=gate.begin(),two=gate.begin();std::atomic_int accepted=0;
    std::thread a([&]{try{gate.consume(one.generation);++accepted;}catch(...){}}),b([&]{try{gate.consume(two.generation);++accepted;}catch(...){}});a.join();b.join();check(accepted==1);
    auto now=Clock::now();Gate expiring([&]{return now;});expiring.generate();auto pending=expiring.begin();now+=std::chrono::minutes(6);rejects([&]{expiring.begin();});rejects([&]{expiring.consume(pending.generation);});check(expiring.status()==Gate::Status::expired);
    gate.generate();gate.revoke();rejects([&]{gate.begin();});
    // Used codes cannot authenticate another session; replacing a code does not
    // revoke an already authenticated stream.
    code=gate.generate();Pair established;check(established.authenticate(gate,code));Pair replay;check(!replay.authenticate(gate,code));gate.generate();
    established.client.write(Bytes{42});Bytes single(1);established.server.read(single);check(single[0]==42);
    // More than sixteen independently authenticated control sessions may stay
    // admitted. Consumed media tickets must not count as outstanding backlog.
    Gate many;
    std::vector<std::unique_ptr<Pair>> sessions;
    std::set<uint64_t> controls;
    for(int index=0;index<40;++index){
        auto session=std::make_unique<Pair>();check(session->authenticate(many,many.generate()));
        const auto control=session->server.open_control(many);check(controls.insert(control).second);
        rejects([&]{session->server.open_control(many);});
        const auto channels=many.control_channels(15,control);check(channels.size()==4);
        for(const auto& channel:channels){
            const auto ticket=many.begin_channel(channel.id,channel.kind);
            many.consume_channel(channel.id,channel.kind,ticket.generation);
            rejects([&]{many.begin_channel(channel.id,channel.kind);});
        }
        sessions.push_back(std::move(session));
    }
    // Regeneration has not revoked older controls; each can request again.
    for(const auto control:controls){
        const auto channels=many.control_channels(1,control);check(channels.size()==1);
        const auto ticket=many.begin_channel(channels[0].id,0);
        many.consume_channel(channels[0].id,0,ticket.generation);
    }
    // Keep the 128 unused-ticket safety bound, independent of active controls.
    std::vector<ChannelCredential> backlog;
    auto controlIt=controls.begin();
    for(int index=0;index<32;++index,++controlIt){
        const auto channels=many.control_channels(15,*controlIt);
        backlog.insert(backlog.end(),channels.begin(),channels.end());
    }
    rejects([&]{many.control_channels(15,*controlIt);});
    for(size_t index=0;index<4;++index){
        const auto& channel=backlog[index];const auto ticket=many.begin_channel(channel.id,channel.kind);
        many.consume_channel(channel.id,channel.kind,ticket.generation);
    }
    check(many.control_channels(15,*controlIt).size()==4);
    for(const auto control:controls){many.close_control(control);rejects([&]{many.control_channels(1,control);});}
    rejects([&]{many.begin_channel(backlog.back().id,backlog.back().kind);});
    // Reject replayed, reordered, oversized and truncated records. A failed
    // stream stays unusable, preventing nonce/state reuse after an exception.
    for(int mode=0;mode<4;++mode){
        Pair pair;check(pair.authenticate(gate,gate.generate()));pair.client.write(Bytes{1,2,3});
        auto packet=pair.c2s.bytes;
        if(mode==0){Bytes first(3);pair.server.read(first);pair.c2s.bytes=packet;}
        if(mode==1)pair.c2s.bytes[7]=9;
        if(mode==2)pair.c2s.bytes[8]=127;
        if(mode==3){pair.c2s.bytes.pop_back();pair.c2s.close();}
        rejects([&]{Bytes out(3);pair.server.read(out);});
        rejects([&]{pair.server.write(Bytes{9});});
    }
    // The protected gate never treats the legacy media request as admission.
    Pair legacy;gate.generate();Bytes request(32);request[0]='D';request[1]='M';legacy.c2s.write(request);
    rejects([&]{legacy.server.server(gate);});
    Pair downgrade;Bytes badHello(32);downgrade.s2c.write(badHello);
    rejects([&]{downgrade.client.client("000001");});
    std::cout<<"Access authentication, one-use admission, retry budget, regeneration, concurrency, chunked encrypted records and tamper rejection passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
