#include <extrapane/media.hpp>
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
    std::cout<<"Media framing, stale generation, bounds and cancellation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
