#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <ApplicationServices/ApplicationServices.h>
#include "peer_workspace.hpp"
#include "peer_runtime.hpp"
#include "peer_discovery.hpp"
#include "resource_capture.hpp"
#include "network.hpp"
#include <extrapane/service.hpp>
#include <set>
#include <map>
#include <filesystem>
#include <algorithm>
#include <cctype>
using namespace extrapane::peers;
namespace {
NSString* text(const std::string& s){return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding]?:@"";}
std::string str(NSString* s){return s.UTF8String?:"";}
std::string hex(const DeviceId& id){constexpr char digits[]="0123456789abcdef";std::string s;for(auto b:id){s+=digits[b>>4];s+=digits[b&15];}return s;}
NSString* kind(Kind k){switch(k){case Kind::display_destination:return @"Extend desktop";case Kind::existing_screen:return @"View existing screen";case Kind::camera:return @"Camera";case Kind::microphone:return @"Microphone";case Kind::speaker:return @"Speakers";case Kind::usb:return @"USB storage";}return @"Resource";}
NSStackView* stack(BOOL horizontal=NO){auto* s=[NSStackView new];s.orientation=horizontal?NSUserInterfaceLayoutOrientationHorizontal:NSUserInterfaceLayoutOrientationVertical;s.alignment=horizontal?NSLayoutAttributeCenterY:NSLayoutAttributeLeading;s.spacing=12;s.translatesAutoresizingMaskIntoConstraints=NO;return s;}
NSTextField* label(NSString* s,CGFloat size=14){auto* v=[NSTextField wrappingLabelWithString:s];v.font=[NSFont systemFontOfSize:size];return v;}
unsigned portValue(NSString* value){const auto digits=str(value);if(digits.empty()||digits.size()>5||!std::all_of(digits.begin(),digits.end(),[](char c){return c>='0'&&c<='9';}))return 0;unsigned result=0;for(char c:digits)result=result*10+unsigned(c-'0');return result<=65535?result:0;}
void invalidField(NSView* field){field.wantsLayer=YES;field.layer.borderColor=NSColor.systemRedColor.CGColor;field.layer.borderWidth=2;field.layer.cornerRadius=5;__weak NSView* weak=field;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2500*NSEC_PER_MSEC),dispatch_get_main_queue(),^{weak.layer.borderWidth=0;});}
void clear(NSStackView* s){for(NSView* v in [s.arrangedSubviews copy]){[s removeArrangedSubview:v];[v removeFromSuperview];}}
}
@interface ExtraPanePeerAction:NSButton
@property(nonatomic,copy) void (^actionBlock)(void);
-(void)execute:(id)sender;
@end
@implementation ExtraPanePeerAction
-(void)execute:(id)sender{(void)sender;if(self.actionBlock)self.actionBlock();}
@end
static ExtraPanePeerAction* button(NSString* title,void (^action)(void),BOOL destructive=NO){auto* b=[ExtraPanePeerAction new];b.title=title;b.bezelStyle=NSBezelStyleRounded;b.target=b;b.action=@selector(execute:);b.actionBlock=action;if(destructive)b.contentTintColor=NSColor.systemRedColor;return b;}
@interface ExtraPanePeerApp:NSObject<NSApplicationDelegate,NSWindowDelegate,NSTextFieldDelegate,NSMenuDelegate>{
    std::shared_ptr<dm::mac::peer::MacPeerRuntime> runtime;
    std::unique_ptr<Service> service;
    dm::mac::PeerDiscovery discovery;
    std::vector<Offer> inventory;
    std::set<std::string> enabled;
    std::map<Service::Token,Service::Event> links;
    std::map<std::pair<Service::Token,Service::Token>,Service::Event> resources;
    std::map<Service::Token,std::vector<Catalog>> catalogs;
    std::set<std::pair<Service::Token,Service::Token>> retrying;
    std::map<std::pair<Service::Token,Service::Token>,Kind> pending;
    std::uint64_t instance,sequence;
    DeviceId expected;
    bool sharing,scanning,refreshing,terminating;
    unsigned listeningPort;
}
@property(nonatomic,strong) NSWindow* window;
@property(nonatomic,strong) NSTextField* address;
@property(nonatomic,strong) NSTextField* port;
@property(nonatomic,strong) NSSecureTextField* code;
@property(nonatomic,strong) NSPopUpButton* outboundRoute;
@property(nonatomic,strong) NSTextField* bindAddress;
@property(nonatomic,strong) NSTextField* listenPort;
@property(nonatomic,strong) NSTextField* status;
@property(nonatomic,strong) NSTextField* generatedCode;
@property(nonatomic,strong) NSButton* requirePairing;
@property(nonatomic,strong) NSButton* openConnection;
@property(nonatomic,strong) NSButton* gateway;
@property(nonatomic,strong) NSButton* startButton;
@property(nonatomic,strong) NSButton* disconnectButton;
@property(nonatomic,strong) NSStackView* nearby;
@property(nonatomic,strong) NSStackView* saved;
@property(nonatomic,strong) NSStackView* offered;
@property(nonatomic,strong) NSStackView* remoteResources;
@property(nonatomic,strong) NSStackView* connectedResources;
@property(nonatomic,strong) NSTabView* pages;
@property(nonatomic,strong) NSTimer* timer;
@property(nonatomic,strong) NSStatusItem* tray;
-(void)poll;
-(void)refreshInventory;
-(void)refreshRoutes;
-(void)publish;
-(void)renderOffers;
-(void)renderConnections;
-(void)renderDevices;
-(void)connectAddress:(std::string)address port:(unsigned)port expected:(DeviceId)identity code:(std::string)code open:(BOOL)open;
-(void)error:(NSString*)message;
-(void)showWindow:(id)sender;
-(void)quit:(id)sender;
@end
@implementation ExtraPanePeerApp
-(NSStackView*)page:(NSString*)name{
    auto* content=stack();content.edgeInsets=NSEdgeInsetsMake(24,24,24,24);[content addArrangedSubview:label(name,28)];
    auto* scroll=[NSScrollView new];scroll.hasVerticalScroller=YES;scroll.drawsBackground=NO;scroll.documentView=content;
    [content.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor constant:-16].active=YES;
    auto* tab=[[NSTabViewItem alloc] initWithIdentifier:name];tab.label=name;tab.view=scroll;[self.pages addTabViewItem:tab];return content;
}
-(void)applicationDidFinishLaunching:(NSNotification*)notification{
    (void)notification;
    try{
        runtime=std::make_shared<dm::mac::peer::MacPeerRuntime>();
        NSString* support=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES) firstObject];NSString* parent=[support stringByAppendingPathComponent:@"ExtraPane"];
        [[NSFileManager defaultManager] createDirectoryAtPath:parent withIntermediateDirectories:YES attributes:nil error:nil];
        for(NSString* value in [[NSUserDefaults standardUserDefaults] stringArrayForKey:@"peerOffers"])enabled.insert(str(value));
        auto* defaults=[NSUserDefaults standardUserDefaults];const bool required=[defaults objectForKey:@"peerPairingRequired"]?[defaults boolForKey:@"peerPairingRequired"]:true;
        service=std::make_unique<Service>(Service::Options{std::filesystem::path([[parent stringByAppendingPathComponent:@"peers"] fileSystemRepresentation]),str(NSProcessInfo.processInfo.hostName),{},required},runtime->hooks());
        instance=std::chrono::steady_clock::now().time_since_epoch().count()|1;
        self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1080,760) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];self.window.title=@"ExtraPane";self.window.minSize=NSMakeSize(620,500);self.window.delegate=self;
        auto* shell=stack(YES);shell.edgeInsets=NSEdgeInsetsMake(12,12,12,12);self.window.contentView=shell;
        auto* navigation=stack();[navigation.widthAnchor constraintEqualToConstant:150].active=YES;
        auto* brand=[NSImageView imageViewWithImage:NSApp.applicationIconImage];[brand.widthAnchor constraintEqualToConstant:44].active=YES;[brand.heightAnchor constraintEqualToConstant:44].active=YES;[navigation addArrangedSubview:brand];[navigation addArrangedSubview:label(@"ExtraPane",20)];
        self.pages=[NSTabView new];self.pages.tabViewType=NSNoTabsNoBorder;self.pages.translatesAutoresizingMaskIntoConstraints=NO;
        [shell addArrangedSubview:navigation];[shell addArrangedSubview:self.pages];[self.pages.heightAnchor constraintEqualToAnchor:shell.heightAnchor constant:-24].active=YES;
        __weak ExtraPanePeerApp* weak=self;
        for(NSString* name in @[@"Devices",@"Share",@"Connections",@"About"]){[navigation addArrangedSubview:button(name,^{[weak.pages selectTabViewItemWithIdentifier:name];})];}
        self.status=label(@"Ready");[navigation addArrangedSubview:self.status];
        auto* devices=[self page:@"Devices"];[devices addArrangedSubview:label(@"Connect to a device by its reachable address, or choose one discovered on your local network.")];
        auto* target=stack(YES);self.address=[NSTextField textFieldWithString:@""];self.address.placeholderString=@"Address or localhost";self.address.delegate=self;[self.address.widthAnchor constraintGreaterThanOrEqualToConstant:240].active=YES;self.port=[NSTextField textFieldWithString:@"43824"];self.port.delegate=self;[self.port.widthAnchor constraintEqualToConstant:85].active=YES;[target addArrangedSubview:self.address];[target addArrangedSubview:self.port];[devices addArrangedSubview:target];
        auto* routeRow=stack(YES);[routeRow addArrangedSubview:label(@"Connect through")];self.outboundRoute=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];[self.outboundRoute.widthAnchor constraintGreaterThanOrEqualToConstant:280].active=YES;self.outboundRoute.menu.delegate=self;[routeRow addArrangedSubview:self.outboundRoute];[devices addArrangedSubview:routeRow];[self refreshRoutes];
        auto* auth=stack(YES);self.code=[NSSecureTextField textFieldWithString:@""];self.code.placeholderString=@"Pairing code";[auth addArrangedSubview:self.code];[auth addArrangedSubview:button(@"Connect",^{ExtraPanePeerApp* strong=weak;if(!strong)return;[weak connectAddress:str(weak.address.stringValue) port:portValue(weak.port.stringValue) expected:strong->expected code:str(weak.code.stringValue) open:weak.openConnection.state==NSControlStateValueOn];})];[devices addArrangedSubview:auth];
        self.openConnection=button(@"Connect without pairing",^{});[self.openConnection setButtonType:NSButtonTypeSwitch];[devices addArrangedSubview:self.openConnection];
        [devices addArrangedSubview:button(@"Find nearby devices",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{if(!strong->scanning){strong->discovery.start(strong->sharing?str(weak.bindAddress.stringValue):std::string{});strong->scanning=true;if(strong->sharing)[weak publish];}else [weak poll];}catch(const std::exception& e){[weak error:text(e.what())];}})];self.nearby=stack();[devices addArrangedSubview:self.nearby];
        [devices addArrangedSubview:label(@"Remembered devices",18)];self.saved=stack();[devices addArrangedSubview:self.saved];self.remoteResources=stack();[devices addArrangedSubview:self.remoteResources];
        auto* share=[self page:@"Share"];[share addArrangedSubview:label(@"Only enabled resources are offered. Capture begins when an authorized device requests one.")];
        auto* local=stack(YES);self.bindAddress=[NSTextField textFieldWithString:@""];self.bindAddress.placeholderString=@"Local IPv4 address";[self.bindAddress.widthAnchor constraintGreaterThanOrEqualToConstant:240].active=YES;auto addresses=dm::mac::localAddresses();if(!addresses.empty())self.bindAddress.stringValue=text(addresses.front().address);self.listenPort=[NSTextField textFieldWithString:@"43824"];[self.listenPort.widthAnchor constraintEqualToConstant:85].active=YES;[local addArrangedSubview:self.bindAddress];[local addArrangedSubview:self.listenPort];[share addArrangedSubview:local];
        for(const auto& entry:addresses){const auto address=entry.address;[share addArrangedSubview:button(text(entry.interface+" ? "+address),^{ExtraPanePeerApp* strong=weak;if(!strong)return;if(!strong->sharing)weak.bindAddress.stringValue=text(address);})];}
        self.requirePairing=button(@"Require pairing",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{const bool value=weak.requirePairing.state==NSControlStateValueOn;strong->service->pairing_required(value);[[NSUserDefaults standardUserDefaults] setBool:value forKey:@"peerPairingRequired"];weak.generatedCode.stringValue=value?text(strong->service->pairing_code()):@"Pairing disabled";[weak publish];}catch(const std::exception& e){[weak error:text(e.what())];}});[self.requirePairing setButtonType:NSButtonTypeSwitch];self.requirePairing.state=required?NSControlStateValueOn:NSControlStateValueOff;[share addArrangedSubview:self.requirePairing];self.generatedCode=label(required?text(service->pairing_code()):@"Pairing disabled",26);[share addArrangedSubview:self.generatedCode];[share addArrangedSubview:label(@"Pairing codes change only when ExtraPane restarts. Remembered devices use their saved identity.")];
        self.gateway=button(@"Allow paired devices to use my paired devices",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{strong->service->gateway(weak.gateway.state==NSControlStateValueOn);}catch(const std::exception& e){[weak error:text(e.what())];}});[self.gateway setButtonType:NSButtonTypeSwitch];[share addArrangedSubview:self.gateway];
        self.offered=stack();[share addArrangedSubview:self.offered];[share addArrangedSubview:button(@"Refresh resources",^{[weak refreshInventory];})];
        auto* permissions=stack(YES);[permissions addArrangedSubview:button(@"Camera permission",^{dm::mac::resource::requestPermission(dm::mac::resource::Kind::Camera,[weak](bool,std::string message){dispatch_async(dispatch_get_main_queue(),^{if(weak){[weak error:text(message)];[weak refreshInventory];}});});})];[permissions addArrangedSubview:button(@"Microphone permission",^{dm::mac::resource::requestPermission(dm::mac::resource::Kind::Microphone,[weak](bool,std::string message){dispatch_async(dispatch_get_main_queue(),^{if(weak){[weak error:text(message)];[weak refreshInventory];}});});})];[share addArrangedSubview:permissions];
        auto* desktopPermissions=stack(YES);[desktopPermissions addArrangedSubview:button(@"Screen recording permission",^{const bool allowed=CGRequestScreenCaptureAccess();[weak error:allowed?@"Screen recording is allowed":@"Enable ExtraPane in System Settings ? Privacy & Security ? Screen Recording, then restart ExtraPane if requested."];[weak refreshInventory];})];[desktopPermissions addArrangedSubview:button(@"Input permission",^{const bool allowed=AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)@{(__bridge NSString*)kAXTrustedCheckOptionPrompt:@YES});[weak error:allowed?@"Accessibility input is allowed":@"Enable ExtraPane in System Settings ? Privacy & Security ? Accessibility."];})];[share addArrangedSubview:desktopPermissions];
        self.startButton=button(@"Start sharing",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{if(strong->sharing){strong->discovery.withdraw();strong->service->stop();strong->sharing=false;weak.startButton.title=@"Start sharing";weak.startButton.contentTintColor=nil;weak.bindAddress.enabled=YES;weak.listenPort.enabled=YES;}else{const unsigned port=portValue(weak.listenPort.stringValue);const bool badAddress=!dm::mac::ipv4(weak.bindAddress.stringValue.UTF8String);if(badAddress)invalidField(weak.bindAddress);if(!port)invalidField(weak.listenPort);if(badAddress||!port){[weak error:@"Check the highlighted sharing details."];return;}strong->service->start(str(weak.bindAddress.stringValue),static_cast<std::uint16_t>(port));}}catch(const std::exception& e){[weak error:text(e.what())];}});[share addArrangedSubview:self.startButton];
        auto* connections=[self page:@"Connections"];self.disconnectButton=button(@"Disconnect all",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{std::vector<Service::Token> ids;for(const auto& [id,e]:strong->links)ids.push_back(id);for(auto id:ids)strong->service->disconnect(id);}catch(const std::exception& e){[weak error:text(e.what())];}},YES);self.disconnectButton.hidden=YES;[connections addArrangedSubview:self.disconnectButton];self.connectedResources=stack();[connections addArrangedSubview:self.connectedResources];
        auto* about=[self page:@"About"];auto* icon=[NSImageView imageViewWithImage:NSApp.applicationIconImage];[icon.widthAnchor constraintEqualToConstant:96].active=YES;[icon.heightAnchor constraintEqualToConstant:96].active=YES;[about addArrangedSubview:icon];[about addArrangedSubview:label(@"Share devices and extend your desktop.",18)];[about addArrangedSubview:label(@"Device-sharing development workspace")];
        for(NSArray* item in @[@[@"Read the documentation",@"https://github.com/EEkebin/ExtraPane#readme"],@[@"Releases and changelog",@"https://github.com/EEkebin/ExtraPane/releases"],@[@"GitHub repository",@"https://github.com/EEkebin/ExtraPane"]]){[about addArrangedSubview:button(item[0],^{[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:item[1]]];})];}
        auto* menu=[NSMenu new];auto* appItem=[NSMenuItem new];[menu addItem:appItem];auto* appMenu=[NSMenu new];[appMenu addItemWithTitle:@"Quit ExtraPane" action:@selector(terminate:) keyEquivalent:@"q"];appItem.submenu=appMenu;NSApp.mainMenu=menu;
        self.tray=[[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];self.tray.button.image=[NSImage imageWithSystemSymbolName:@"rectangle.on.rectangle" accessibilityDescription:@"ExtraPane"];self.tray.button.target=self;self.tray.button.action=@selector(showWindow:);
        self.timer=[NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer* timer){(void)timer;[weak poll];}];[self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[self refreshInventory];[self renderDevices];
    }catch(const std::exception& e){NSAlert* alert=[NSAlert new];alert.messageText=@"ExtraPane could not start";alert.informativeText=text(e.what());[alert runModal];[NSApp terminate:nil];}
}
-(void)refreshRoutes{
    NSString* selected=self.outboundRoute.selectedItem.representedObject?:@"";
    try{const auto addresses=dm::mac::localAddresses();[self.outboundRoute removeAllItems];[self.outboundRoute addItemWithTitle:@"Automatic route"];self.outboundRoute.lastItem.representedObject=@"";
        for(const auto& entry:addresses){[self.outboundRoute addItemWithTitle:text(entry.interface+" - "+entry.address)];self.outboundRoute.lastItem.representedObject=text(entry.address);}
        for(NSMenuItem* item in self.outboundRoute.itemArray)if([item.representedObject isEqual:selected]){[self.outboundRoute selectItem:item];break;}
    }catch(const std::exception& e){[self error:text(e.what())];}
}
-(void)menuNeedsUpdate:(NSMenu*)menu{if(menu==self.outboundRoute.menu)[self refreshRoutes];}
-(void)controlTextDidChange:(NSNotification*)notification{if(notification.object==self.address||notification.object==self.port)expected={};}
-(void)error:(NSString*)message{self.status.stringValue=message?:@"";self.status.textColor=NSColor.systemYellowColor;}
-(void)connectAddress:(std::string)address port:(unsigned)port expected:(DeviceId)identity code:(std::string)code open:(BOOL)open{
    try{
        const auto saved=service->remembered();auto normalized=[](std::string value){if(value.size()==9){auto lower=value;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});if(lower=="localhost")return std::string("127.0.0.1");}return value;};
        const auto hinted=std::count_if(saved.begin(),saved.end(),[&](const auto& item){return item.port_hint==port&&normalized(item.address_hint)==normalized(address);});
        if(!open&&!valid_id(identity)&&code.empty()&&hinted>1){invalidField(self.address);[self error:@"Choose the intended device from Remembered devices."];return;}
        // Address hints choose a saved key candidate only. Service still pins and proves its identity.
        const bool remembered=(valid_id(identity)&&std::any_of(saved.begin(),saved.end(),[&](const auto& item){return item.id==identity;}))||(!valid_id(identity)&&code.empty()&&hinted==1);
        const bool badAddress=!dm::mac::ipv4(address.c_str()),badPort=!port||port>65535;
        const bool badCode=!open&&!remembered&&(code.size()!=6||!std::all_of(code.begin(),code.end(),[](char c){return c>='0'&&c<='9';}));
        if(badAddress)invalidField(self.address);if(badPort)invalidField(self.port);if(badCode)invalidField(self.code);
        if(badAddress||badPort||badCode){const unsigned count=unsigned(badAddress)+unsigned(badPort)+unsigned(badCode);[self error:count>1?@"Check the highlighted connection details.":badAddress?@"Enter an IPv4 address or localhost.":badPort?@"Enter a port between 1 and 65535.":@"Enter the six-digit pairing code."];return;}
        const auto local=str(self.outboundRoute.selectedItem.representedObject?:@"");if(!local.empty()){const auto connected=dm::mac::localAddresses();if(std::none_of(connected.begin(),connected.end(),[&](const auto& item){return item.address==local;})){invalidField(self.outboundRoute);[self error:@"The selected network address is disconnected. Choose another route."];return;}}service->connect({address,static_cast<std::uint16_t>(port),local,identity,code,bool(open)});self.code.stringValue=@"";self.status.stringValue=@"Connecting...";self.status.textColor=NSColor.labelColor;
    }catch(const std::exception& e){[self error:text(e.what())];}
}
-(void)refreshInventory{if(refreshing||terminating)return;refreshing=true;auto shared=runtime;__weak ExtraPanePeerApp* weak=self;dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{try{auto offers=shared->refresh();dispatch_async(dispatch_get_main_queue(),^{ExtraPanePeerApp* strong=weak;if(!strong||strong->terminating)return;strong->inventory=offers;strong->refreshing=false;[strong publish];[strong renderOffers];});}catch(const std::exception& e){auto message=text(e.what());dispatch_async(dispatch_get_main_queue(),^{ExtraPanePeerApp* strong=weak;if(!strong)return;if(weak){strong->refreshing=false;[weak error:message];}});}});}
-(void)publish{if(!service)return;try{std::vector<Offer> offers;for(const auto& offer:inventory)if(enabled.contains(offer.id))offers.push_back(offer);service->offers(offers);if(sharing){Announcement a;a.device=service->identity();a.instance=instance;a.sequence=++sequence;a.name=str(NSProcessInfo.processInfo.hostName);a.port=listeningPort;a.pairing_required=self.requirePairing.state==NSControlStateValueOn;for(const auto& offer:offers)if(offer.available)a.offered_kinds|=1u<<static_cast<unsigned>(offer.kind);discovery.advertise(a);}}catch(const std::exception& e){[self error:text(e.what())];}}
-(void)renderOffers{clear(self.offered);__weak ExtraPanePeerApp* weak=self;for(const auto& offer:inventory){auto id=offer.id;auto* toggle=button(text(offer.label),^{ExtraPanePeerApp* strong=weak;if(!strong)return;if(strong->enabled.contains(id))strong->enabled.erase(id);else strong->enabled.insert(id);NSMutableArray* saved=[NSMutableArray array];for(const auto& item:strong->enabled)[saved addObject:text(item)];[[NSUserDefaults standardUserDefaults] setObject:saved forKey:@"peerOffers"];[weak publish];});[toggle setButtonType:NSButtonTypeSwitch];toggle.state=enabled.contains(id)?NSControlStateValueOn:NSControlStateValueOff;toggle.enabled=offer.available;[self.offered addArrangedSubview:toggle];if(!offer.available)[self.offered addArrangedSubview:label(text(offer.unavailable_reason))];}}
-(void)renderDevices{
    clear(self.saved);clear(self.remoteResources);if(!service)return;__weak ExtraPanePeerApp* weak=self;
    for(const auto& saved:service->remembered()){auto* row=stack(YES);auto item=saved;auto* connect=button(text(saved.name.empty()?hex(saved.id).substr(0,12):saved.name),^{[weak connectAddress:item.address_hint port:item.port_hint expected:item.id code:std::string{} open:NO];});connect.enabled=!saved.address_hint.empty()&&saved.port_hint;[row addArrangedSubview:connect];[row addArrangedSubview:button(@"Forget",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{strong->service->forget(item.id);}catch(const std::exception& e){[weak error:text(e.what())];}},YES)];[self.saved addArrangedSubview:row];}
    for(const auto& [connection,event]:links){if(event.context.incoming)continue;[self.remoteResources addArrangedSubview:label(event.verified?@"Paired device":@"Unverified device",18)];auto connectionId=connection;[self.remoteResources addArrangedSubview:button(@"Disconnect device",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{strong->service->disconnect(connectionId);}catch(const std::exception& e){[weak error:text(e.what())];}},YES)];for(const auto& catalog:catalogs[connection]){[self.remoteResources addArrangedSubview:label(text(catalog.name),16)];for(const auto& offer:catalog.offers){auto owner=catalog.device;auto item=offer;auto* card=stack();[card addArrangedSubview:label(text(offer.label))];const auto unavailable=dm::mac::peer::MacPeerRuntime::receiveUnavailable(item.kind);auto* control=button(@"Allow input",^{});[control setButtonType:NSButtonTypeSwitch];control.hidden=!item.input_allowed;control.state=item.kind==Kind::display_destination&&item.input_allowed?NSControlStateValueOn:NSControlStateValueOff;[card addArrangedSubview:control];
        NSTextField* width=[NSTextField textFieldWithString:[NSString stringWithFormat:@"%u",item.width?item.width:1920]];NSTextField* height=[NSTextField textFieldWithString:[NSString stringWithFormat:@"%u",item.height?item.height:1080]];NSTextField* fps=[NSTextField textFieldWithString:[NSString stringWithFormat:@"%u",std::min(item.refresh?item.refresh:60u,360u)]];
        if(item.kind==Kind::display_destination){auto* dimensions=stack(YES);[dimensions addArrangedSubview:label(@"Width")];[dimensions addArrangedSubview:width];[dimensions addArrangedSubview:label(@"Height")];[dimensions addArrangedSubview:height];[dimensions addArrangedSubview:label(@"Hz / FPS")];[dimensions addArrangedSubview:fps];[card addArrangedSubview:dimensions];}
        auto* use=button(kind(item.kind),^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{rpc::Open request{owner,item.id,bool(item.input_allowed&&control.state==NSControlStateValueOn)};if(item.kind==Kind::display_destination){request.width=portValue(width.stringValue);request.height=portValue(height.stringValue);request.refresh=portValue(fps.stringValue);const bool badWidth=request.width<640||request.width>4096||request.width%2,badHeight=request.height<480||request.height>4096||request.height%2,badRate=!request.refresh||request.refresh>360,badArea=std::uint64_t(request.width)*request.height>8847360;if(badWidth||badArea)invalidField(width);if(badHeight||badArea)invalidField(height);if(badRate)invalidField(fps);if(badWidth||badHeight||badRate||badArea){[weak error:@"Check the highlighted display mode."];return;}}rpc::encode(request);const auto token=strong->service->open(connectionId,request);strong->pending[{connectionId,token}]=item.kind;[weak renderConnections];}catch(const std::exception& e){[weak error:text(e.what())];}});use.enabled=item.available&&unavailable.empty();[card addArrangedSubview:use];if(!use.enabled)[card addArrangedSubview:label(text(unavailable.empty()?item.unavailable_reason:unavailable))];[self.remoteResources addArrangedSubview:card];}}}
}
-(void)renderConnections{clear(self.connectedResources);self.disconnectButton.hidden=resources.empty();__weak ExtraPanePeerApp* weak=self;for(const auto& [id,event]:resources){(void)id;auto c=event.context;auto* row=stack(YES);[row addArrangedSubview:label(retrying.contains(id)?[kind(c.kind) stringByAppendingString:@" - Reconnecting..."]:kind(c.kind))];[row addArrangedSubview:button(retrying.contains(id)?@"Cancel retry":@"Disconnect",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{strong->service->close(c.connection,c.grant);if(strong->retrying.erase({c.connection,c.grant})){strong->resources.erase({c.connection,c.grant});[weak renderConnections];}}catch(const std::exception& e){[weak error:text(e.what())];}},YES)];[self.connectedResources addArrangedSubview:row];}for(const auto& [key,type]:pending){const auto connection=key.first,request=key.second;auto* row=stack(YES);[row addArrangedSubview:label([kind(type) stringByAppendingString:@" - Connecting..."])];[row addArrangedSubview:button(@"Cancel",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{strong->service->cancel(connection,request);strong->pending.erase({connection,request});[weak renderConnections];}catch(const std::exception& e){[weak error:text(e.what())];}},YES)];[self.connectedResources addArrangedSubview:row];}if(resources.empty()&&pending.empty())[self.connectedResources addArrangedSubview:label(@"No resources connected")];}
-(void)poll{
    if(!service||terminating)return;bool devicesChanged=false,connectionsChanged=false;
    for(auto& event:service->poll()){auto id=event.context.connection;switch(event.kind){
        case Service::EventKind::listening:instance=std::chrono::steady_clock::now().time_since_epoch().count()|1;sequence=0;sharing=true;listeningPort=event.port;self.startButton.title=@"Stop sharing";self.startButton.contentTintColor=NSColor.systemRedColor;self.bindAddress.enabled=NO;self.listenPort.enabled=NO;try{discovery.start(str(self.bindAddress.stringValue));scanning=true;[self publish];}catch(const std::exception& e){[self error:text(e.what())];}break;
        case Service::EventKind::connected:links[id]=event;if(!event.context.incoming)service->catalog(id);devicesChanged=true;break;
        case Service::EventKind::catalog:catalogs[id]=event.catalogs;devicesChanged=true;break;
        case Service::EventKind::granted:pending.erase({id,event.context.request});if(event.replaces_grant){const auto old=std::make_pair(id,event.replaces_grant);retrying.erase(old);resources.erase(old);}resources[{event.context.connection,event.context.grant}]=event;connectionsChanged=true;self.status.stringValue=@"Resource connected";break;
        case Service::EventKind::reconnecting:std::erase_if(pending,[&](const auto& entry){return entry.first.first==id;});for(const auto& [key,item]:resources)if(key.first==id&&!item.context.incoming&&item.context.kind!=Kind::usb)retrying.insert(key);catalogs.erase(id);self.status.stringValue=text(event.detail);devicesChanged=connectionsChanged=true;break;
        case Service::EventKind::stopped:{const auto key=std::make_pair(id,event.context.grant);if(!retrying.contains(key)||!event.detail.empty()){retrying.erase(key);resources.erase(key);}connectionsChanged=true;break;}
        case Service::EventKind::disconnected:std::erase_if(pending,[&](const auto& entry){return entry.first.first==id;});std::erase_if(retrying,[&](const auto& key){return key.first==id;});links.erase(id);catalogs.erase(id);for(auto i=resources.begin();i!=resources.end();)if(i->second.context.connection==id)i=resources.erase(i);else ++i;devicesChanged=connectionsChanged=true;break;
        case Service::EventKind::error:if(pending.erase({id,event.context.request}))connectionsChanged=true;[self error:text(event.detail)];break;
        case Service::EventKind::trust_changed:devicesChanged=true;break;
    }}
    if(devicesChanged)[self renderDevices];if(connectionsChanged)[self renderConnections];
    if(scanning){auto routes=discovery.poll();static std::vector<std::string> previous;std::vector<std::string> next;for(const auto& route:routes)next.push_back(hex(route.announcement.device)+route.observed_ipv4+std::to_string(route.announcement.port));if(next!=previous){previous=next;clear(self.nearby);__weak ExtraPanePeerApp* weak=self;for(const auto& route:routes){auto selected=route;[self.nearby addArrangedSubview:button(text(route.announcement.name+" ? "+route.observed_ipv4+":"+std::to_string(route.announcement.port)),^{ExtraPanePeerApp* strong=weak;if(!strong)return;weak.address.stringValue=text(selected.observed_ipv4);weak.port.stringValue=[NSString stringWithFormat:@"%u",selected.announcement.port];strong->expected=selected.announcement.device;[weak.window makeFirstResponder:weak.code];})];}}}
}
-(void)showWindow:(id)sender{(void)sender;[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];}
-(void)quit:(id)sender{(void)sender;[NSApp terminate:nil];}
-(BOOL)windowShouldClose:(NSWindow*)window{if(sharing||!links.empty()){[window orderOut:nil];return NO;}[NSApp terminate:nil];return NO;}
-(void)applicationWillTerminate:(NSNotification*)notification{(void)notification;terminating=true;[self.timer invalidate];discovery.stop();service.reset();if(runtime)runtime->shutdown();if(self.tray)[[NSStatusBar systemStatusBar] removeStatusItem:self.tray];}
@end
int runPeerWorkspace(){[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];ExtraPanePeerApp* app=[ExtraPanePeerApp new];NSApp.delegate=app;[NSApp run];return 0;}
