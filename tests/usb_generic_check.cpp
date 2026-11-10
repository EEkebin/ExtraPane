#include <dm/usb_device.hpp>
#include <dm/usb_generic_lease.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace dm::usb;using extrapane::Availability;
void need(bool yes){if(!yes)throw std::runtime_error("Generic USB check failed");}
struct State{std::vector<std::string> events;bool running=false,bindFail=false,startFail=false,releaseFail=false;};
struct Runner:HostRunner{
 State& s;Runner(State& state):s(state){}
 Availability ready()override{return {true,{}};}
 std::vector<HostDevice> devices()override{return {};}
 Availability reserve(const std::wstring&)override{s.events.push_back("reserve");return {true,{}};}
 Availability bind(const std::string&)override{throw std::runtime_error("Storage bind used");}
 Availability unbind(const std::wstring&)override{throw std::runtime_error("Unscoped unbind used");}
 Availability bind_generic(const std::string&)override{s.events.push_back("durable-intent");s.events.push_back("exact-guid-bind");return {!s.bindFail,"Bind failed after intent"};}
 Availability release_generic()override{need(!s.running);s.events.push_back("exact-lease-release");return {!s.releaseFail,"Ownership changed"};}
 Availability start(const std::wstring&,const std::string&,std::uint16_t)override{s.events.push_back("start");s.running=!s.startFail;return {!s.startFail,"Start failed"};}
 void stop()noexcept override{s.running=false;s.events.push_back("stop");}
 void release()noexcept override{s.events.push_back("unlock");}
 bool running()const noexcept override{return s.running;}
};
int main(){try{
 need(import_port_absent(1,"error: requested port(s) not found\r\n"));
 need(!import_port_absent(0,"error: requested port(s) not found"));
 need(!import_port_absent(1,"error: access denied"));
 need(!import_port_absent(1,"error: requested port(s) not found\nPort 01: device in use"));
 GenericLease lease{L"{12345678-1234-1234-1234-123456789abc}",L"USB\\VID_TEST","1-2"};
 need(valid_generic_lease(lease));auto corrupt=lease;corrupt.instance.push_back(0);need(!valid_generic_lease(corrupt));corrupt=lease;corrupt.bus="01-2";need(!valid_generic_lease(corrupt));corrupt=lease;corrupt.guid[2]=L'Z';need(!valid_generic_lease(corrupt));
 {auto current=lease;current.instance=L"USB\\VID_3151&PID_3020\\EXACT";current.parent=L"USB\\HUB\\PARENT";current.port=3;
  const std::wstring stub=L"USB\\VID_80EE&PID_CAFE\\EXACT";need(valid_generic_lease(current));
  need(generic_stub_matches(current,stub,current.parent,3,L"VBoxUSB"));
  need(!generic_stub_matches(lease,stub,current.parent,3,L"VBoxUSB"));
  need(!generic_stub_matches(current,stub+L"OTHER",current.parent,3,L"VBoxUSB"));
  need(!generic_stub_matches(current,stub,L"USB\\OTHERHUB",3,L"VBoxUSB"));
  need(!generic_stub_matches(current,stub,current.parent,4,L"VBoxUSB"));
  need(!generic_stub_matches(current,stub,current.parent,3,L"USBCCGP"));
  current.port=0;need(!valid_generic_lease(current));current.port=256;need(!valid_generic_lease(current));
 }
 need(generic_lease_action(lease,L"",false,false,false,false)==GenericLeaseAction::retire_only);
 need(generic_lease_action(lease,L"",true,false,false,false)==GenericLeaseAction::release_binding);
 need(generic_lease_action(lease,lease.instance,true,false,false,false)==GenericLeaseAction::release_binding);
 need(generic_lease_action(lease,L"OTHER",true,false,false,false)==GenericLeaseAction::retire_only);
 need(generic_lease_action(lease,lease.instance,true,true,false,false)==GenericLeaseAction::refuse);
 need(generic_lease_action(lease,lease.instance,true,false,true,false)==GenericLeaseAction::refuse);
 need(generic_lease_action(lease,lease.instance,true,false,false,true)==GenericLeaseAction::refuse);
 GenericFacts facts;facts.complete=true;facts.hid=true;need(generic_eligibility(facts).available);
 facts.audio=true;need(generic_eligibility(facts).available);
 {auto verified=facts;verified.composite=true;verified.descriptors_verified=true;need(generic_eligibility(verified).available);verified.storage=true;need(!generic_eligibility(verified).available);}
 for(auto flag:{&GenericFacts::complete,&GenericFacts::storage,&GenericFacts::hub,&GenericFacts::controller,&GenericFacts::unknown,&GenericFacts::composite,&GenericFacts::owned}){auto invalid=facts;invalid.*flag=flag==&GenericFacts::complete?false:true;need(!generic_eligibility(invalid).available);}
 GenericDevice selected{"usb-generic-test","Keyboard","descriptor","1-2",{},L"USB\\VID_1234&PID_5678\\INSTANCE",0x1234,0x5678,true};
 for(unsigned mode=0;mode<5;++mode){State state;state.bindFail=mode==1;state.startFail=mode==2;state.releaseFail=mode==4;
  {GenericExport session(std::make_unique<Runner>(state),[&]{auto current=selected;if(mode==3)current.fingerprint="replaced";return std::vector{current};});
   auto result=session.start(selected,43210);need(result.available==(mode==0||mode==4));if(result.available){need(session.bus()=="1-2");need(session.stop().available==(mode!=4));}}
  auto find=[&](const char* event){return std::find(state.events.begin(),state.events.end(),event);};
  need(find("reserve")!=state.events.end()&&find("unlock")!=state.events.end());
  if(mode!=3){need(find("durable-intent")<find("exact-guid-bind"));need(find("stop")<find("exact-lease-release"));need(find("exact-lease-release")<find("unlock"));}
  else need(find("exact-guid-bind")==state.events.end());
 }
 need(matches_generic_import_port("Port 01: device in use at high-speed\n -> usbip://127.0.0.1:3456/1-2\n",1,3456,"1-2",true));
 need(!matches_generic_import_port("Port 01: device in use at high-speed\n -> usbip://127.0.0.1:3456/1-3\n",1,3456,"1-2",true));
 need(!matches_generic_import_port("Port 01: device in use at high-speed\n -> usbip://127.0.0.1:3456/1-2\nPort 02: device in use at high-speed\n",1,3456,"1-2",true));
 // Labels: prefer the device's own product string, add functions, VID:PID only when still generic.
 need(usb_function_name("Keyboard","HID Keyboard Device")=="Keyboard");
 need(usb_function_name("Mouse","HID-compliant mouse")=="Mouse");
 need(usb_function_name("HIDClass","HID-compliant game controller")=="Game controller");
 need(usb_function_name("HIDClass","HID-compliant vendor-defined device").empty());
 need(usb_function_name("MEDIA","USB Audio Device")=="Audio");
 need(generic_label("G502 HERO Gaming Mouse","Logitech","USB Composite Device",{"Mouse","Keyboard","Mouse"},0x046D,0xC08B)=="Logitech G502 HERO Gaming Mouse \xC2\xB7 Mouse, Keyboard");
 need(generic_label("Logitech USB Receiver","Logitech","USB Input Device",{},0x046D,0xC52B)=="Logitech USB Receiver");
 need(generic_label("","(Standard USB Host Controller)","USB Composite Device",{"Keyboard"},0x3151,0x3020)=="USB Composite Device \xC2\xB7 Keyboard (3151:3020)");
 need(generic_label("USB Receiver","(Standard system devices)","USB Input Device",{"Mouse"},0x046D,0xC52B)=="USB Receiver \xC2\xB7 Mouse (046D:C52B)");
 need(generic_label("","","",{},0,0)=="USB device");
 need(generic_label("Samson C01U              ","(Standard USB Host Controller)","USB Composite Device",{"Audio"},0x17A0,0x0001)=="Samson C01U \xC2\xB7 Audio");
 {std::vector<GenericDevice> devices(3);devices[0].label=devices[1].label="Watchman Dongle";devices[2].label="Index HMD";devices[0].bus_id="1-2";devices[1].bus_id="1-3";devices[2].bus_id="1-4";
  disambiguate_labels(devices);need(devices[0].label=="Watchman Dongle \xC2\xB7 port 1-2");need(devices[1].label=="Watchman Dongle \xC2\xB7 port 1-3");need(devices[2].label=="Index HMD");}
 std::cout<<"Generic USB policy, scoped lifecycle, import identity and label checks passed; no hardware accessed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
