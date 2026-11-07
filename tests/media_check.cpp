#include <extrapane/media.hpp>
#include <extrapane/camera_fanout.hpp>
#include <algorithm>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace extrapane::media;
using namespace std::chrono_literals;
void check(bool value) { if(!value) throw std::runtime_error("Media check failed"); }
template<class F> bool rejects(F action) {try {action();return false;} catch(const std::exception&){return true;}}
int main() {try {
    Frame f{{7,9},0,-100,{1,2,3}};
    const auto header=encode_header(f);
    check(header==encode_header(f.binding,f.sequence,f.timestamp,f.payload.size()));
    auto decoded=decode_header(header);
    check(decoded.binding==f.binding && decoded.timestamp==-100 && decoded.payload.size()==3);
    for(std::size_t n=0;n<header.size();++n)
        check(rejects([&]{decode_header(std::span(header).first(n));}));
    for(const auto offset:{0,4,5,6,7,44,47}) {
        auto bad=header;bad[offset]^=0x80;check(rejects([&]{decode_header(bad);}));
    }
    auto huge=header;huge[40]=0xff;check(rejects([&]{decode_header(huge);}));
    extrapane::wire::Bytes bytes;std::size_t cursor=0;int reads=0;
    auto read=[&](std::span<std::uint8_t> out){
        ++reads;if(out.size()>bytes.size()-cursor)throw std::runtime_error("Truncated stream");
        std::copy_n(bytes.begin()+cursor,out.size(),out.begin());cursor+=out.size();
    };
    auto write=[&](std::span<const std::uint8_t> data){bytes.insert(bytes.end(),data.begin(),data.end());};
    Channel channel(f.binding,read,write);channel.send(f);check(channel.receive().payload==f.payload);
    check(rejects([&]{channel.send(f);}));check(channel.failed());
    check(rejects([&]{channel.receive();}));
    bytes.assign(header.begin(),header.end());bytes.insert(bytes.end(),f.payload.begin(),f.payload.end());cursor=0;reads=0;
    Channel stale({7,10},read,write);check(rejects([&]{stale.receive();}));check(reads==1 && stale.failed());
    bytes.assign(huge.begin(),huge.end());cursor=0;reads=0;
    Channel bounded(f.binding,read,write);check(rejects([&]{bounded.receive();}));check(reads==1);
    bytes.assign(header.begin(),header.end());cursor=0;
    Channel truncated(f.binding,read,write);check(rejects([&]{truncated.receive();}));check(truncated.failed());
    FrameQueue queue(f.binding,2);check(queue.push(f));f.sequence=1;check(queue.push(f));
    f.sequence=2;check(!queue.push(f));check(queue.closed());check(!queue.pop_for(0ms));
    FrameQueue restarted({7,10});check(!restarted.push(f));check(restarted.closed());
    FrameQueue order(f.binding);check(order.push(f));check(order.pop_for(0ms)->sequence==2);
    check(!order.push(f));check(order.closed());
    FrameQueue waiting(f.binding);
    auto waiter=std::async(std::launch::async,[&]{return waiting.pop_for(5s);});
    waiting.close();check(waiter.wait_for(1s)==std::future_status::ready);check(!waiter.get());
    {
        extrapane::camera::Fanout fanout;
        auto a=fanout.add("first",{21,1});a->admitted=true;
        fanout.publish(10,100,{1,2,3},false);check(!a->frames.pop_for(0ms)); // Await an independent encoder start.
        fanout.publish(11,110,{1,2,3},true);auto first=a->frames.pop_for(0ms);check(first&&first->binding==Binding{21,1}&&first->sequence==11);
        auto b=fanout.add("late",{22,7}); // Unadmitted recipients receive no media.
        fanout.publish(12,120,{4,5},true);check(!b->frames.pop_for(0ms));check(a->frames.pop_for(0ms).has_value());
        b->admitted=true;fanout.publish(13,130,{6},false);check(!b->frames.pop_for(0ms));check(a->frames.pop_for(0ms).has_value());
        fanout.publish(14,140,{7},true);auto joined=b->frames.pop_for(0ms);check(joined&&joined->binding==Binding{22,7});check(a->frames.pop_for(0ms).has_value());
        for(unsigned i=15;i<20;++i){fanout.publish(i,i*10,{8,9},false);check(a->frames.pop_for(0ms).has_value());}
        check(b->failed&&b->frames.closed()&&!a->failed); // Only slow recipient fails.
        fanout.remove("late");fanout.publish(20,200,{10},false);check(a->frames.pop_for(0ms).has_value());
        check(rejects([&]{fanout.add("first",{23,1});}));
        fanout.remove("first");check(a->frames.closed());
        auto replacement=fanout.add("first",{23,2});replacement->admitted=true;
        fanout.publish(21,210,{1},false);check(!replacement->frames.pop_for(0ms));
        fanout.publish(22,220,{2},true);check(replacement->frames.pop_for(0ms)->binding==Binding{23,2});
        for(unsigned i=0;i<7;++i)fanout.add("row"+std::to_string(i),{30+i,1});
        check(rejects([&]{fanout.add("too-many",{50,1});}));
        check(rejects([&]{fanout.publish(23,230,{},true);}));
    }
    std::cout<<"Media framing, stale generation, bounds and cancellation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
