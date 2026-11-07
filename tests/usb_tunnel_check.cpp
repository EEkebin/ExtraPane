#include <extrapane/usb_tunnel.hpp>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace extrapane::usb::tunnel;
void check(bool ok){if(!ok)throw std::runtime_error("USB tunnel isolated check failed");}
void rejects(const std::function<void()>& f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected);}
Frame wire(Frame f){auto parsed=decode(encode(f));check(parsed.payload.size()==f.payload.size());parsed.payload=std::move(f.payload);return parsed;}
std::vector<std::uint8_t> import(std::string bus){std::vector<std::uint8_t> out(40);out[0]=1;out[1]=0x11;out[2]=0x80;out[3]=3;std::copy(bus.begin(),bus.end(),out.begin()+8);return out;}
int main(){
    {Pump sender,receiver("1-2");auto bytes=import("1-2");receiver.accept(wire(sender.next(std::span(bytes).first(10))));check(receiver.pending().empty());sender.accept(wire(receiver.next()));receiver.accept(wire(sender.next(std::span(bytes).subspan(10))));check(std::ranges::equal(receiver.pending(),bytes));receiver.consume(40);check(receiver.pending().empty());}
    for(auto bus:{"1-3",""}){Pump sender,receiver("1-2");auto bytes=import(bus);rejects([&]{receiver.accept(wire(sender.next(bytes)));});}
    {Pump sender,receiver("1-2");auto bytes=import("1-2");bytes[3]=5;rejects([&]{receiver.accept(sender.next(bytes));});}
    {Pump sender,receiver;std::vector<std::uint8_t> bytes(maximum_payload,0x7b);for(unsigned i=0;i<capacity/maximum_payload;++i){receiver.accept(wire(sender.next(bytes)));sender.accept(wire(receiver.next()));}check(sender.read_limit()==0&&receiver.pending().size()==capacity);rejects([&]{sender.next(std::span(bytes).first(1));});receiver.consume(31);receiver.accept(wire(sender.next()));sender.accept(wire(receiver.next()));check(sender.read_limit()==31);receiver.accept(wire(sender.next(std::span(bytes).first(31))));check(receiver.pending().size()==capacity);}
    {Pump a,b;auto f=a.next();b.accept(f);rejects([&]{b.accept(f);});}
    {Frame f;f.payload.resize(maximum_payload+1);rejects([&]{encode(f);});Header h{};rejects([&]{decode(h);});h=encode(Frame{});h[16]=0xff;rejects([&]{decode(h);});h=encode(Frame{});h[6]=1;rejects([&]{decode(h);});}
    {Pump a,b;b.accept(a.next({},true));check(b.stopped()&&b.pending().empty());rejects([&]{b.next();});}
    // Bidirectional fake backpressure with different sink drain rates. No sockets.
    {Pump a,b;std::vector<std::uint8_t> left(150000,0x11),right(130000,0x22),gotLeft,gotRight;std::size_t li=0,ri=0;
        for(unsigned tick=0;tick<5000&&(gotLeft.size()<left.size()||gotRight.size()<right.size());++tick){
            auto n=std::min(a.read_limit(),left.size()-li);b.accept(wire(a.next(std::span(left).subspan(li,n))));li+=n;
            n=std::min(b.read_limit(),right.size()-ri);a.accept(wire(b.next(std::span(right).subspan(ri,n))));ri+=n;
            n=std::min<std::size_t>(a.pending().size(),137);gotRight.insert(gotRight.end(),a.pending().begin(),a.pending().begin()+n);a.consume(n);
            n=std::min<std::size_t>(b.pending().size(),191);gotLeft.insert(gotLeft.end(),b.pending().begin(),b.pending().begin()+n);b.consume(n);
        }check(gotLeft==left&&gotRight==right);
    }
    std::cout<<"USB_TUNNEL_CHECK_OK\n";
}
