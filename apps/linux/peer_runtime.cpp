#include "peer_runtime.hpp"
#include "controller.hpp"
#include "host.hpp"
#include "resource_source.hpp"
#include "resource_receiver.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QPointer>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <algorithm>
namespace {
using namespace extrapane::peers;using Service=extrapane::peers::Service;using Clock=std::chrono::steady_clock;
using Host=dm::linux_client::Host;using Source=dm::linux_client::ResourceSource;
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void wipe(std::string& value){volatile char* p=value.data();for(size_t i=0;i<value.size();++i)p[i]=0;value.clear();}
std::string opaque(Kind kind,const QString& value){return std::to_string(unsigned(kind))+"-"+QCryptographicHash::hash(value.toUtf8(),QCryptographicHash::Sha256).toHex().toStdString();}
uint16_t freePort(){int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);need(fd>=0,"Cannot allocate local media port");sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);socklen_t n=sizeof(a);bool ok=bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0&&getsockname(fd,reinterpret_cast<sockaddr*>(&a),&n)==0;close(fd);need(ok,"Cannot allocate local media address");return ntohs(a.sin_port);}
QVariantList inventory(const char* kind,std::stop_token stop){QProcess process;process.setProcessChannelMode(QProcess::SeparateChannels);process.start(QCoreApplication::applicationFilePath(),{"--list-resource",QString::fromLatin1(kind)});need(process.waitForStarted(2000),"Cannot start device inventory");QByteArray bytes;const auto until=Clock::now()+std::chrono::seconds(12);while(process.state()!=QProcess::NotRunning){process.waitForFinished(50);bytes+=process.readAllStandardOutput();process.readAllStandardError();if(stop.stop_requested()||Clock::now()>until||bytes.size()>1024*1024){process.kill();process.waitForFinished(2000);throw std::runtime_error("Device inventory cancelled or exceeded its limit");}}bytes+=process.readAllStandardOutput();need(bytes.size()<=1024*1024&&process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"Device inventory failed");QJsonParseError error;auto json=QJsonDocument::fromJson(bytes,&error);need(error.error==QJsonParseError::NoError&&json.isArray(),"Invalid device inventory");return json.array().toVariantList();}
}
struct LinuxPeerRuntime::State {
 using Key=std::pair<uint64_t,uint64_t>;
 struct Local {Offer offer;QString device,monitor;};
 struct Use {Service::Context context;rpc::Open request;Local local;std::shared_ptr<dm::access::Gate> gate;uint64_t control=0;uint16_t port=0;std::unique_ptr<Host> host;std::unique_ptr<Source> source;std::atomic_bool cancelled=false,done=false,ready=false;~Use(){wipe(request.native_credential);}};
 mutable std::mutex mutex;std::vector<Local> sources;std::map<Key,std::shared_ptr<Use>> uses;std::atomic_bool closed=false,inventoryDone=false;QString availability;
 explicit State(QString value):availability(std::move(value)){}
 static Key key(const Service::Context& c){return {c.connection,c.request};}
 Local source(const std::string& id){std::lock_guard lock(mutex);for(const auto& s:sources)if(s.offer.id==id&&s.offer.available)return s;throw std::runtime_error("Resource is no longer available");}
 std::shared_ptr<Use> find(const Service::Context& c){auto f=uses.find(key(c));need(f!=uses.end(),"Media request was not prepared");return f->second;}
 void host(const std::shared_ptr<Use>& use,bool existing){
  const auto kind=existing?0:static_cast<int>(use->context.kind);need(kind>=0&&kind<=2,"This resource has no Linux receiving integration");
  const auto reason=kind==0?QString{}:dm::linux_client::resourceReceiverUnavailable(static_cast<extrapane::ResourceKind>(kind));if(!reason.isEmpty())throw std::runtime_error(reason.toStdString());
  use->gate=std::make_shared<dm::access::Gate>();auto code=use->gate->generate();wipe(code);auto initial=use->gate->begin();use->gate->consume(initial.generation);wipe(initial.code);use->control=use->gate->open_control(initial.generation);
  auto ticket=use->gate->control_channels(1u<<kind,use->control,kind==0?std::optional<bool>(use->request.input):std::optional<bool>{});use->request.native_credential=dm::access::formatCredential(ticket[0]);wipe(ticket[0].secret);
  use->port=freePort();use->host=std::make_unique<Host>("127.0.0.1",nullptr,use->port,use->gate,dm::linux_client::ScreenChooserOffer{existing?1u:0u,use->request.input},dm::linux_client::HostScope{kind,existing,use->request.input});
  QObject::connect(use->host.get(),&Host::state,use->host.get(),[weak=std::weak_ptr<Use>(use)](const QString& text){if(auto u=weak.lock()){if(text.startsWith("Listening on"))u->ready=true;if(text.startsWith("Server error:")||text.startsWith("Resource ended:"))u->done=true;}},Qt::DirectConnection);
  QObject::connect(use->host.get(),&QThread::finished,use->host.get(),[weak=std::weak_ptr<Use>(use)]{if(auto u=weak.lock())u->done=true;},Qt::DirectConnection);use->host->prepareScopedListener();use->host->start();use->ready=true;
 }
 void stop(const Service::Context& context){auto f=uses.find(key(context));if(f==uses.end())return;auto u=f->second;u->cancelled=true;if(u->source){u->source->requestInterruption();u->source->wait();u->source.reset();}if(u->host){u->host->requestInterruption();u->host->wait();u->host.reset();}if(u->gate){u->gate->close_control(u->control);u->gate->revoke();}uses.erase(f);}
};
struct LinuxPeerRuntime::Ui {
 struct Display {std::shared_ptr<State::Use> use;std::unique_ptr<Controller> controller;std::unique_ptr<QQmlContext> context;QPointer<QQuickWindow> window;~Display(){if(controller)controller->shutdown();if(window)delete window;}};
 QQmlEngine* engine;std::map<State::Key,std::unique_ptr<Display>> displays;QTimer cleanup;
 explicit Ui(QQmlEngine* value):engine(value){}
 void reap(bool all=false){for(auto it=displays.begin();it!=displays.end();){auto& display=*it->second;if(!all&&!display.use->cancelled&&!display.use->done){++it;continue;}display.controller->shutdown();if(display.window)delete display.window;it=displays.erase(it);}}
};
LinuxPeerRuntime::LinuxPeerRuntime(QQmlEngine* engine,QString availability,QObject* parent):QObject(parent),state_(std::make_shared<State>(std::move(availability))),ui_(std::make_unique<Ui>(engine)){connect(&ui_->cleanup,&QTimer::timeout,this,[this]{ui_->reap();});ui_->cleanup.start(50);}
LinuxPeerRuntime::~LinuxPeerRuntime(){shutdown();}
std::vector<Offer> LinuxPeerRuntime::offers()const{std::lock_guard lock(state_->mutex);std::vector<Offer> result;for(const auto& s:state_->sources)result.push_back(s.offer);return result;}
void LinuxPeerRuntime::refreshInventory(){
 if(inventory_.joinable()){if(!state_->inventoryDone)return;inventory_.join();}state_->inventoryDone=false;std::vector<State::Local> initial;const bool displayAvailable=state_->availability.isEmpty();initial.push_back({{opaque(Kind::display_destination,"custom"),"Virtual display (custom mode)",Kind::display_destination,displayAvailable,true,state_->availability.toStdString(),1920,1080,60},{},{}});
 for(auto* screen:QGuiApplication::screens()){const auto size=screen->geometry().size()*screen->devicePixelRatio();initial.push_back({{opaque(Kind::display_destination,screen->name()),("Display destination "+screen->name()).left(32).toStdString(),Kind::display_destination,displayAvailable,true,state_->availability.toStdString(),unsigned(size.width()),unsigned(size.height()),unsigned(std::clamp(std::lround(screen->refreshRate()),1L,360L))},{},screen->name()});}
 auto state=state_;inventory_=std::jthread([this,state,initial=std::move(initial)](std::stop_token stop)mutable{struct Finished{std::atomic_bool& done;~Finished(){done=true;}}finished{state->inventoryDone};try{
  const auto screenReason=Host::unavailable(true,false);initial.push_back({{"screen-chooser","Choose a screen on this device",Kind::existing_screen,screenReason.isEmpty(),false,screenReason.toStdString()},{},{}});
  for(auto [kind,name]:{std::pair{Kind::camera,"camera"},std::pair{Kind::microphone,"microphone"}}){try{for(const auto& entry:inventory(name,stop)){auto device=entry.toMap();const auto id=device.value("id").toString();if(id.isEmpty())continue;initial.push_back({{opaque(kind,id),device.value("label",name).toString().simplified().left(32).toStdString(),kind,device.value("available",true).toBool(),false,device.value("available",true).toBool()?std::string{}:device.value("reason","Device unavailable").toString().simplified().left(120).toStdString()},id,{}});}}catch(const std::exception& e){initial.push_back({{std::string(name)+"-unavailable",name,kind,false,false,QString::fromUtf8(e.what()).left(120).toStdString()},{},{}});}if(stop.stop_requested())return;}
  initial.push_back({{"speakers-unavailable","Speakers",Kind::speaker,false,false,"Linux speaker forwarding is not implemented"},{},{}});initial.push_back({{"usb-unavailable","USB storage",Kind::usb,false,false,"Linux USB forwarding is not implemented"},{},{}});need(initial.size()<=max_resources,"Device inventory exceeds its limit");{std::lock_guard lock(state->mutex);if(state->closed)return;state->sources=std::move(initial);}QMetaObject::invokeMethod(this,[this]{emit inventoryChanged();},Qt::QueuedConnection);
 }catch(const std::exception& e){const auto reason=QString::fromUtf8(e.what());QMetaObject::invokeMethod(this,[this,reason]{emit inventoryFailed(reason);},Qt::QueuedConnection);}});
}
Service::Hooks LinuxPeerRuntime::hooks(){auto state=state_;Service::Hooks hooks;
 hooks.prepare_request=[state](const Service::Context& c,rpc::Open& request){need(!state->closed&&!state->uses.contains(State::key(c)),"Duplicate or closed Linux media request");auto u=std::make_shared<State::Use>();u->context=c;u->request=request;state->uses.emplace(State::key(c),u);try{if(c.kind!=Kind::existing_screen){state->host(u,false);request.native_credential=u->request.native_credential;}}catch(...){state->stop(c);throw;}};
 hooks.prepare=[state](const Service::Context& c,rpc::Open& request){need(!state->closed,"Linux media runtime stopped");if(c.incoming){need(!state->uses.contains(State::key(c)),"Duplicate Linux media grant");auto u=std::make_shared<State::Use>();u->context=c;u->request=request;u->local=state->source(request.resource);need(u->local.offer.kind==c.kind&&(!request.input||u->local.offer.input_allowed),"Resource permission changed");state->uses.emplace(State::key(c),u);try{if(c.kind==Kind::existing_screen){need(!request.width&&!request.height&&!request.refresh,"Existing screens cannot request a virtual display mode");state->host(u,true);request.native_credential=u->request.native_credential;return Service::Endpoint{Service::Role::connect_local,u->port};}need(c.kind==Kind::display_destination||c.kind==Kind::camera||c.kind==Kind::microphone,"Unsupported Linux resource");return Service::Endpoint{Service::Role::listen_local,0};}catch(...){state->stop(c);throw;}}
 auto u=state->find(c);u->context=c;u->request=request;if(c.kind==Kind::existing_screen)return Service::Endpoint{Service::Role::listen_local,0};need(u->host&&u->host->isRunning(),"Local media endpoint stopped");return Service::Endpoint{Service::Role::connect_local,u->port};};
 hooks.ready=[this,state](const Service::Context& c,const rpc::Open& request,uint16_t port){auto u=state->find(c);u->request=request;if((c.incoming&&c.kind==Kind::display_destination)||(!c.incoming&&c.kind==Kind::existing_screen)){QMetaObject::invokeMethod(this,[this,u,port]{if(!state_->closed&&!u->cancelled)openDisplay(u,port);},Qt::QueuedConnection);}
 else if(c.incoming&&(c.kind==Kind::camera||c.kind==Kind::microphone)){Source::Options options;options.kind=c.kind==Kind::camera?Source::Kind::Camera:Source::Kind::Microphone;options.device=u->local.device;options.server="127.0.0.1";options.local="0.0.0.0";options.port=port;options.code=QString::fromStdString(request.native_credential);u->source=std::make_unique<Source>(options);QObject::connect(u->source.get(),&QThread::finished,u->source.get(),[weak=std::weak_ptr<State::Use>(u)]{if(auto use=weak.lock())use->done=true;},Qt::DirectConnection);u->source->start();}};
 hooks.stopped=[state](const Service::Context& c){state->stop(c);};hooks.cancel_request=hooks.stopped;hooks.poll=[state]{std::vector<Service::Context> ended;for(const auto& [key,use]:state->uses)if(use->done)ended.push_back(use->context);return ended;};return hooks;
}
void LinuxPeerRuntime::openDisplay(std::shared_ptr<void> opaqueUse,uint16_t port){auto use=std::static_pointer_cast<State::Use>(opaqueUse);try{need(ui_->engine&&!use->cancelled,"Display request was cancelled");auto display=std::make_unique<Ui::Display>();display->use=use;display->controller=std::make_unique<Controller>(state_->availability,nullptr,false);display->context=std::make_unique<QQmlContext>(ui_->engine->rootContext());display->context->setContextProperty("receiver",display->controller.get());QQmlComponent component(ui_->engine,QUrl("qrc:/ReceiverWindow.qml"));auto* object=component.create(display->context.get());display->window=qobject_cast<QQuickWindow*>(object);if(!display->window){delete object;throw std::runtime_error(component.errorString().toStdString());}display->controller->setWindow(display->window);
 if(use->context.kind==Kind::existing_screen)display->controller->setScreenRequest({dm::video::wire::DisplayMode::existing_screen,1},use->request.input);
 else{if(!use->local.monitor.isEmpty()){QScreen* target=nullptr;for(auto* screen:QGuiApplication::screens())if(screen->name()==use->local.monitor){target=screen;break;}need(target,"Selected destination monitor is disconnected");display->window->setScreen(target);display->window->setPosition(target->geometry().topLeft()+QPoint(40,40));}if(use->request.width)display->controller->setDisplayMode({{"mode","custom"},{"width",use->request.width},{"height",use->request.height},{"fps",use->request.refresh}});else if(use->local.offer.width)display->controller->setDisplayMode({{"mode","preset"},{"width",use->local.offer.width},{"height",use->local.offer.height},{"fps",use->local.offer.refresh}});}
 auto* controller=display->controller.get();connect(controller,&Controller::changed,this,[weak=std::weak_ptr<State::Use>(use),controller]{if(auto u=weak.lock())if(!controller->active())u->done=true;});ui_->displays.emplace(State::key(use->context),std::move(display));auto credential=QString::fromStdString(use->request.native_credential);controller->connectTo("127.0.0.1","0.0.0.0",use->request.input,0,0,port,credential);credential.fill(QChar(0));if(!controller->active())use->done=true;
 }catch(...){use->done=true;}}
void LinuxPeerRuntime::shutdown(){if(state_->closed.exchange(true))return;inventory_.request_stop();if(inventory_.joinable())inventory_.join();ui_->cleanup.stop();while(!state_->uses.empty())state_->stop(state_->uses.begin()->second->context);ui_->reap(true);}
