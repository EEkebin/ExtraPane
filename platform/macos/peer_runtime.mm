#include "peer_runtime.hpp"
#include "peer_display.hpp"
#include "host.hpp"
#include "resource_session.hpp"
#include "resource_capture.hpp"
#import <ApplicationServices/ApplicationServices.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>
#include <stdexcept>
namespace dm::mac::peer {
namespace {
using namespace extrapane::peers;using Service=extrapane::peers::Service;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void wipe(std::string& s){volatile char* p=s.data();for(size_t i=0;i<s.size();++i)p[i]=0;s.clear();}
std::string opaque(Kind kind,const std::string& value){unsigned char digest[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),digest);std::string result=std::to_string(unsigned(kind))+"-";for(auto b:digest){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;}
void shorten(std::string& value,size_t maximum){if(value.size()<=maximum)return;size_t end=maximum;while(end&&((static_cast<unsigned char>(value[end])&0xc0)==0x80))--end;value.resize(end);}
uint16_t freePort(){int fd=socket(AF_INET,SOCK_STREAM,0);need(fd>=0,"Cannot allocate local media port");sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);socklen_t size=sizeof(address);bool ok=bind(fd,reinterpret_cast<sockaddr*>(&address),size)==0&&getsockname(fd,reinterpret_cast<sockaddr*>(&address),&size)==0;close(fd);need(ok,"Cannot allocate local media address");return ntohs(address.sin_port);}
}
struct MacPeerRuntime::State {
 using Key=std::pair<uint64_t,uint64_t>;
 struct Local {Offer offer;std::string device;unsigned monitor=0;SharedScreen screen;};
 struct Use {Service::Context context;rpc::Open request;Local local;std::shared_ptr<access::Gate> gate;uint64_t control=0;uint16_t port=0;std::unique_ptr<Host> host;std::unique_ptr<ResourceSession> source;std::unique_ptr<PeerDisplayWindow> display;std::atomic_bool done=false,ready=false;~Use(){wipe(request.native_credential);}};
 std::mutex mutex;std::vector<Local> sources;std::map<Key,std::shared_ptr<Use>> uses;std::atomic_bool closed=false;
 static Key key(const Service::Context& c){return {c.connection,c.request};}
 Local source(const std::string& id){std::lock_guard lock(mutex);for(auto& s:sources)if(s.offer.id==id&&s.offer.available)return s;throw std::runtime_error("Resource is no longer available");}
 std::shared_ptr<Use> find(const Service::Context& c){auto it=uses.find(key(c));need(it!=uses.end(),"Media request was not prepared");return it->second;}
 void host(const std::shared_ptr<Use>& u,bool existing){
  need(existing||u->context.kind==Kind::display_destination,"macOS does not provide this virtual receiving device");
  u->gate=std::make_shared<access::Gate>();auto code=u->gate->generate();wipe(code);auto initial=u->gate->begin();u->gate->consume(initial.generation);wipe(initial.code);u->control=u->gate->open_control(initial.generation);auto ticket=u->gate->control_channels(1,u->control,u->request.input);u->request.native_credential=access::formatCredential(ticket[0]);wipe(ticket[0].secret);
  std::vector<SharedScreen> screens;if(existing){auto screen=u->local.screen;screen.allowInput=u->request.input;screens.push_back(screen);}
  u->port=freePort();u->host=std::make_unique<Host>("127.0.0.1",[weak=std::weak_ptr<Use>(u)](std::string text){if(auto use=weak.lock()){if(text.starts_with("Listening on"))use->ready=true;if(text.starts_with("Server error:")||text.starts_with("Connection ended:"))use->done=true;}},u->port,u->gate,std::move(screens),HostScope{true,existing,u->request.input});
  auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(!u->ready&&!u->done&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(10));need(u->ready&&!u->done,"Local display host could not start");
 }
 void stop(const Service::Context& c){auto it=uses.find(key(c));if(it==uses.end())return;auto u=it->second;if(u->display)u->display->stop();if(u->source){u->source->cancel();u->source.reset();}if(u->host){u->host->stop();u->host.reset();}if(u->gate){u->gate->close_control(u->control);u->gate->revoke();}uses.erase(it);}
};
MacPeerRuntime::MacPeerRuntime():state_(std::make_shared<State>()){}
MacPeerRuntime::~MacPeerRuntime(){shutdown();}
std::string MacPeerRuntime::receiveUnavailable(Kind kind){if(kind==Kind::display_destination||kind==Kind::existing_screen)return {};return "macOS virtual camera, microphone, speaker and USB receiving integrations are not implemented";}
std::vector<Offer> MacPeerRuntime::refresh(){
 const bool canCapture=CGPreflightScreenCaptureAccess();std::vector<State::Local> sources;sources.push_back({{"display-custom","Virtual display (custom mode)",Kind::display_destination,true,true,{},1920,1080,60},{},0,{}});
 CGDirectDisplayID displays[64];uint32_t count=0;need(CGGetActiveDisplayList(64,displays,&count)==kCGErrorSuccess,"Cannot enumerate monitors");
 for(uint32_t n=0;n<count;++n){auto id=displays[n];auto screen=shareScreen(1,id,false);auto mode=CGDisplayCopyDisplayMode(id);auto hz=mode?CGDisplayModeGetRefreshRate(mode):60;unsigned width=unsigned(CGDisplayPixelsWide(id)),height=unsigned(CGDisplayPixelsHigh(id));if(mode)CGDisplayModeRelease(mode);
 sources.push_back({{opaque(Kind::display_destination,screen.displayUuid),"Display destination "+std::to_string(n+1),Kind::display_destination,true,true,{},width,height,unsigned(std::clamp(hz>0?hz:60,1.0,360.0))},{},id,{}});
 sources.push_back({{opaque(Kind::existing_screen,screen.displayUuid),"Existing screen "+std::to_string(n+1),Kind::existing_screen,canCapture,false,canCapture?std::string{}:"Screen Recording permission is required"},{},id,screen});
 }
 for(auto [kind,native]:{std::pair{Kind::camera,resource::Kind::Camera},std::pair{Kind::microphone,resource::Kind::Microphone}}){try{for(auto choice:resource::enumerate(native)){if(choice.id.empty())continue;Offer offer{opaque(kind,choice.id),choice.label,kind,choice.available,false,choice.available?std::string{}:choice.reason};if(offer.label.empty())offer.label=kind==Kind::camera?"Camera":"Microphone";shorten(offer.label,120);if(!offer.available&&offer.unavailable_reason.empty())offer.unavailable_reason="Device permission is required";shorten(offer.unavailable_reason,400);sources.push_back({std::move(offer),choice.id,0,{}});}}catch(...){sources.push_back({{kind==Kind::camera?"camera-unavailable":"microphone-unavailable",kind==Kind::camera?"Camera":"Microphone",kind,false,false,"Device inventory could not be read"},{},0,{}});}}
 sources.push_back({{"speakers-unavailable","Speakers",Kind::speaker,false,false,"macOS speaker forwarding is not implemented"},{},0,{}});sources.push_back({{"usb-unavailable","USB storage",Kind::usb,false,false,"macOS USB forwarding is not implemented"},{},0,{}});need(sources.size()<=max_resources,"Device inventory exceeds its limit");std::vector<Offer> offers;for(auto& source:sources)offers.push_back(source.offer);{std::lock_guard lock(state_->mutex);if(!state_->closed)state_->sources=std::move(sources);}return offers;
}
Service::Hooks MacPeerRuntime::hooks(){auto state=state_;Service::Hooks hooks;
 hooks.prepare_request=[state](const Service::Context& c,rpc::Open& request){need(!state->closed&&!state->uses.contains(State::key(c)),"Duplicate or closed media request");need(receiveUnavailable(c.kind).empty(),"This macOS receiving integration is unavailable");auto u=std::make_shared<State::Use>();u->context=c;u->request=request;state->uses.emplace(State::key(c),u);try{if(c.kind==Kind::display_destination){state->host(u,false);request.native_credential=u->request.native_credential;}}catch(...){state->stop(c);throw;}};
 hooks.prepare=[state](const Service::Context& c,rpc::Open& request){need(!state->closed,"macOS media runtime stopped");if(c.incoming){need(!state->uses.contains(State::key(c)),"Duplicate media grant");auto u=std::make_shared<State::Use>();u->context=c;u->request=request;u->local=state->source(request.resource);need(u->local.offer.kind==c.kind&&(!request.input||u->local.offer.input_allowed),"Resource permission changed");state->uses.emplace(State::key(c),u);try{if(c.kind==Kind::existing_screen){need(!request.width&&!request.height&&!request.refresh,"Existing screens cannot request a virtual mode");state->host(u,true);request.native_credential=u->request.native_credential;return Service::Endpoint{Service::Role::connect_local,u->port};}need(c.kind==Kind::display_destination||c.kind==Kind::camera||c.kind==Kind::microphone,"Unsupported macOS source");return Service::Endpoint{Service::Role::listen_local,0};}catch(...){state->stop(c);throw;}}
 auto u=state->find(c);u->context=c;u->request=request;if(c.kind==Kind::existing_screen)return Service::Endpoint{Service::Role::listen_local,0};need(u->host&&u->host->running(),"Local display host stopped");return Service::Endpoint{Service::Role::connect_local,u->port};};
 hooks.ready=[state](const Service::Context& c,const rpc::Open& request,uint16_t port){auto u=state->find(c);u->request=request;if((c.incoming&&c.kind==Kind::display_destination)||(!c.incoming&&c.kind==Kind::existing_screen)){DisplayOptions options;options.port=port;options.existing=c.kind==Kind::existing_screen;options.input=request.input;options.credential=request.native_credential;options.monitor=u->local.monitor;options.exact=request.width!=0;if(!options.existing){options.width=request.width?request.width:u->local.offer.width;options.height=request.height?request.height:u->local.offer.height;options.refresh=request.refresh?request.refresh:u->local.offer.refresh;}u->display=std::make_unique<PeerDisplayWindow>(std::move(options));}
 else if(c.incoming&&(c.kind==Kind::camera||c.kind==Kind::microphone)){ResourceOptions options;options.kind=c.kind==Kind::camera?extrapane::ResourceKind::camera:extrapane::ResourceKind::microphone;options.sourceId=u->local.device;options.target={"127.0.0.1","0.0.0.0",port};options.code=request.native_credential;options.nativeTicket=true;ResourceCallbacks callbacks;callbacks.ended=[weak=std::weak_ptr<State::Use>(u)](std::string){if(auto use=weak.lock())use->done=true;};u->source=std::make_unique<ResourceSession>(std::move(options),std::move(callbacks));u->source->start();}};
 hooks.stopped=[state](const Service::Context& c){state->stop(c);};hooks.cancel_request=hooks.stopped;hooks.poll=[state]{std::vector<Service::Context> ended;for(auto& [key,u]:state->uses)if(u->done||(u->display&&u->display->done())||(u->host&&!u->host->running()))ended.push_back(u->context);return ended;};return hooks;
}
void MacPeerRuntime::shutdown(){if(state_->closed.exchange(true))return;while(!state_->uses.empty())state_->stop(state_->uses.begin()->second->context);}
}
