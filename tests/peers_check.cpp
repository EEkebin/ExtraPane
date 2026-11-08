#include <extrapane/peers.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace extrapane::peers;
namespace {
void check(bool v){if(!v)throw std::runtime_error("Peer policy check failed");}
template<class F>void rejected(F f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}check(failed);}
DeviceId id(unsigned value){DeviceId r{};r[0]=static_cast<std::uint8_t>(value);return r;}
Catalog catalog(unsigned owner){return {id(owner),11,1,"Example device",{{"camera-1","Camera",Kind::camera,true,false,{}},{"screen-1","Existing screen",Kind::existing_screen,true,false,{}},{"usb-1","USB storage",Kind::usb,true,false,{}},{"display-1","Use as extended display",Kind::display_destination,true,true,{}}}};}
}
int main(){try{
    Announcement announce{id(1),4,1,"Surface",43824,10,63,true};
    auto bytes=encode(announce);check(decode_announcement(bytes)==announce);
    Withdrawal withdrawal{id(1),4,2};check(decode_withdrawal(encode(withdrawal))==withdrawal);
    auto local=catalog(1);check(decode_catalog(encode(local))==local);
    auto preset=local;preset.offers.back().width=3000;preset.offers.back().height=2000;preset.offers.back().refresh=500;check(decode_catalog(encode(preset))==preset);
    preset.offers.back().height=0;rejected([&]{encode(preset);});preset=local;preset.offers[0].width=1920;preset.offers[0].height=1080;preset.offers[0].refresh=60;rejected([&]{encode(preset);});
    for(std::size_t n=0;n<bytes.size();++n)rejected([&]{decode_announcement(std::span(bytes).first(n));});
    auto wire=encode(local);for(std::size_t n=0;n<wire.size();++n)rejected([&]{decode_catalog(std::span(wire).first(n));});
    auto invalid=bytes;invalid.push_back(0);rejected([&]{decode_announcement(invalid);});invalid=bytes;invalid[7]=1;rejected([&]{decode_announcement(invalid);});
    invalid=bytes;invalid[6]=3;rejected([&]{decode_announcement(invalid);});
    auto bad=announce;bad.device={};rejected([&]{encode(bad);});bad=announce;bad.lifetime_seconds=121;rejected([&]{encode(bad);});bad=announce;bad.offered_kinds=64;rejected([&]{encode(bad);});bad=announce;bad.name="invalid\nname";rejected([&]{encode(bad);});bad=announce;bad.name="\xc0\xaf";rejected([&]{encode(bad);});
    auto duplicate=local;duplicate.offers.push_back(duplicate.offers[0]);rejected([&]{encode(duplicate);});
    auto invalidOffer=local;invalidOffer.offers[0].input_allowed=true;rejected([&]{encode(invalidOffer);});
    invalidOffer=local;invalidOffer.offers[0].kind=static_cast<Kind>(6);rejected([&]{encode(invalidOffer);});
    invalidOffer=local;invalidOffer.offers[0].available=false;rejected([&]{encode(invalidOffer);});
    invalidOffer.offers[0].unavailable_reason="Permission denied";check(decode_catalog(encode(invalidOffer))==invalidOffer);
    auto oversized=local;oversized.offers.clear();for(unsigned i=0;i<max_resources;++i)oversized.offers.push_back({std::to_string(i),std::string(128,'x'),Kind::camera,false,false,std::string(512,'x')});rejected([&]{validate(oversized);});

    DiscoveryTable discovery;discovery.interfaces({3,5});
    check(!discovery.observe(announce,"192.0.2.1",1,0));check(!discovery.observe(announce,"127.0.0.1",3,0));check(!discovery.observe(announce,"224.0.0.1",3,0));check(!discovery.observe(announce,"192.0.2.999",3,0));
    check(discovery.observe(announce,"192.0.2.1",3,0));check(discovery.snapshot(0).size()==1);
    check(!discovery.observe(announce,"192.0.2.1",3,9000));check(discovery.snapshot(10000).empty()); // Replay cannot extend lifetime.
    check(discovery.observe(announce,"192.0.2.1",3,10000));check(!discovery.withdraw(withdrawal,"192.0.2.2",3));
    check(discovery.withdraw(withdrawal,"192.0.2.1",3));check(discovery.snapshot(10000).empty());
    check(discovery.observe(announce,"192.0.2.1",3,0));check(discovery.observe(announce,"192.0.2.1",5,0));discovery.interfaces({5});check(discovery.snapshot(0).size()==1);
    discovery.clear();check(discovery.snapshot(0).empty());
    for(unsigned i=0;i<max_discovered_routes;++i){auto a=announce;a.device[1]=static_cast<std::uint8_t>(i);check(discovery.observe(a,"192.0.2.1",5,0));}
    auto overflow=announce;overflow.device[2]=1;check(!discovery.observe(overflow,"192.0.2.1",5,0));check(discovery.observe(overflow,"192.0.2.1",5,10001));

    SharingPolicy policy(local);const ConnectionIdentity friendPeer{id(2),true}, stranger{id(3),false};
    check(policy.pairing_required()&&!policy.sharing()&&!policy.gateway_enabled());check(policy.visible(friendPeer,0).empty());
    policy.sharing(true);check(!policy.open(friendPeer,id(1),"camera-1",false,0).grant);policy.remember(friendPeer.device);
    auto camera=policy.open(friendPeer,id(1),"camera-1",false,0);check(camera.grant.has_value());
    check(!policy.close(stranger,camera.grant->id));check(!policy.close({friendPeer.device,false},camera.grant->id));
    check(!policy.open(friendPeer,id(1),"screen-1",true,0).grant);check(policy.open(friendPeer,id(1),"screen-1",false,0).grant.has_value());
    check(policy.open(friendPeer,id(1),"display-1",true,0).grant.has_value());check(!policy.open(friendPeer,id(1),"not-offered",false,0).grant);
    check(policy.open(friendPeer,id(1),"usb-1",false,0).grant.has_value());check(!policy.open(friendPeer,id(1),"usb-1",false,0).grant);
    auto withdrawn=local.offers;withdrawn.erase(withdrawn.begin());auto canceled=policy.offers(withdrawn);check(canceled.size()==1&&canceled[0].id==camera.grant->id);
    check(!policy.open(friendPeer,id(1),"camera-1",false,0).grant);policy.offers(local.offers);
    policy.pairing_required(false);auto open=policy.open(stranger,id(1),"camera-1",false,0);check(open.grant.has_value()&&!open.grant->paired);
    canceled=policy.pairing_required(true);check(canceled.size()==1&&canceled[0].id==open.grant->id);

    const ConnectionIdentity destination{id(4),true};auto remote=catalog(4);policy.remember(destination.device);
    rejected([&]{policy.remote({id(4),false},remote,0,0);});rejected([&]{policy.remote(destination,remote,1,0);});
    rejected([&]{policy.remote(friendPeer,remote,0,0);});policy.remote(destination,remote,0,0);
    auto conflicting=remote;conflicting.name="Changed without revision";rejected([&]{policy.remote(destination,conflicting,0,0);});
    rejected([&]{policy.remote(destination,remote,0,UINT64_MAX);});
    check(policy.visible(friendPeer,0).size()==1);check(!policy.open(friendPeer,id(4),"camera-1",false,0).grant);
    policy.gateway(true);check(policy.visible(friendPeer,0).size()==2);
    const ConnectionIdentity self{id(1),true};auto selfGrant=policy.open(self,id(1),"camera-1",false,0);check(selfGrant.grant.has_value());check(policy.close(self,selfGrant.grant->id));check(policy.visible(self,0).size()==1);check(!policy.open(self,id(4),"camera-1",false,0).grant);
    check(!policy.open({id(1),false},id(1),"camera-1",false,0).grant);
    auto delegated=policy.open(friendPeer,id(4),"camera-1",false,0);check(delegated.grant.has_value()&&delegated.grant->delegated);
    policy.pairing_required(false);check(policy.visible(stranger,0).size()==1);check(!policy.open(stranger,id(4),"camera-1",false,0).grant);
    canceled=policy.gateway(false);check(canceled.size()==1&&canceled[0].id==delegated.grant->id);check(!policy.grants().empty());
    policy.gateway(true);check(policy.open(friendPeer,id(4),"camera-1",false,0).grant.has_value());
    canceled=policy.expire(30000);check(canceled.size()==1);check(policy.visible(friendPeer,30000).size()==1);
    policy.remote(destination,remote,0,30000);check(policy.open(friendPeer,id(4),"camera-1",false,30000).grant.has_value());
    canceled=policy.forget(id(4));check(canceled.size()==1);check(!policy.open(friendPeer,id(4),"camera-1",false,30000).grant);
    auto remaining=policy.grants().size();canceled=policy.forget(id(2));check(canceled.size()==remaining&&policy.grants().empty());
    policy.remember(id(2));check(policy.open(friendPeer,id(1),"camera-1",false,30000).grant.has_value());
    check(policy.sharing(false).size()==1&&policy.grants().empty()&&policy.visible(friendPeer,30000).empty());

    Reconnect retry(Kind::microphone);Milliseconds now=0;for(auto delay:{1000u,2000u,4000u,8000u,8000u}){auto due=retry.failed(Failure::transport_lost,now);check(due&&*due==now+delay&&!retry.ready(now));now=*due;check(retry.ready(now));}
    check(!retry.failed(Failure::transport_lost,now));retry.connected();check(retry.failed(Failure::transport_lost,0).has_value());check(!retry.ready(30000));
    for(auto failure:{Failure::explicit_stop,Failure::permission_revoked,Failure::identity_changed,Failure::resource_removed}){retry.connected();check(!retry.failed(failure,0));check(!retry.failed(Failure::transport_lost,1));}
    Reconnect storage(Kind::usb);check(!storage.failed(Failure::transport_lost,0));
    std::cout<<"Peer wire, discovery expiry, resource grants, gateway revocation and reconnect checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
