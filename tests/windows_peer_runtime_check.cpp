#include "windows_peer_runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace extrapane;using namespace windows_peer;
static void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
struct Child {Command command;std::string output;std::vector<std::string> input;bool live=false;};
static std::string ticket(unsigned kind){return "EPCT1:"+std::to_string(kind)+":"+std::string(32,'a')+":"+std::string(64,'b');}
struct Fake:Process {
 std::shared_ptr<Child> child;explicit Fake(std::shared_ptr<Child> c):child(std::move(c)){}
 void start(const Command& command)override{child->command=command;child->live=true;if(command.executable=="ExtraPane.Service.exe"||(command.executable=="dm-view.exe"&&command.arguments[0]=="host"))child->output="ACCESS_CODE 123456\nLISTENING 127.0.0.1\n";}
 void write(const std::string& value)override{child->input.push_back(value);if(value=="G")child->output+="ACCESS_CODE 654321\n";if(child->command.executable=="ExtraPane.Session.exe"){if(value.starts_with("ADD ")){const auto kind=value[4]-'0';child->output+="SESSION_TICKET_V1\t"+std::string(std::array{"display","camera","microphone","usb","speaker"}[kind])+"\t"+ticket(kind)+"\n";}else child->output+="SESSION_READY\n";}}
 std::string drain()override{auto out=std::move(child->output);child->output.clear();return out;}bool running()const override{return child->live;}void stop()noexcept override{child->live=false;}
};
int main(){try{
 for(const auto& operation:{"list-cameras","list-microphones","list-speakers","list-storage","list-usb"}){Command inventoryCommand{"inventory.exe",{operation},false,false,false};require(native_arguments(inventoryCommand,123)==std::vector<std::string>{operation},"inventory must not get unsupported parent argument");}
 require(native_arguments({"source.exe",{"send"}},123)==std::vector<std::string>({"send","--parent-pid","123"}),"source lost parent lifetime protection");
 std::vector<std::shared_ptr<Child>> children;Factory factory=[&]{auto c=std::make_shared<Child>();children.push_back(c);return std::make_unique<Fake>(c);};
 std::vector<Source> inventory{{{"camera","Camera",peers::Kind::camera,true,false,""},"private-camera"},{{"screen","Screen",peers::Kind::existing_screen,true,false,""},"\\\\?\\DISPLAY#private-screen"}};
 WindowsPeerRuntime runtime(factory,[&]{return inventory;},[]{return std::uint16_t(48000);});auto offers=runtime.refresh();require(offers.size()==2&&children.empty(),"inventory launched capture");auto hooks=runtime.hooks();peers::DeviceId owner{};owner[0]=7;
 peers::Service::Context outgoing{1,1,0,owner,false,peers::Kind::camera};peers::rpc::Open request{owner,"remote-camera",false};hooks.prepare_request(outgoing,request);require(request.native_credential==ticket(1),"consumer did not issue kind-bound ticket");outgoing.grant=10;auto endpoint=hooks.prepare(outgoing,request);require(endpoint.role==peers::Service::Role::connect_local&&endpoint.port==48000,"consumer bridge direction incorrect");hooks.ready(outgoing,request,endpoint.port);
 peers::Service::Context incoming{2,3,20,owner,true,peers::Kind::camera};peers::rpc::Open offered{owner,"camera",false};offered.native_credential=ticket(1);endpoint=hooks.prepare(incoming,offered);require(endpoint.role==peers::Service::Role::listen_local,"provider bridge direction incorrect");hooks.ready(incoming,offered,48001);auto capture=children.back();require(capture->input[0]=="private-camera\n","provider source selection failed");
 auto duplicate=incoming;duplicate.grant=21;bool rejected=false;try{hooks.prepare(duplicate,offered);}catch(...){rejected=true;}require(rejected,"duplicate request accepted");hooks.stopped(duplicate);require(capture->live,"failed duplicate request stopped original grant");
 capture->live=false;auto ended=hooks.poll();require(ended.size()==1&&ended[0].grant==20,"native termination not reported");hooks.stopped(incoming);
 peers::Service::Context screen{3,4,30,owner,true,peers::Kind::existing_screen};peers::rpc::Open existing{owner,"screen",false};endpoint=hooks.prepare(screen,existing);require(endpoint.role==peers::Service::Role::connect_local&&existing.native_credential==ticket(0),"existing-screen provider did not reverse connection");hooks.ready(screen,existing,endpoint.port);
 peers::Service::Context viewer{4,5,0,owner,false,peers::Kind::existing_screen};peers::rpc::Open watch{owner,"remote-screen",false};hooks.prepare_request(viewer,watch);require(watch.native_credential.empty(),"screen viewer created a virtual backend");viewer.grant=40;watch.native_credential=ticket(0);endpoint=hooks.prepare(viewer,watch);require(endpoint.role==peers::Service::Role::listen_local,"screen viewer bridge direction incorrect");hooks.ready(viewer,watch,48002);require(std::find(children.back()->command.arguments.begin(),children.back()->command.arguments.end(),"--existing-screen")!=children.back()->command.arguments.end(),"viewer did not select existing screen");
 hooks.stopped(viewer);hooks.stopped(screen);hooks.stopped(outgoing);for(const auto& child:children)require(!child->live,"native child leaked after all grants stopped");
 std::cout<<"Windows peer runtime: fake-process inventory, authenticated hook routing, native admission, reversed screen, revocation and cleanup passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
