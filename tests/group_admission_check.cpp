#include <dm/group.hpp>
#include <dm/control.hpp>
#include <condition_variable>
#include <deque>
#include <thread>
#include <iostream>
#include <atomic>
using namespace dm::access;
void check(bool value){if(!value)throw std::runtime_error("Grouped admission check failed");}
template<class F>void rejects(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
struct Pipe{std::mutex mutex;std::condition_variable changed;std::deque<uint8_t> bytes;bool closed=false;
 void write(std::span<const uint8_t> data){std::lock_guard lock(mutex);if(closed)throw std::runtime_error("closed");for(auto b:data)bytes.push_back(b);changed.notify_all();}
 void read(std::span<uint8_t> data){std::unique_lock lock(mutex);for(auto& b:data){if(!changed.wait_for(lock,std::chrono::seconds(2),[&]{return closed||!bytes.empty();})||bytes.empty())throw std::runtime_error("closed");b=bytes.front();bytes.pop_front();}}
 void close(){std::lock_guard lock(mutex);closed=true;changed.notify_all();}
};
struct Pair{Pipe out,in;Stream client{[&](auto b){in.read(b);},[&](auto b){out.write(b);}};Stream server{[&](auto b){out.read(b);},[&](auto b){in.write(b);}};
 bool authenticate(Gate& gate,const std::string& credential){bool a=false,b=false;std::thread worker([&]{try{server.server(gate);b=true;}catch(...){out.close();in.close();}});try{client.client(credential);a=true;}catch(...){out.close();in.close();}worker.join();return a&&b;}
};
int main(){try{
 auto now=Clock::now();Gate gate([&]{return now;});const auto code=gate.generate();Pair root;check(root.authenticate(gate,code));check(!root.server.authorized_kind());
 const auto request=group_request(15);root.client.write(request);GroupRequest received{};root.server.read(received);check(group_mask(received)==15);
 auto issued=root.server.issue_channels(gate,15);check(issued.size()==4);rejects([&]{root.server.issue_channels(gate,1);});
 auto reply=group_reply(15,issued);root.server.write(reply);GroupReply got{};root.client.read(got);auto tickets=group_tickets(got,15);check(tickets.size()==4);
 Pair codeReplay;check(!codeReplay.authenticate(gate,code));
 std::array<std::unique_ptr<Pair>,4> channels;
 for(unsigned kind=0;kind<4;++kind){channels[kind]=std::make_unique<Pair>();check(channels[kind]->authenticate(gate,formatCredential(tickets[kind])));check(channels[kind]->server.authorized_kind()==kind);rejects([&]{channels[kind]->server.issue_channels(gate,1);});}
 // Independent selected channels remain usable after siblings have closed.
 channels[0].reset();for(unsigned kind=1;kind<4;++kind){Bytes sample{static_cast<uint8_t>(kind),42};channels[kind]->client.write(sample);Bytes output(2);channels[kind]->server.read(output);check(output==sample);}
 for(const auto& ticket:tickets){Pair replay;check(!replay.authenticate(gate,formatCredential(ticket)));}
 // A selected-resource ticket cannot be turned into a different resource ticket.
 Pair next;check(next.authenticate(gate,gate.generate()));auto selected=next.server.issue_channels(gate,2);auto wrong=selected[0];wrong.kind=0;Pair mismatch;check(!mismatch.authenticate(gate,formatCredential(wrong)));Pair correct;check(correct.authenticate(gate,formatCredential(selected[0])));check(correct.server.authorized_kind()==1);
 // Failed proof does not consume the six-digit root code or authorize a child.
 Pair badRoot;auto fresh=gate.generate();auto wrongCode=fresh;wrongCode[0]=fresh[0]=='0'?'1':'0';check(!badRoot.authenticate(gate,wrongCode));Pair accepted;check(accepted.authenticate(gate,fresh));auto pending=accepted.server.issue_channels(gate,1);auto bad=pending[0];bad.secret[0]=bad.secret[0]=='a'?'b':'a';Pair wrongSecret;check(!wrongSecret.authenticate(gate,formatCredential(bad)));Pair rightSecret;check(rightSecret.authenticate(gate,formatCredential(pending[0])));
 // Expiry, regeneration, replay and competing consumption all fail closed.
 Pair expiry;check(expiry.authenticate(gate,gate.generate()));auto exp=expiry.server.issue_channels(gate,1);now+=std::chrono::seconds(61);Pair expired;check(!expired.authenticate(gate,formatCredential(exp[0])));
 Pair replaced;check(replaced.authenticate(gate,gate.generate()));auto old=replaced.server.issue_channels(gate,1);gate.generate();Pair revoked;check(!revoked.authenticate(gate,formatCredential(old[0])));
 Pair raceRoot;check(raceRoot.authenticate(gate,gate.generate()));auto race=raceRoot.server.issue_channels(gate,1);auto a=gate.begin_channel(race[0].id,0),b=gate.begin_channel(race[0].id,0);std::atomic_uint winners=0;std::thread x([&]{try{gate.consume_channel(race[0].id,0,a.generation);++winners;}catch(...){}}),y([&]{try{gate.consume_channel(race[0].id,0,b.generation);++winners;}catch(...){}});x.join();y.join();check(winners==1);
 rejects([&]{group_request(0);});rejects([&]{group_request(16);});auto malformed=request;malformed[15]=1;rejects([&]{group_mask(malformed);});auto extra=got;extra[6]=1;rejects([&]{group_tickets(extra,15);});check(!validCredential("EPCT1:0:bad"));
 // A live control connection survives its original OTP expiry and rotation.
 Pair live;check(live.authenticate(gate,gate.generate()));const auto control=live.server.open_control(gate);rejects([&]{live.server.open_control(gate);});rejects([&]{live.server.issue_channels(gate,1);});
 now+=std::chrono::minutes(6);auto liveTicket=gate.control_channels(2,control);auto liveWrong=liveTicket[0];liveWrong.kind=2;Pair liveMismatch;check(!liveMismatch.authenticate(gate,formatCredential(liveWrong)));
 gate.generate();Pair liveChild;check(liveChild.authenticate(gate,formatCredential(liveTicket[0])));Pair liveReplay;check(!liveReplay.authenticate(gate,formatCredential(liveTicket[0])));
 auto superseded=gate.control_channels(4,control);auto replacement=gate.control_channels(4,control);Pair supersededChild;check(!supersededChild.authenticate(gate,formatCredential(superseded[0])));Pair replacementChild;check(replacementChild.authenticate(gate,formatCredential(replacement[0])));
 auto closing=gate.control_channels(8,control);gate.close_control(control);Pair closedChild;check(!closedChild.authenticate(gate,formatCredential(closing[0])));rejects([&]{gate.control_channels(1,control);});gate.close_control(control);
 Pair live2;check(live2.authenticate(gate,gate.generate()));const auto control2=live2.server.open_control(gate);auto expiredLive=gate.control_channels(1,control2);now+=std::chrono::seconds(61);Pair expiredLiveChild;check(!expiredLiveChild.authenticate(gate,formatCredential(expiredLive[0])));auto revokedLive=gate.control_channels(1,control2);gate.revoke();Pair revokedLiveChild;check(!revokedLiveChild.authenticate(gate,formatCredential(revokedLive[0])));rejects([&]{gate.control_channels(1,control2);});
 for(auto op:{ControlOperation::open,ControlOperation::ping,ControlOperation::add,ControlOperation::close}){auto request=control_request(op);check(parse_control(request).first==op);request[15]=1;rejects([&]{parse_control(request);});}
 auto lan=control_reply(ControlOperation::add,2,false);check(control_credential(lan,ControlOperation::add,2,false).empty());rejects([&]{control_credential(lan,ControlOperation::add,2,true);});lan[127]=1;rejects([&]{control_credential(lan,ControlOperation::add,2,false);});
 auto coded=control_reply(ControlOperation::add,0,true,formatCredential(revokedLive[0]));check(control_credential(coded,ControlOperation::add,0,true)==formatCredential(revokedLive[0]));rejects([&]{control_credential(coded,ControlOperation::add,1,true);});
 std::cout<<"Grouped code admission, per-kind tickets, replay/expiry/revocation, concurrency and independent protected streams passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
