#include <extrapane/peer_rpc.hpp>
#include <iostream>
#include <stdexcept>
using namespace extrapane::peers;
using namespace extrapane::peers::rpc;
void need(bool value){if(!value)throw std::runtime_error("Peer RPC check failed");}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid RPC state accepted");}
int main(){try{
    DeviceId id{};id[0]=1;Open request{id,"screen-1",false,1920,1080,60};auto body=rpc::encode(request);auto parsed=decode_open(body);need(parsed.owner==id&&parsed.resource=="screen-1"&&parsed.width==1920&&!parsed.input);
    request.native_credential="EPCT1.test-ticket";need(decode_open(rpc::encode(request)).native_credential==request.native_credential);
    auto badTicket=request;badTicket.native_credential.assign(106,'x');rejects([&]{rpc::encode(badTicket);});badTicket.native_credential="bad\nvalue";rejects([&]{rpc::encode(badTicket);});
    auto truncated=rpc::encode(request);truncated.pop_back();rejects([&]{decode_open(truncated);});
    auto invalid=request;invalid.refresh=0;rejects([&]{rpc::encode(invalid);});body[32]=3;rejects([&]{decode_open(body);});
    const auto bytes=rpc::encode(Message{Operation::open,42,rpc::encode(request)});Decoder decoder;std::vector<Message> messages;
    for(const auto byte:bytes){auto next=decoder.feed(std::span(&byte,1));messages.insert(messages.end(),next.begin(),next.end());}need(messages.size()==1&&messages[0].id==42);
    auto bad=bytes;bad[6]=1;Decoder poisoned;rejects([&]{poisoned.feed(bad);});rejects([&]{poisoned.feed(bytes);});need(poisoned.failed());
    auto empty=encode_catalogs({});need(decode_catalogs(empty).empty());empty.push_back(0);rejects([&]{decode_catalogs(empty);});
    auto granted=rpc::encode(Granted{9,"EPCT1.ticket"});need(decode_granted(granted).grant==9&&decode_granted(granted).native_credential=="EPCT1.ticket");granted.push_back(0);rejects([&]{decode_granted(granted);});rejects([&]{rpc::encode(Granted{0,{}});});
    bool allowed=true;Multiplexer sender(true,[&](auto grant){return allowed&&grant==17;});Multiplexer receiver(false,[&](auto grant){return allowed&&grant==17;});
    auto opening=sender.open(17);receiver.opened(opening);rejects([&]{receiver.opened(opening);});
    const Bytes chunk(data_chunk,73);std::size_t count=0;while(auto data=sender.send(opening.id,chunk)){receiver.receive(*data);count+=data->payload.size();}need(count==Multiplexer::window&&receiver.queued()==count);
    need(receiver.pending(opening.id).size()==count);auto credit=receiver.consumed(opening.id,1000);sender.credited(credit);need(receiver.queued()==count-1000);auto next=sender.send(opening.id,chunk);need(next&&next->payload.size()==1000);receiver.receive(*next);
    rejects([&]{sender.credited({Operation::stream_credit,opening.id,integer(Multiplexer::window+1)});});
    auto replay=opening;need(receiver.close(opening.id));rejects([&]{receiver.opened(replay);});
    auto second=sender.open(17);receiver.opened(second);receiver.receive(*sender.send(second.id,chunk));allowed=false;rejects([&]{sender.send(second.id,chunk);});rejects([&]{receiver.pending(second.id);});auto stopped=receiver.revoke(17);need(stopped.size()==1&&receiver.queued()==0);
    rejects([&]{sender.open(999);});
    std::cout<<"Peer RPC framing, partial reads, replay rejection, flow control and grant revocation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
