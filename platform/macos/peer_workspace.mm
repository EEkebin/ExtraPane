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
#include <stdexcept>
#include <limits>
using namespace extrapane::peers;
namespace {
struct PendingResource {Kind kind;rpc::Open resource;Service::Token monitorRow=0;};
struct MonitorDraft {Service::Token connection=0;DeviceId owner{};std::string resource;Kind kind=Kind::display_destination;std::string width="1920",height="1080",refresh="60";bool input=true;};
struct PeerDestination {std::string address;unsigned port;};
std::string normalizedAddress(std::string value){auto lower=value;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});return lower=="localhost"?"127.0.0.1":value;}
NSString* text(const std::string& s){return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding]?:@"";}
std::string str(NSString* s){return s.UTF8String?:"";}
std::string hex(const DeviceId& id){constexpr char digits[]="0123456789abcdef";std::string s;for(auto b:id){s+=digits[b>>4];s+=digits[b&15];}return s;}
NSString* kind(Kind k){switch(k){case Kind::display_destination:return @"Extend desktop";case Kind::existing_screen:return @"View existing screen";case Kind::camera:return @"Camera";case Kind::microphone:return @"Microphone";case Kind::speaker:return @"Speakers";case Kind::usb:return @"USB storage";}return @"Resource";}
NSStackView* stack(BOOL horizontal=NO){auto* s=[NSStackView new];s.orientation=horizontal?NSUserInterfaceLayoutOrientationHorizontal:NSUserInterfaceLayoutOrientationVertical;s.alignment=horizontal?NSLayoutAttributeCenterY:NSLayoutAttributeLeading;s.spacing=12;s.translatesAutoresizingMaskIntoConstraints=NO;return s;}
NSStackView* borderedStack(){auto* v=stack();v.edgeInsets=NSEdgeInsetsMake(12,12,12,12);v.wantsLayer=YES;v.layer.borderWidth=1;v.layer.borderColor=NSColor.separatorColor.CGColor;v.layer.cornerRadius=10;return v;}
NSTextField* label(NSString* s,CGFloat size=14){auto* v=[NSTextField wrappingLabelWithString:s];v.font=[NSFont systemFontOfSize:size];return v;}
unsigned portValue(NSString* value){const auto digits=str(value);if(digits.empty()||digits.size()>5||!std::all_of(digits.begin(),digits.end(),[](char c){return c>='0'&&c<='9';}))return 0;unsigned result=0;for(char c:digits)result=result*10+unsigned(c-'0');return result<=65535?result:0;}
void invalidField(NSView* field){field.wantsLayer=YES;field.layer.borderColor=NSColor.systemRedColor.CGColor;field.layer.borderWidth=2;field.layer.cornerRadius=5;__weak NSView* weak=field;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2500*NSEC_PER_MSEC),dispatch_get_main_queue(),^{weak.layer.borderWidth=0;});}
void separator(NSStackView* parent){auto* line=[NSBox new];line.boxType=NSBoxSeparator;line.translatesAutoresizingMaskIntoConstraints=NO;[parent addArrangedSubview:line];[line.widthAnchor constraintEqualToAnchor:parent.widthAnchor constant:-(parent.edgeInsets.left+parent.edgeInsets.right)].active=YES;}
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
@interface ExtraPanePeerPopup:NSPopUpButton
@property(nonatomic,copy) void (^selectionBlock)(NSInteger);
-(void)changed:(id)sender;
@end
@implementation ExtraPanePeerPopup
-(void)changed:(id)sender{(void)sender;if(self.selectionBlock)self.selectionBlock(self.indexOfSelectedItem);}
@end
@interface ExtraPanePeerField:NSTextField<NSTextFieldDelegate>
@property(nonatomic,copy) void (^editBlock)(NSString*);
@end
@implementation ExtraPanePeerField
-(void)controlTextDidChange:(NSNotification*)notification{(void)notification;if(self.editBlock)self.editBlock(self.stringValue);}
@end
@interface ExtraPanePeerApp:NSObject<NSApplicationDelegate,NSWindowDelegate,NSTextFieldDelegate,NSMenuDelegate>{
    std::shared_ptr<dm::mac::peer::MacPeerRuntime> runtime;
    std::unique_ptr<Service> service;
    dm::mac::PeerDiscovery discovery;
    dm::mac::PeerDiscovery searchDiscovery;
    std::vector<Route> nearbyRoutes;
    std::vector<std::string> nearbySignature;
    std::map<std::pair<DeviceId,std::string>,std::chrono::steady_clock::time_point> observedRoutes;
    std::chrono::steady_clock::time_point searchUntil;
    std::vector<Offer> inventory;
    std::set<std::string> enabled;
    std::map<Service::Token,Service::Event> links;
    std::set<Service::Token> connectingLinks;
    std::map<Service::Token,PeerDestination> destinations;
    std::map<std::pair<Service::Token,Service::Token>,Service::Event> resources;
    std::map<Service::Token,std::vector<Catalog>> catalogs;
    std::set<std::pair<std::string,Service::Token>> collapsedDevices;
    std::set<std::pair<Service::Token,Service::Token>> retrying;
    std::map<std::pair<Service::Token,Service::Token>,PendingResource> pending;
    std::map<Service::Token,DeviceId> endpointPairing;
    std::map<Service::Token,MonitorDraft> monitorDrafts;
    std::map<std::pair<Service::Token,Service::Token>,Service::Token> resourceRows;
    Service::Token monitorSequence;
    std::uint64_t instance,sequence;
    DeviceId expected;
    bool sharing,sharingStarting,gatewayEnabled,loopbackSharing,scanning,refreshing,terminating;
    Service::Token sharingGeneration;
    unsigned listeningPort;
    std::string listeningAddress;
}
@property(nonatomic,strong) NSWindow* window;
@property(nonatomic,strong) NSTextField* address;
@property(nonatomic,strong) NSTextField* port;
@property(nonatomic,strong) NSPopUpButton* outboundRoute;
@property(nonatomic,strong) NSTextField* bindAddress;
@property(nonatomic,strong) NSTextField* listenPort;
@property(nonatomic,strong) NSTextField* status;
@property(nonatomic,strong) NSTextField* generatedCode;
@property(nonatomic,strong) NSButton* requirePairing;
@property(nonatomic,strong) NSButton* openConnection;
@property(nonatomic,strong) NSButton* gateway;
@property(nonatomic,strong) NSButton* stopSharingButton;
@property(nonatomic,strong) NSButton* applyNetworkButton;
@property(nonatomic,strong) NSButton* manualButton;
@property(nonatomic,strong) NSButton* manualConnectButton;
@property(nonatomic,strong) NSStackView* manualFields;
@property(nonatomic,strong) NSButton* disconnectButton;
@property(nonatomic,strong) NSStackView* nearby;
@property(nonatomic,strong) NSTextField* searchStatus;
@property(nonatomic,strong) NSButton* searchButton;
@property(nonatomic,strong) NSStackView* searchDots;
@property(nonatomic,strong) NSStackView* aboutHero;
@property(nonatomic,strong) NSStackView* linkedDevices;
@property(nonatomic,strong) NSStackView* saved;
@property(nonatomic,strong) NSStackView* offered;
@property(nonatomic,strong) NSStackView* remoteResources;
@property(nonatomic,strong) NSStackView* connectedResources;
@property(nonatomic,strong) NSTabView* pages;
@property(nonatomic,strong) NSTimer* timer;
@property(nonatomic,strong) NSStatusItem* tray;
-(void)poll;
-(void)beginSharing;
-(std::string)validatedBindAddress;
-(BOOL)hasIncomingUses;
-(void)applyNetworkSettings;
-(void)stopSharing;
-(void)stopListener;
-(void)updateSharingControls;
-(void)toggleOffer:(std::string)identifier;
-(void)toggleGateway;
-(void)startDiscovery;
-(void)updateSearchAppearance;
-(void)showManual;
-(void)updateManualConnect;
-(void)refreshInventory;
-(void)refreshRoutes;
-(void)publish;
-(void)renderOffers;
-(void)renderConnections;
-(void)renderDevices;
-(void)renderCatalogs;
-(NSStackView*)resourceCard:(Offer)item owner:(DeviceId)owner connection:(Service::Token)connectionId row:(Service::Token)rowId choices:(std::vector<Offer>)choices;
-(void)renderNearby;
-(void)observeRoutes:(const std::vector<Route>&)routes;
-(void)connectAddress:(std::string)address port:(unsigned)port expected:(DeviceId)identity code:(std::string)code open:(BOOL)open;
-(BOOL)requestPairingCode:(NSString*)target result:(std::string&)code;
-(void)pairVia:(Service::Token)gateway owner:(DeviceId)owner name:(NSString*)name;
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
    (void)notification;sharingGeneration=0;monitorSequence=0;
    try{
        runtime=std::make_shared<dm::mac::peer::MacPeerRuntime>();
        NSString* support=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES) firstObject];NSString* parent=[support stringByAppendingPathComponent:@"ExtraPane"];
        [[NSFileManager defaultManager] createDirectoryAtPath:parent withIntermediateDirectories:YES attributes:nil error:nil];
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"peerOffers"];
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
        for(NSString* name in @[@"Devices",@"Resources",@"About"]){[navigation addArrangedSubview:button(name,^{[weak.pages selectTabViewItemWithIdentifier:name];})];}
        self.status=label(@"Ready");[navigation addArrangedSubview:self.status];
        auto* devices=[self page:@"Devices"];[devices addArrangedSubview:label(@"Pair a nearby device or use its address, then start streams with resource power buttons.")];
        auto* nearbyHeader=stack(YES);[nearbyHeader addArrangedSubview:label(@"Nearby devices",26)];self.searchButton=button(@"",^{[weak startDiscovery];});self.searchButton.image=[NSImage imageWithSystemSymbolName:@"arrow.clockwise" accessibilityDescription:@"Search nearby devices"];self.searchButton.toolTip=@"Search nearby devices";self.searchButton.bordered=NO;self.searchButton.wantsLayer=YES;[self.searchButton.widthAnchor constraintEqualToConstant:36].active=YES;[self.searchButton.heightAnchor constraintEqualToConstant:36].active=YES;[nearbyHeader addArrangedSubview:self.searchButton];
        self.searchDots=stack(YES);self.searchDots.spacing=5;self.searchDots.hidden=YES;for(unsigned i=0;i<3;++i){auto* dot=[NSView new];dot.wantsLayer=YES;dot.layer.cornerRadius=3.5;dot.layer.backgroundColor=NSColor.secondaryLabelColor.CGColor;[dot.widthAnchor constraintEqualToConstant:7].active=YES;[dot.heightAnchor constraintEqualToConstant:7].active=YES;[self.searchDots addArrangedSubview:dot];}[nearbyHeader addArrangedSubview:self.searchDots];[devices addArrangedSubview:nearbyHeader];separator(devices);self.searchStatus=label(@"Search to find devices on your local network.");[devices addArrangedSubview:self.searchStatus];self.nearby=stack();[devices addArrangedSubview:self.nearby];[self.nearby.widthAnchor constraintEqualToAnchor:devices.widthAnchor constant:-48].active=YES;
        [devices addArrangedSubview:label(@"Paired devices",26)];separator(devices);self.saved=stack();[devices addArrangedSubview:self.saved];
        self.manualButton=button(@"Connect by address",^{weak.manualFields.hidden=!weak.manualFields.hidden;weak.manualButton.image=[NSImage imageWithSystemSymbolName:weak.manualFields.hidden?@"chevron.right":@"chevron.down" accessibilityDescription:nil];});self.manualButton.image=[NSImage imageWithSystemSymbolName:@"chevron.right" accessibilityDescription:nil];self.manualButton.imagePosition=NSImageLeft;[devices addArrangedSubview:self.manualButton];
        self.manualFields=stack();self.manualFields.hidden=YES;[self.manualFields addArrangedSubview:label(@"Use a local, static or forwarded address when the device is not listed nearby.")];[devices addArrangedSubview:self.manualFields];
        auto* target=stack(YES);self.address=[NSTextField textFieldWithString:@""];self.address.placeholderString=@"Address or localhost";self.address.delegate=self;[self.address.widthAnchor constraintGreaterThanOrEqualToConstant:240].active=YES;self.port=[NSTextField textFieldWithString:@"43824"];self.port.delegate=self;[self.port.widthAnchor constraintEqualToConstant:85].active=YES;[target addArrangedSubview:self.address];[target addArrangedSubview:self.port];[self.manualFields addArrangedSubview:target];
        auto* routeRow=stack(YES);[routeRow addArrangedSubview:label(@"Connect through")];self.outboundRoute=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];[self.outboundRoute.widthAnchor constraintGreaterThanOrEqualToConstant:280].active=YES;self.outboundRoute.menu.delegate=self;[routeRow addArrangedSubview:self.outboundRoute];[self.manualFields addArrangedSubview:routeRow];[self refreshRoutes];
        self.manualConnectButton=button(@"Connect",^{ExtraPanePeerApp* strong=weak;if(!strong)return;[weak connectAddress:str(weak.address.stringValue) port:portValue(weak.port.stringValue) expected:strong->expected code:std::string{} open:weak.openConnection.state==NSControlStateValueOn];});[self.manualFields addArrangedSubview:self.manualConnectButton];
        self.openConnection=button(@"Connect without pairing",^{});[self.openConnection setButtonType:NSButtonTypeSwitch];[self.manualFields addArrangedSubview:self.openConnection];
        self.linkedDevices=stack();[devices addArrangedSubview:self.linkedDevices];[self.linkedDevices.widthAnchor constraintEqualToAnchor:devices.widthAnchor constant:-48].active=YES;
        auto* share=devices;[share addArrangedSubview:label(@"This device",22)];[share addArrangedSubview:label(@"Available resources are discoverable automatically. Capture begins only when an authorized device connects.")];
        auto* local=stack(YES);self.bindAddress=[NSTextField textFieldWithString:@""];self.bindAddress.placeholderString=@"Local IPv4 address";[self.bindAddress.widthAnchor constraintGreaterThanOrEqualToConstant:240].active=YES;auto addresses=dm::mac::localAddresses();if(!addresses.empty())self.bindAddress.stringValue=text(addresses.front().address);self.listenPort=[NSTextField textFieldWithString:@"43824"];[self.listenPort.widthAnchor constraintEqualToConstant:85].active=YES;[local addArrangedSubview:self.bindAddress];[local addArrangedSubview:self.listenPort];[share addArrangedSubview:local];
        for(const auto& entry:addresses){const auto address=entry.address;[share addArrangedSubview:button(text(entry.interface+" ? "+address),^{ExtraPanePeerApp* strong=weak;if(!strong)return;if(![strong hasIncomingUses]&&!strong->sharingStarting)weak.bindAddress.stringValue=text(address);})];}
        self.applyNetworkButton=button(@"Apply network settings",^{[weak applyNetworkSettings];});[share addArrangedSubview:self.applyNetworkButton];
        self.requirePairing=button(@"Require pairing",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{const bool value=weak.requirePairing.state==NSControlStateValueOn;strong->service->pairing_required(value);[[NSUserDefaults standardUserDefaults] setBool:value forKey:@"peerPairingRequired"];weak.generatedCode.stringValue=value?text(strong->service->pairing_code()):@"Pairing disabled";[weak publish];}catch(const std::exception& e){[weak error:text(e.what())];}});[self.requirePairing setButtonType:NSButtonTypeSwitch];self.requirePairing.state=required?NSControlStateValueOn:NSControlStateValueOff;[share addArrangedSubview:self.requirePairing];self.generatedCode=label(required?text(service->pairing_code()):@"Pairing disabled",26);[share addArrangedSubview:self.generatedCode];[share addArrangedSubview:label(@"Pairing codes change only when ExtraPane restarts. Remembered devices use their saved identity.")];
        self.gateway=button(@"Allow paired devices to use my paired devices",^{[weak toggleGateway];});[self.gateway setButtonType:NSButtonTypeSwitch];[share addArrangedSubview:self.gateway];
        self.offered=stack();[share addArrangedSubview:self.offered];[share addArrangedSubview:button(@"Refresh resources",^{[weak refreshInventory];})];
        auto* permissions=stack(YES);[permissions addArrangedSubview:button(@"Camera permission",^{dm::mac::resource::requestPermission(dm::mac::resource::Kind::Camera,[weak](bool,std::string message){dispatch_async(dispatch_get_main_queue(),^{if(weak){[weak error:text(message)];[weak refreshInventory];}});});})];[permissions addArrangedSubview:button(@"Microphone permission",^{dm::mac::resource::requestPermission(dm::mac::resource::Kind::Microphone,[weak](bool,std::string message){dispatch_async(dispatch_get_main_queue(),^{if(weak){[weak error:text(message)];[weak refreshInventory];}});});})];[share addArrangedSubview:permissions];
        auto* desktopPermissions=stack(YES);[desktopPermissions addArrangedSubview:button(@"Screen recording permission",^{const bool allowed=CGRequestScreenCaptureAccess();[weak error:allowed?@"Screen recording is allowed":@"Enable ExtraPane in System Settings ? Privacy & Security ? Screen Recording, then restart ExtraPane if requested."];[weak refreshInventory];})];[desktopPermissions addArrangedSubview:button(@"Input permission",^{const bool allowed=AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)@{(__bridge NSString*)kAXTrustedCheckOptionPrompt:@YES});[weak error:allowed?@"Accessibility input is allowed":@"Enable ExtraPane in System Settings ? Privacy & Security ? Accessibility."];})];[share addArrangedSubview:desktopPermissions];
        self.stopSharingButton=button(@"Stop sharing all",^{[weak stopSharing];},YES);self.stopSharingButton.hidden=YES;[share addArrangedSubview:self.stopSharingButton];
        auto* connections=[self page:@"Resources"];self.remoteResources=stack();[connections addArrangedSubview:self.remoteResources];[self.remoteResources.widthAnchor constraintEqualToAnchor:connections.widthAnchor constant:-48].active=YES;[connections addArrangedSubview:label(@"Active resources",18)];self.disconnectButton=button(@"Disconnect all resources",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{for(const auto& [key,event]:strong->resources){(void)event;strong->service->close(key.first,key.second);}for(const auto& [key,entry]:strong->pending){(void)entry;strong->service->cancel(key.first,key.second);}}catch(const std::exception& e){[weak error:text(e.what())];}},YES);self.disconnectButton.hidden=YES;[connections addArrangedSubview:self.disconnectButton];self.connectedResources=stack();[connections addArrangedSubview:self.connectedResources];
        auto* about=[self page:@"About"];self.aboutHero=stack(YES);self.aboutHero.spacing=24;
        auto* icon=[NSImageView imageViewWithImage:NSApp.applicationIconImage];icon.imageScaling=NSImageScaleProportionallyUpOrDown;[icon.widthAnchor constraintEqualToConstant:128].active=YES;[icon.heightAnchor constraintEqualToConstant:128].active=YES;[self.aboutHero addArrangedSubview:icon];
        auto* description=stack();[description addArrangedSubview:label(@"ExtraPane",32)];[description addArrangedSubview:label(@"Share resources between your devices.",18)];auto* version=label(text(std::string("Running version: ")+EXTRAPANE_MAC_VERSION));version.textColor=NSColor.secondaryLabelColor;[description addArrangedSubview:version];[self.aboutHero addArrangedSubview:description];[about addArrangedSubview:self.aboutHero];[self.aboutHero.widthAnchor constraintEqualToAnchor:about.widthAnchor constant:-48].active=YES;separator(about);
        [about addArrangedSubview:label(@"Extend your desktop and connect cameras, microphones, speakers and USB devices over your network. Available features depend on the device and operating system.")];
        for(NSArray* item in @[@[@"Read the documentation",@"book",@"https://github.com/EEkebin/ExtraPane#readme"],@[@"Releases and changelog",@"clock",@"https://github.com/EEkebin/ExtraPane/releases"],@[@"GitHub repository",@"chevron.left.forwardslash.chevron.right",@"https://github.com/EEkebin/ExtraPane"],@[@"Report an issue",@"bubble.left",@"https://github.com/EEkebin/ExtraPane/issues"]]){auto* link=button(item[0],^{[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:item[2]]];});link.image=[NSImage imageWithSystemSymbolName:item[1] accessibilityDescription:nil];link.imagePosition=NSImageLeft;[about addArrangedSubview:link];}
        separator(about);auto* licenses=label(@"Third-party components retain their own licenses. See the repository for licensing information.",12);licenses.textColor=NSColor.secondaryLabelColor;[about addArrangedSubview:licenses];
        auto* menu=[NSMenu new];auto* appItem=[NSMenuItem new];[menu addItem:appItem];auto* appMenu=[NSMenu new];[appMenu addItemWithTitle:@"Quit ExtraPane" action:@selector(terminate:) keyEquivalent:@"q"];appItem.submenu=appMenu;NSApp.mainMenu=menu;
        self.tray=[[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];self.tray.button.image=[NSImage imageWithSystemSymbolName:@"rectangle.on.rectangle" accessibilityDescription:@"ExtraPane"];self.tray.button.target=self;self.tray.button.action=@selector(showWindow:);
        self.timer=[NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer* timer){(void)timer;[weak poll];}];[self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];[self refreshInventory];[self renderDevices];service->auto_connect(true);try{[self beginSharing];}catch(const std::exception& e){[self error:text(e.what())];}
    }catch(const std::exception& e){NSAlert* alert=[NSAlert new];alert.messageText=@"ExtraPane could not start";alert.informativeText=text(e.what());[alert runModal];[NSApp terminate:nil];}
}
-(void)showManual{[self updateManualConnect];self.manualFields.hidden=NO;self.manualButton.image=[NSImage imageWithSystemSymbolName:@"chevron.down" accessibilityDescription:nil];}
-(void)updateManualConnect{
    const auto address=normalizedAddress(str(self.address.stringValue));const auto port=portValue(self.port.stringValue);bool connected=false,pendingConnect=false;
    const auto remembered=service->remembered();for(const auto& [id,event]:links){if(event.context.incoming)continue;const auto endpoint=destinations.find(id);const bool rememberedAddress=std::any_of(remembered.begin(),remembered.end(),[&](const auto& device){return device.id==event.context.peer&&normalizedAddress(device.address_hint)==address&&device.port_hint==port;});if((valid_id(expected)&&event.context.peer==expected)||(endpoint!=destinations.end()&&endpoint->second.address==address&&endpoint->second.port==port)||rememberedAddress){connected=true;break;}}
    for(const auto& id:connectingLinks){const auto endpoint=destinations.find(id);if(endpoint!=destinations.end()&&endpoint->second.address==address&&endpoint->second.port==port){pendingConnect=true;break;}}
    self.manualConnectButton.hidden=connected;self.manualConnectButton.enabled=!pendingConnect;
}
-(void)windowDidResize:(NSNotification*)notification{(void)notification;const bool narrow=self.window.contentView.bounds.size.width<820;self.aboutHero.orientation=narrow?NSUserInterfaceLayoutOrientationVertical:NSUserInterfaceLayoutOrientationHorizontal;self.aboutHero.alignment=narrow?NSLayoutAttributeLeading:NSLayoutAttributeCenterY;}
-(void)updateSearchAppearance{
    self.searchButton.enabled=!scanning;self.searchDots.hidden=!scanning;[self.searchButton.layer removeAnimationForKey:@"searchSpin"];
    for(NSView* dot in self.searchDots.arrangedSubviews)[dot.layer removeAnimationForKey:@"searchBounce"];
    if(!scanning||NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion)return;
    auto* spin=[CABasicAnimation animationWithKeyPath:@"transform.rotation.z"];spin.fromValue=@0;spin.toValue=@(6.283185307179586);spin.duration=1;spin.repeatCount=std::numeric_limits<float>::infinity();[self.searchButton.layer addAnimation:spin forKey:@"searchSpin"];
    unsigned index=0;for(NSView* dot in self.searchDots.arrangedSubviews){auto* bounce=[CABasicAnimation animationWithKeyPath:@"transform.translation.y"];bounce.fromValue=@0;bounce.toValue=@5;bounce.duration=0.35;bounce.autoreverses=YES;bounce.repeatCount=std::numeric_limits<float>::infinity();bounce.beginTime=CACurrentMediaTime()+0.12*index++;bounce.timingFunction=[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];[dot.layer addAnimation:bounce forKey:@"searchBounce"];}
}
-(void)startDiscovery{
    try{searchDiscovery.start();scanning=true;searchUntil=std::chrono::steady_clock::now()+std::chrono::seconds(10);nearbyRoutes.clear();nearbySignature.clear();[self updateSearchAppearance];self.searchStatus.stringValue=@"Searching nearby...";[self renderNearby];}catch(const std::exception& e){scanning=false;searchDiscovery.stop();[self updateSearchAppearance];self.searchStatus.stringValue=@"Search unavailable. You can still connect by address.";[self error:text(e.what())];}
}
-(void)observeRoutes:(const std::vector<Route>&)routes{
    auto remembered=service->remembered();for(const auto& removed:service->pending_forgets())remembered.push_back(removed);const auto now=std::chrono::steady_clock::now();std::erase_if(observedRoutes,[&](const auto& entry){return now-entry.second>std::chrono::seconds(60);});
    for(const auto& route:routes){const auto identity=route.announcement.device;if(identity==service->identity()||std::none_of(remembered.begin(),remembered.end(),[&](const auto& saved){return saved.id==identity;}))continue;
        // The validated LAN sender address is a candidate only; Service still pins the saved identity.
        const auto signature=route.observed_ipv4+":"+std::to_string(route.announcement.port);const auto key=std::make_pair(identity,signature);const auto previous=observedRoutes.find(key);if(previous!=observedRoutes.end()&&now-previous->second<std::chrono::seconds(30))continue;
        try{service->observe_route(identity,route.observed_ipv4,route.announcement.port);observedRoutes[key]=now;}catch(const std::exception& e){[self error:text(e.what())];}
    }
}
-(void)renderNearby{
    clear(self.nearby);const auto saved=service->remembered();__weak ExtraPanePeerApp* weak=self;unsigned count=0;
    for(const auto& route:nearbyRoutes){const auto identity=route.announcement.device;if(identity==service->identity()||std::any_of(saved.begin(),saved.end(),[&](const auto& item){return item.id==identity;})||std::any_of(links.begin(),links.end(),[&](const auto& entry){return entry.second.context.peer==identity;}))continue;
        ++count;auto selected=route;auto* row=stack();row.edgeInsets=NSEdgeInsetsMake(12,12,12,12);row.wantsLayer=YES;row.layer.borderWidth=1;row.layer.borderColor=NSColor.separatorColor.CGColor;row.layer.cornerRadius=10;
        auto* cardContent=stack(YES);auto* glyph=[NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"desktopcomputer" accessibilityDescription:@"Device"]];[glyph.widthAnchor constraintEqualToConstant:32].active=YES;[glyph.heightAnchor constraintEqualToConstant:32].active=YES;[cardContent addArrangedSubview:glyph];auto* details=stack();details.spacing=4;[details addArrangedSubview:label(text(route.announcement.name),18)];auto* endpoint=label(text(route.observed_ipv4+":"+std::to_string(route.announcement.port)),12);endpoint.textColor=NSColor.secondaryLabelColor;[details addArrangedSubview:endpoint];[cardContent addArrangedSubview:details];if(!selected.announcement.pairing_required)[details addArrangedSubview:label(@"Pairing disabled ? Unverified connection",12)];[cardContent addArrangedSubview:button(@"Connect",^{ExtraPanePeerApp* strong=weak;if(!strong)return;[weak connectAddress:selected.observed_ipv4 port:selected.announcement.port expected:selected.announcement.device code:std::string{} open:!selected.announcement.pairing_required];})];[row addArrangedSubview:cardContent];[self.nearby addArrangedSubview:row];[row.widthAnchor constraintEqualToAnchor:self.nearby.widthAnchor].active=YES;[cardContent.widthAnchor constraintEqualToAnchor:row.widthAnchor constant:-24].active=YES;
    }
    if(!scanning&&!count&&!nearbySignature.empty())self.searchStatus.stringValue=@"No unpaired devices found. You can also connect by address.";
}

-(BOOL)hasIncomingUses{return std::any_of(resources.begin(),resources.end(),[](const auto& entry){return entry.second.context.incoming;});}
-(void)updateSharingControls{
    const bool incoming=[self hasIncomingUses];const bool locked=incoming||sharingStarting;self.bindAddress.enabled=!locked;self.listenPort.enabled=!locked;self.applyNetworkButton.enabled=!locked;self.stopSharingButton.hidden=!incoming;self.gateway.state=gatewayEnabled?NSControlStateValueOn:NSControlStateValueOff;
}
-(std::string)validatedBindAddress{
    const auto port=portValue(self.listenPort.stringValue);auto local=str(self.bindAddress.stringValue);std::string lower=local;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});if(lower=="localhost")local="127.0.0.1";
    bool badAddress=!dm::mac::ipv4(local.c_str());const bool loopback=local.starts_with("127.");if(!badAddress&&!loopback){const auto addresses=dm::mac::localAddresses();badAddress=std::none_of(addresses.begin(),addresses.end(),[&](const auto& entry){return entry.address==local;});}
    if(badAddress)invalidField(self.bindAddress);if(!port)invalidField(self.listenPort);if(badAddress||!port)throw std::invalid_argument("Check the highlighted sharing details.");return local;
}
-(void)beginSharing{
    if(sharing||sharingStarting)return;const auto local=[self validatedBindAddress];const auto port=portValue(self.listenPort.stringValue);
    sharingGeneration=service->start(local,static_cast<std::uint16_t>(port));sharingStarting=true;loopbackSharing=local.starts_with("127.");listeningAddress=local;self.bindAddress.stringValue=text(local);self.status.stringValue=@"Starting sharing...";[self updateSharingControls];
}
-(void)applyNetworkSettings{
    if(sharingStarting||[self hasIncomingUses]){[self error:@"Stop incoming resource connections before changing network settings."];return;}
    try{[self validatedBindAddress];if(sharing)[self stopListener];[self beginSharing];}catch(const std::exception& e){[self error:text(e.what())];}[self updateSharingControls];
}
-(void)stopListener{
    discovery.stop();sharingGeneration=0;sharing=sharingStarting=false;listeningPort=0;service->stop();[self updateSharingControls];
}
-(void)stopSharing{
    try{for(const auto& [key,event]:resources){(void)key;if(event.context.incoming)service->close(event.context.connection,event.context.grant);}self.status.stringValue=@"Stopping incoming resource connections...";self.status.textColor=NSColor.labelColor;}catch(const std::exception& e){[self error:text(e.what())];}
}
-(void)toggleOffer:(std::string)identifier{
    // A provider's control stops current uses, never changes resource availability.
    try{for(const auto& [key,event]:resources){(void)key;if(event.context.incoming&&event.resource&&event.resource->owner==service->identity()&&event.resource->resource==identifier)service->close(event.context.connection,event.context.grant);}}catch(const std::exception& e){[self error:text(e.what())];}
}
-(void)toggleGateway{
    const bool requested=self.gateway.state==NSControlStateValueOn;if(requested==gatewayEnabled)return;
    try{if(!sharing&&!sharingStarting)[self beginSharing];service->gateway(requested);gatewayEnabled=requested;[self publish];}catch(const std::exception& e){[self error:text(e.what())];}[self updateSharingControls];
}
-(void)refreshRoutes{
    NSString* selected=self.outboundRoute.selectedItem.representedObject?:@"";
    try{const auto addresses=dm::mac::localAddresses();[self.outboundRoute removeAllItems];[self.outboundRoute addItemWithTitle:@"Automatic route"];self.outboundRoute.lastItem.representedObject=@"";
        for(const auto& entry:addresses){[self.outboundRoute addItemWithTitle:text(entry.interface+" - "+entry.address)];self.outboundRoute.lastItem.representedObject=text(entry.address);}
        for(NSMenuItem* item in self.outboundRoute.itemArray)if([item.representedObject isEqual:selected]){[self.outboundRoute selectItem:item];break;}
    }catch(const std::exception& e){[self error:text(e.what())];}
}
-(void)menuNeedsUpdate:(NSMenu*)menu{if(menu==self.outboundRoute.menu)[self refreshRoutes];}
-(void)controlTextDidChange:(NSNotification*)notification{if(notification.object==self.address||notification.object==self.port){expected={};[self updateManualConnect];}}
-(void)error:(NSString*)message{self.status.stringValue=message?:@"";self.status.textColor=NSColor.systemYellowColor;}
-(void)connectAddress:(std::string)address port:(unsigned)port expected:(DeviceId)identity code:(std::string)code open:(BOOL)open{
    try{
        const auto saved=service->remembered();auto normalized=[](std::string value){if(value.size()==9){auto lower=value;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});if(lower=="localhost")return std::string("127.0.0.1");}return value;};
        const auto match=[&](const auto& item){return valid_id(identity)?item.id==identity:item.port_hint==port&&normalized(item.address_hint)==normalized(address);};
        const auto pendingRemoval=service->pending_forgets();if(std::any_of(pendingRemoval.begin(),pendingRemoval.end(),match)){[self error:@"Finish or clear the pending pairing removal before pairing again."];return;}
        const auto hinted=std::count_if(saved.begin(),saved.end(),match);
        if(hinted>1){invalidField(self.address);[self error:@"This address matches several paired devices. Use the device's current address."];return;}
        const bool remembered=hinted==1;
        if(remembered){identity=std::find_if(saved.begin(),saved.end(),match)->id;open=NO;}
        const bool badAddress=!dm::mac::ipv4(address.c_str()),badPort=!port||port>65535;
        if(badAddress)invalidField(self.address);if(badPort)invalidField(self.port);
        if(badAddress||badPort){[self error:badAddress&&badPort?@"Check the highlighted connection details.":badAddress?@"Enter an IPv4 address or localhost.":@"Enter a port between 1 and 65535."];return;}
        const auto local=str(self.outboundRoute.selectedItem.representedObject?:@"");
        auto validRoute=[&](){if(local.empty())return true;const auto connected=dm::mac::localAddresses();return std::any_of(connected.begin(),connected.end(),[&](const auto& item){return item.address==local;});};
        if(!validRoute()){invalidField(self.outboundRoute);[self error:@"Choose an available local network interface."];return;}
        if(!open&&!remembered){
            if(![self requestPairingCode:text(address) result:code])return;
        }
        if(!validRoute()){code.clear();[self error:@"The selected network interface is no longer available."];return;}
        const auto connection=service->connect({address,static_cast<std::uint16_t>(port),local,identity,code,bool(open)});code.clear();connectingLinks.insert(connection);destinations[connection]={normalizedAddress(address),port};[self updateManualConnect];self.status.stringValue=@"";self.status.textColor=NSColor.labelColor;
    }catch(const std::exception& e){[self error:text(e.what())];}
}
-(BOOL)requestPairingCode:(NSString*)target result:(std::string&)code{
    auto* alert=[NSAlert new];alert.messageText=[NSString stringWithFormat:@"Pair with %@",target];alert.informativeText=@"Enter the six-digit code shown on that device. A gateway cannot supply this code on its behalf.";
    auto* input=[NSSecureTextField textFieldWithString:@""];input.frame=NSMakeRect(0,0,280,28);input.placeholderString=@"Six-digit pairing code";alert.accessoryView=input;[alert addButtonWithTitle:@"Pair"];[alert addButtonWithTitle:@"Cancel"];[alert.window setInitialFirstResponder:input];
    for(;;){const auto response=[alert runModal];code=str(input.stringValue);input.stringValue=@"";if(response!=NSAlertFirstButtonReturn){code.clear();return NO;}if(code.size()==6&&std::all_of(code.begin(),code.end(),[](char c){return c>='0'&&c<='9';}))return YES;code.clear();alert.informativeText=@"Enter exactly six digits from the other device.";}
}
-(void)pairVia:(Service::Token)gateway owner:(DeviceId)owner name:(NSString*)name{
    try{
        if(!service||!links.contains(gateway)||!links.at(gateway).verified||links.at(gateway).context.incoming)throw std::runtime_error("The verified gateway is no longer available.");
        for(const auto& [token,event]:links)if(!event.context.incoming&&event.verified&&event.context.peer==owner){[self.pages selectTabViewItemWithIdentifier:@"Resources"];return;}
        for(const auto& [token,identity]:endpointPairing)if(identity==owner)return;
        const auto removed=service->pending_forgets();if(std::any_of(removed.begin(),removed.end(),[&](const auto& item){return item.id==owner;}))throw std::runtime_error("Finish or clear the pending pairing removal before pairing this device.");
        const auto saved=service->remembered();const bool remembered=std::any_of(saved.begin(),saved.end(),[&](const auto& item){return item.id==owner;});std::string code;
        if(!remembered&&![self requestPairingCode:name result:code])return;
        const auto token=service->connect_via(gateway,owner,code);code.clear();connectingLinks.insert(token);endpointPairing.emplace(token,owner);[self renderCatalogs];
    }catch(const std::exception& e){[self error:text(e.what())];}
}
-(void)refreshInventory{if(refreshing||terminating)return;refreshing=true;auto shared=runtime;__weak ExtraPanePeerApp* weak=self;dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{try{auto offers=shared->refresh();dispatch_async(dispatch_get_main_queue(),^{ExtraPanePeerApp* strong=weak;if(!strong||strong->terminating)return;strong->inventory=offers;strong->refreshing=false;strong->enabled.clear();for(const auto& offer:offers)if(offer.available)strong->enabled.insert(offer.id);[strong publish];[strong renderOffers];if(!strong->sharing&&!strong->sharingStarting)try{[strong beginSharing];}catch(const std::exception& e){[strong error:text(e.what())];}});}catch(const std::exception& e){auto message=text(e.what());dispatch_async(dispatch_get_main_queue(),^{ExtraPanePeerApp* strong=weak;if(!strong)return;if(weak){strong->refreshing=false;[weak error:message];}});}});}
-(void)publish{if(!service)return;try{std::vector<Offer> offers;for(const auto& offer:inventory)if(enabled.contains(offer.id))offers.push_back(offer);service->offers(offers);if(sharing&&!loopbackSharing){Announcement a;a.device=service->identity();a.instance=instance;a.sequence=++sequence;a.name=str(NSProcessInfo.processInfo.hostName);a.port=listeningPort;a.pairing_required=self.requirePairing.state==NSControlStateValueOn;for(const auto& offer:offers)if(offer.available)a.offered_kinds|=1u<<static_cast<unsigned>(offer.kind);discovery.advertise(a);}}catch(const std::exception& e){[self error:text(e.what())];}}
-(void)renderOffers{
    clear(self.offered);__weak ExtraPanePeerApp* weak=self;const auto remembered=service->remembered();NSMutableDictionary<NSNumber*,NSStackView*>* groups=[NSMutableDictionary dictionary];
    for(const auto& offer:inventory){
        const auto type=offer.kind==Kind::existing_screen?Kind::display_destination:offer.kind;NSNumber* groupKey=@(static_cast<unsigned>(type));NSStackView* group=groups[groupKey];if(!group){group=borderedStack();[group addArrangedSubview:label(type==Kind::display_destination?@"Monitors":type==Kind::camera?@"Cameras":type==Kind::microphone?@"Microphones":type==Kind::speaker?@"Speakers":@"USB",20)];groups[groupKey]=group;[self.offered addArrangedSubview:group];}
        auto identifier=offer.id;NSMutableArray<NSString*>* consumers=[NSMutableArray array];
        for(const auto& [key,event]:resources){(void)key;if(!event.context.incoming||!event.resource||event.resource->owner!=service->identity()||event.resource->resource!=identifier)continue;
            NSString* consumer=nil;const auto saved=std::find_if(remembered.begin(),remembered.end(),[&](const auto& device){return device.id==event.context.peer;});if(saved!=remembered.end()&&!saved->name.empty())consumer=text(saved->name);
            if(!consumer)consumer=text("Device "+hex(event.context.peer).substr(0,12));if(![consumers containsObject:consumer])[consumers addObject:consumer];
        }
        const bool active=consumers.count>0;auto* row=stack(YES);auto* details=stack();[details addArrangedSubview:label(text(offer.label))];[details addArrangedSubview:label(active?[@"In use by " stringByAppendingString:[consumers componentsJoinedByString:@", "]]:offer.available?@"Available":text(offer.unavailable_reason))];[row addArrangedSubview:details];
        auto* toggle=button(@"",^{[weak toggleOffer:identifier];},active);toggle.image=[NSImage imageWithSystemSymbolName:@"power" accessibilityDescription:active?@"Stop sharing":offer.available?@"Available":@"Unavailable"];toggle.contentTintColor=active?NSColor.systemRedColor:NSColor.secondaryLabelColor;toggle.toolTip=active?[@"Stop sharing " stringByAppendingString:text(offer.label)]:@"Connect to this resource from another device";toggle.enabled=active;[row addArrangedSubview:toggle];[group addArrangedSubview:row];
    }
    [self updateSharingControls];
}
-(void)renderDevices{
    clear(self.saved);clear(self.linkedDevices);if(!service)return;__weak ExtraPanePeerApp* weak=self;const auto remembered=service->remembered();
    for(const auto& saved:remembered){auto* row=stack(YES);row.edgeInsets=NSEdgeInsetsMake(12,12,12,12);row.wantsLayer=YES;row.layer.borderWidth=1;row.layer.borderColor=NSColor.separatorColor.CGColor;row.layer.cornerRadius=10;auto item=saved;[row addArrangedSubview:label(text(saved.name.empty()?hex(saved.id).substr(0,12):saved.name))];
        [row addArrangedSubview:button(@"Resources",^{[weak.pages selectTabViewItemWithIdentifier:@"Resources"];})];
        [row addArrangedSubview:button(@"Forget",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{strong->service->forget(item.id);std::erase_if(strong->monitorDrafts,[&](const auto& row){return row.second.owner==item.id;});}catch(const std::exception& e){[weak error:text(e.what())];}},YES)];[self.saved addArrangedSubview:row];
    }
    for(const auto& removed:service->pending_forgets()){auto* row=borderedStack();[row addArrangedSubview:label(text(removed.name.empty()?hex(removed.id).substr(0,12):removed.name))];[row addArrangedSubview:label(@"Removing pairing when the other device is reachable...")];const auto identity=removed.id;[row addArrangedSubview:button(@"Clear pending removal",^{ExtraPanePeerApp* strong=weak;if(!strong)return;auto* alert=[NSAlert new];alert.messageText=@"Clear pending removal?";alert.informativeText=@"Removal on the other device has not been confirmed. Forget this computer on that device manually before pairing again.";[alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Clear pending removal"];if([alert runModal]==NSAlertSecondButtonReturn)try{strong->service->dismiss_forget(identity);}catch(const std::exception& e){[weak error:text(e.what())];}},YES)];[self.saved addArrangedSubview:row];}
    std::map<DeviceId,std::pair<Service::Token,Service::Event>> managed;for(const auto& [id,event]:links){if(std::any_of(remembered.begin(),remembered.end(),[&](const auto& item){return item.id==event.context.peer;})||event.verified)continue;auto found=managed.find(event.context.peer);if(found==managed.end()||(found->second.second.context.incoming&&!event.context.incoming))managed[event.context.peer]={id,event};}
    if(!managed.empty())[self.linkedDevices addArrangedSubview:label(@"Unpaired devices",22)];
    for(const auto& [identity,entry]:managed){const auto connection=entry.first;const auto& event=entry.second;auto* row=stack(YES);row.edgeInsets=NSEdgeInsetsMake(12,12,12,12);row.wantsLayer=YES;row.layer.borderWidth=1;row.layer.borderColor=NSColor.separatorColor.CGColor;row.layer.cornerRadius=10;std::string name="Device "+hex(event.context.peer).substr(0,12);for(const auto& saved:remembered)if(saved.id==event.context.peer&&!saved.name.empty())name=saved.name;for(const auto& catalog:catalogs[connection])if(catalog.device==event.context.peer&&!catalog.name.empty())name=catalog.name;[row addArrangedSubview:label(text(name+" (identity not verified)"))];
        if(!event.context.incoming)[row addArrangedSubview:button(@"Resources",^{[weak.pages selectTabViewItemWithIdentifier:@"Resources"];})];[self.linkedDevices addArrangedSubview:row];
    }
    [self updateManualConnect];[self renderNearby];[self renderCatalogs];
}
-(void)renderCatalogs{
    clear(self.remoteResources);if(!service)return;__weak ExtraPanePeerApp* weak=self;
    for(const auto& [connection,event]:links){
        if(event.context.incoming)continue;const auto connectionId=connection;const auto collapseKey=std::make_pair(hex(event.context.peer),connectionId);NSString* deviceName=nil;
        for(const auto& catalog:catalogs[connection])if(catalog.device==event.context.peer&&!catalog.name.empty()){deviceName=text(catalog.name);break;}
        if(!deviceName)for(const auto& saved:service->remembered())if(saved.id==event.context.peer&&!saved.name.empty()){deviceName=text(saved.name);break;}
        if(!deviceName)deviceName=text("Device "+hex(event.context.peer).substr(0,12));
        auto* section=stack();section.edgeInsets=NSEdgeInsetsMake(12,12,12,12);section.wantsLayer=YES;section.layer.borderWidth=1;section.layer.borderColor=NSColor.separatorColor.CGColor;section.layer.cornerRadius=10;auto* header=stack(YES);auto* body=stack();body.hidden=collapsedDevices.contains(collapseKey);NSString* title=[deviceName stringByAppendingString:event.verified?@" (verified)":@" (unverified)"];
        auto* disclosure=button(title,nil);disclosure.image=[NSImage imageWithSystemSymbolName:body.hidden?@"chevron.right":@"chevron.down" accessibilityDescription:nil];disclosure.imagePosition=NSImageLeft;disclosure.alignment=NSTextAlignmentLeft;disclosure.accessibilityLabel=[title stringByAppendingString:body.hidden?@", collapsed":@", expanded"];
        [disclosure setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        __weak NSButton* weakDisclosure=disclosure;__weak NSStackView* weakBody=body;
        disclosure.actionBlock=^{ExtraPanePeerApp* strong=weak;if(!strong)return;const bool collapse=!strong->collapsedDevices.contains(collapseKey);if(collapse)strong->collapsedDevices.insert(collapseKey);else strong->collapsedDevices.erase(collapseKey);weakBody.hidden=collapse;weakDisclosure.image=[NSImage imageWithSystemSymbolName:collapse?@"chevron.right":@"chevron.down" accessibilityDescription:nil];weakDisclosure.accessibilityLabel=[title stringByAppendingString:collapse?@", collapsed":@", expanded"];};
        [header addArrangedSubview:disclosure];[section addArrangedSubview:header];[section addArrangedSubview:body];[self.remoteResources addArrangedSubview:section];
        [section.widthAnchor constraintEqualToAnchor:self.remoteResources.widthAnchor].active=YES;[header.widthAnchor constraintEqualToAnchor:section.widthAnchor constant:-24].active=YES;
        for(const auto& catalog:catalogs[connection]){
            if(catalog.device!=event.context.peer){
                auto* remote=borderedStack();[remote addArrangedSubview:label(text(catalog.name.empty()?hex(catalog.device).substr(0,12):catalog.name),18)];
                [remote addArrangedSubview:label(@"Reached through this gateway. Pair directly with this device; both devices can share resources.")];
                const auto owner=catalog.device;const auto saved=service->remembered();const bool known=std::any_of(saved.begin(),saved.end(),[&](const auto& item){return item.id==owner;});const bool pending=std::any_of(endpointPairing.begin(),endpointPairing.end(),[&](const auto& item){return item.second==owner;});
                const bool ready=std::any_of(links.begin(),links.end(),[&](const auto& item){return !item.second.context.incoming&&item.second.verified&&item.second.context.peer==owner;});
                if(ready)[remote addArrangedSubview:label(@"Use this device's resource section below.")];
                else{NSString* target=text(catalog.name.empty()?hex(owner).substr(0,12):catalog.name);auto* pair=button(pending?@"Verifying device...":known?@"View resources":@"Pair device",^{[weak pairVia:connectionId owner:owner name:target];});pair.enabled=event.verified&&!pending;[remote addArrangedSubview:pair];}
                [body addArrangedSubview:remote];continue;
            }
            [body addArrangedSubview:label(text(catalog.name),16)];std::vector<Offer> monitors;for(const auto& offer:catalog.offers)if(offer.kind==Kind::display_destination||offer.kind==Kind::existing_screen)monitors.push_back(offer);
            std::stable_sort(monitors.begin(),monitors.end(),[](const auto& left,const auto& right){auto rank=[](const auto& item){return item.kind==Kind::existing_screen?2:(((!item.width&&!item.height)||item.label.find("Custom")!=std::string::npos)?0:1);};return rank(left)<rank(right);});
            if(!monitors.empty()){
                const auto owner=catalog.device;for(auto& [rowId,draft]:monitorDrafts)if(draft.owner==owner&&!links.contains(draft.connection))draft.connection=connectionId;const bool hasRows=std::any_of(monitorDrafts.begin(),monitorDrafts.end(),[&](const auto& row){return row.second.connection==connectionId&&row.second.owner==owner;});
                if(!hasRows){MonitorDraft draft;draft.connection=connectionId;draft.owner=owner;draft.resource=monitors.front().id;draft.kind=monitors.front().kind;draft.input=monitors.front().input_allowed&&draft.kind==Kind::display_destination;draft.width=std::to_string(monitors.front().width?monitors.front().width:1920);draft.height=std::to_string(monitors.front().height?monitors.front().height:1080);draft.refresh=std::to_string(std::min(monitors.front().refresh?monitors.front().refresh:60u,360u));monitorDrafts[++monitorSequence]=draft;}
                auto* group=borderedStack();auto* heading=stack(YES);[heading addArrangedSubview:label(@"Monitors",20)];const auto defaultOffer=monitors.front();[heading addArrangedSubview:button(@"+ Add monitor",^{ExtraPanePeerApp* strong=weak;if(!strong)return;MonitorDraft draft;draft.connection=connectionId;draft.owner=owner;draft.resource=defaultOffer.id;draft.kind=defaultOffer.kind;draft.input=defaultOffer.input_allowed&&draft.kind==Kind::display_destination;draft.width=std::to_string(defaultOffer.width?defaultOffer.width:1920);draft.height=std::to_string(defaultOffer.height?defaultOffer.height:1080);draft.refresh=std::to_string(std::min(defaultOffer.refresh?defaultOffer.refresh:60u,360u));strong->monitorDrafts[++strong->monitorSequence]=draft;[weak renderCatalogs];})];[group addArrangedSubview:heading];
                for(const auto& [rowId,draft]:monitorDrafts)if(draft.connection==connectionId&&draft.owner==owner){auto found=std::find_if(monitors.begin(),monitors.end(),[&](const auto& offer){return offer.id==draft.resource;});Offer selected;if(found!=monitors.end())selected=*found;else{selected.id=draft.resource;selected.kind=draft.kind;selected.label="Unavailable monitor";selected.available=false;selected.unavailable_reason="This monitor is no longer available";}[group addArrangedSubview:[self resourceCard:selected owner:owner connection:connectionId row:rowId choices:monitors]];}[body addArrangedSubview:group];
            }
            for(auto type:{Kind::camera,Kind::speaker,Kind::microphone,Kind::usb}){auto* group=borderedStack();[group addArrangedSubview:label(type==Kind::camera?@"Cameras":type==Kind::speaker?@"Speakers":type==Kind::microphone?@"Microphones":@"USB",20)];bool found=false;for(const auto& offer:catalog.offers)if(offer.kind==type){found=true;[group addArrangedSubview:[self resourceCard:offer owner:catalog.device connection:connectionId row:0 choices:std::vector<Offer>{}]];}if(found)[body addArrangedSubview:group];}
        }
    }
}
-(NSStackView*)resourceCard:(Offer)item owner:(DeviceId)owner connection:(Service::Token)connectionId row:(Service::Token)rowId choices:(std::vector<Offer>)choices{
    __weak ExtraPanePeerApp* weak=self;auto* card=stack();card.edgeInsets=NSEdgeInsetsMake(8,0,8,0);const auto unavailable=dm::mac::peer::MacPeerRuntime::receiveUnavailable(item.kind);
    const auto matchesGrant=[&](const auto& entry){const auto& event=entry.second;if(event.context.incoming||event.context.connection!=connectionId||!event.resource||event.resource->owner!=owner||event.resource->resource!=item.id)return false;auto row=resourceRows.find(entry.first);return !rowId||(row!=resourceRows.end()&&row->second==rowId);};
    const auto matchesPending=[&](const auto& entry){return entry.first.first==connectionId&&entry.second.resource.owner==owner&&entry.second.resource.resource==item.id&&(!rowId||entry.second.monitorRow==rowId);};
    const bool active=std::any_of(resources.begin(),resources.end(),matchesGrant)||std::any_of(pending.begin(),pending.end(),matchesPending);
    if(rowId){
        auto* selectors=stack(YES);auto* mode=[[ExtraPanePeerPopup alloc] initWithFrame:NSZeroRect pullsDown:NO];[mode addItemsWithTitles:@[@"Virtual monitor",@"View existing screen"]];[mode selectItemAtIndex:item.kind==Kind::existing_screen?1:0];mode.enabled=!active;mode.target=mode;mode.action=@selector(changed:);
        mode.selectionBlock=^(NSInteger index){ExtraPanePeerApp* strong=weak;if(!strong||!strong->monitorDrafts.contains(rowId))return;const auto type=index==1?Kind::existing_screen:Kind::display_destination;auto found=std::find_if(choices.begin(),choices.end(),[&](const auto& offer){return offer.kind==type;});if(found==choices.end()){[weak error:@"This device has no monitors in that mode."];[weak renderCatalogs];return;}auto& draft=strong->monitorDrafts.at(rowId);draft.resource=found->id;draft.kind=found->kind;draft.input=found->input_allowed&&type==Kind::display_destination;draft.width=std::to_string(found->width?found->width:1920);draft.height=std::to_string(found->height?found->height:1080);draft.refresh=std::to_string(std::min(found->refresh?found->refresh:60u,360u));[weak renderCatalogs];};[selectors addArrangedSubview:mode];
        auto* destination=[[ExtraPanePeerPopup alloc] initWithFrame:NSZeroRect pullsDown:NO];std::vector<Offer> candidates;unsigned number=0;for(const auto& offer:choices)if(offer.kind==item.kind){candidates.push_back(offer);NSString* title;if(offer.kind==Kind::display_destination&&((!offer.width&&!offer.height)||offer.label.find("Custom")!=std::string::npos))title=@"Custom";else title=[NSString stringWithFormat:@"Monitor %u - %@",++number,text(offer.label)];[destination addItemWithTitle:title];if(offer.id==item.id)[destination selectItemAtIndex:candidates.size()-1];}destination.enabled=!active;destination.target=destination;destination.action=@selector(changed:);destination.selectionBlock=^(NSInteger index){ExtraPanePeerApp* strong=weak;if(!strong||!strong->monitorDrafts.contains(rowId)||index<0||static_cast<std::size_t>(index)>=candidates.size())return;const auto selected=candidates[index];auto& draft=strong->monitorDrafts.at(rowId);draft.resource=selected.id;draft.kind=selected.kind;draft.input=selected.input_allowed&&selected.kind==Kind::display_destination;draft.width=std::to_string(selected.width?selected.width:1920);draft.height=std::to_string(selected.height?selected.height:1080);draft.refresh=std::to_string(std::min(selected.refresh?selected.refresh:60u,360u));[weak renderCatalogs];};[selectors addArrangedSubview:destination];[card addArrangedSubview:selectors];
    }else [card addArrangedSubview:label(text(item.label))];
    auto makeField=[weak,rowId,active](NSString* title,const std::string& value,unsigned field){auto* box=[ExtraPanePeerField new];box.stringValue=text(value);box.placeholderString=title;box.delegate=box;box.enabled=!active;[box.widthAnchor constraintGreaterThanOrEqualToConstant:72].active=YES;box.editBlock=^(NSString* edited){ExtraPanePeerApp* strong=weak;if(!strong||!strong->monitorDrafts.contains(rowId))return;auto& draft=strong->monitorDrafts.at(rowId);if(field==0)draft.width=str(edited);else if(field==1)draft.height=str(edited);else draft.refresh=str(edited);};return box;};
    MonitorDraft draft;if(rowId)draft=monitorDrafts.at(rowId);auto* width=makeField(@"Width",draft.width,0);auto* height=makeField(@"Height",draft.height,1);auto* fps=makeField(@"Hz / FPS",draft.refresh,2);
    if(item.kind==Kind::display_destination){auto* dimensions=stack(YES);[dimensions addArrangedSubview:width];[dimensions addArrangedSubview:height];[dimensions addArrangedSubview:fps];[card addArrangedSubview:dimensions];}
    auto* actions=stack(YES);auto* control=button(@"Allow input",nil);[control setButtonType:NSButtonTypeSwitch];control.hidden=!item.input_allowed;control.enabled=!active;control.state=(rowId?draft.input:item.input_allowed&&item.kind==Kind::display_destination)?NSControlStateValueOn:NSControlStateValueOff;__weak NSButton* weakControl=control;control.actionBlock=^{ExtraPanePeerApp* strong=weak;if(strong&&rowId&&strong->monitorDrafts.contains(rowId))strong->monitorDrafts.at(rowId).input=weakControl.state==NSControlStateValueOn;};[actions addArrangedSubview:control];
    auto* use=button(@"",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{
        std::vector<std::pair<Service::Token,Service::Token>> grants,requests;
        for(const auto& [key,event]:strong->resources)if(!event.context.incoming&&event.context.connection==connectionId&&event.resource&&event.resource->owner==owner&&event.resource->resource==item.id&&(!rowId||(strong->resourceRows.contains(key)&&strong->resourceRows.at(key)==rowId)))grants.push_back(key);
        for(const auto& [key,entry]:strong->pending)if(key.first==connectionId&&entry.resource.owner==owner&&entry.resource.resource==item.id&&(!rowId||entry.monitorRow==rowId))requests.push_back(key);
        if(!grants.empty()||!requests.empty()){for(const auto& key:grants){strong->service->close(key.first,key.second);if(strong->retrying.erase(key)){strong->resources.erase(key);strong->resourceRows.erase(key);}}for(const auto& key:requests){strong->service->cancel(key.first,key.second);strong->pending.erase(key);}[weak renderConnections];[weak renderCatalogs];return;}
        rpc::Open request{owner,item.id,bool(item.input_allowed&&control.state==NSControlStateValueOn)};if(item.kind==Kind::display_destination){request.width=portValue(width.stringValue);request.height=portValue(height.stringValue);request.refresh=portValue(fps.stringValue);const bool badWidth=request.width<640||request.width>4096||request.width%2,badHeight=request.height<480||request.height>4096||request.height%2,badRate=!request.refresh||request.refresh>360,badArea=std::uint64_t(request.width)*request.height>8847360;if(badWidth||badArea)invalidField(width);if(badHeight||badArea)invalidField(height);if(badRate)invalidField(fps);if(badWidth||badHeight||badRate||badArea){[weak error:@"Check the highlighted display mode."];return;}}
        const auto token=strong->service->open(connectionId,request);strong->pending[{connectionId,token}]={item.kind,request,rowId};[weak renderConnections];[weak renderCatalogs];
    }catch(const std::exception& e){[weak error:text(e.what())];}},active);
    use.image=[NSImage imageWithSystemSymbolName:@"power" accessibilityDescription:active?@"Disconnect":@"Connect"];use.contentTintColor=active?NSColor.systemRedColor:NSColor.systemGreenColor;use.enabled=active||(item.available&&unavailable.empty());[actions addArrangedSubview:use];
    if(rowId){auto* remove=button(@"Remove",^{ExtraPanePeerApp* strong=weak;if(strong){strong->monitorDrafts.erase(rowId);[weak renderCatalogs];}});remove.enabled=!active;[actions addArrangedSubview:remove];}[card addArrangedSubview:actions];if(!item.available||!unavailable.empty())[card addArrangedSubview:label(text(unavailable.empty()?item.unavailable_reason:unavailable))];return card;
}

-(void)renderConnections{clear(self.connectedResources);self.disconnectButton.hidden=resources.empty()&&pending.empty();__weak ExtraPanePeerApp* weak=self;for(const auto& [id,event]:resources){(void)id;auto c=event.context;auto* row=stack(YES);[row addArrangedSubview:label(retrying.contains(id)?[kind(c.kind) stringByAppendingString:@" - Reconnecting..."]:kind(c.kind))];[row addArrangedSubview:button(retrying.contains(id)?@"Cancel retry":@"Disconnect",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{strong->service->close(c.connection,c.grant);if(strong->retrying.erase({c.connection,c.grant})){strong->resources.erase({c.connection,c.grant});strong->resourceRows.erase({c.connection,c.grant});[weak renderConnections];[weak renderDevices];}}catch(const std::exception& e){[weak error:text(e.what())];}},YES)];[self.connectedResources addArrangedSubview:row];}for(const auto& [key,type]:pending){const auto connection=key.first,request=key.second;auto* row=stack(YES);[row addArrangedSubview:label([kind(type.kind) stringByAppendingString:@" - Connecting..."])];[row addArrangedSubview:button(@"Cancel",^{ExtraPanePeerApp* strong=weak;if(!strong)return;try{strong->service->cancel(connection,request);strong->pending.erase({connection,request});[weak renderConnections];[weak renderDevices];}catch(const std::exception& e){[weak error:text(e.what())];}},YES)];[self.connectedResources addArrangedSubview:row];}if(resources.empty()&&pending.empty())[self.connectedResources addArrangedSubview:label(@"No resources connected")];}
-(void)poll{
    if(!service||terminating)return;bool devicesChanged=false,connectionsChanged=false;
    for(auto& event:service->poll()){auto id=event.context.connection;switch(event.kind){
        case Service::EventKind::listening:if(!sharingGeneration||event.sharing_generation!=sharingGeneration)break;instance=std::chrono::steady_clock::now().time_since_epoch().count()|1;sequence=0;sharingStarting=false;sharing=true;listeningPort=event.port;self.status.stringValue=@"Sharing enabled";self.status.textColor=NSColor.labelColor;[self updateSharingControls];try{if(loopbackSharing)discovery.stop();else discovery.start(listeningAddress);[self publish];}catch(const std::exception& e){[self error:text(e.what())];}break;
        case Service::EventKind::connected:endpointPairing.erase(id);connectingLinks.erase(id);links[id]=event;if(!event.context.incoming)service->catalog(id);devicesChanged=true;break;
        case Service::EventKind::catalog:catalogs[id]=event.catalogs;devicesChanged=true;break;
        case Service::EventKind::granted:{Service::Token row=0;auto requested=pending.find({id,event.context.request});if(requested!=pending.end())row=requested->second.monitorRow;pending.erase({id,event.context.request});if(event.replaces_grant){const auto old=std::make_pair(id,event.replaces_grant);if(resourceRows.contains(old))row=resourceRows.at(old);resourceRows.erase(old);retrying.erase(old);resources.erase(old);}const auto key=std::make_pair(id,event.context.grant);resources[key]=event;if(row)resourceRows[key]=row;connectionsChanged=true;self.status.stringValue=@"Resource connected";break;}
        case Service::EventKind::reconnecting:std::erase_if(pending,[&](const auto& entry){return entry.first.first==id;});for(const auto& [key,item]:resources)if(key.first==id&&!item.context.incoming&&item.context.kind!=Kind::usb)retrying.insert(key);catalogs.erase(id);self.status.stringValue=text(event.detail);devicesChanged=connectionsChanged=true;break;
        case Service::EventKind::stopped:{if(event.context.request&&!event.context.grant)pending.erase({id,event.context.request});const auto key=std::make_pair(id,event.context.grant);if(!retrying.contains(key)||!event.detail.empty()){retrying.erase(key);resources.erase(key);resourceRows.erase(key);}connectionsChanged=true;break;}
        case Service::EventKind::disconnected:endpointPairing.erase(id);connectingLinks.erase(id);destinations.erase(id);std::erase_if(pending,[&](const auto& entry){return entry.first.first==id;});std::erase_if(retrying,[&](const auto& key){return key.first==id;});links.erase(id);catalogs.erase(id);std::erase_if(resourceRows,[&](const auto& entry){return entry.first.first==id;});std::erase_if(collapsedDevices,[&](const auto& key){return key.second==id;});for(auto i=resources.begin();i!=resources.end();)if(i->second.context.connection==id)i=resources.erase(i);else ++i;devicesChanged=connectionsChanged=true;break;
        case Service::EventKind::error:if(endpointPairing.erase(id))devicesChanged=true;connectingLinks.erase(id);if(!links.contains(id))destinations.erase(id);[self updateManualConnect];if(event.sharing_generation){if(event.sharing_generation!=sharingGeneration)break;[self stopListener];}if(pending.erase({id,event.context.request}))connectionsChanged=true;[self error:text(event.detail)];break;
        case Service::EventKind::trust_changed:observedRoutes.clear();devicesChanged=true;break;
    }}
    if(devicesChanged||connectionsChanged)[self renderDevices];if(connectionsChanged)[self renderConnections];if(devicesChanged||connectionsChanged)[self renderOffers];
    if(sharing)[self observeRoutes:discovery.poll()]; // Maintains advertisements/known routes; never populates Nearby.
    if(scanning){auto routes=searchDiscovery.poll();[self observeRoutes:routes];std::vector<std::string> signature;for(const auto& route:routes)signature.push_back(hex(route.announcement.device)+route.observed_ipv4+std::to_string(route.announcement.port)+route.announcement.name);if(signature!=nearbySignature){nearbySignature=std::move(signature);nearbyRoutes=std::move(routes);[self renderNearby];}if(std::chrono::steady_clock::now()>=searchUntil){scanning=false;searchDiscovery.stop();[self updateSearchAppearance];self.searchStatus.stringValue=@"Search complete. Search again to refresh nearby devices.";[self renderNearby];}}

}
-(void)showWindow:(id)sender{(void)sender;[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];}
-(void)quit:(id)sender{(void)sender;[NSApp terminate:nil];}
-(BOOL)windowShouldClose:(NSWindow*)window{if(sharingStarting||!connectingLinks.empty()||!resources.empty()||!pending.empty()){[window orderOut:nil];return NO;}[NSApp terminate:nil];return NO;}
-(void)applicationWillTerminate:(NSNotification*)notification{(void)notification;terminating=true;[self.timer invalidate];discovery.stop();searchDiscovery.stop();service.reset();if(runtime)runtime->shutdown();if(self.tray)[[NSStatusBar systemStatusBar] removeStatusItem:self.tray];}
@end
int runPeerWorkspace(){[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];ExtraPanePeerApp* app=[ExtraPanePeerApp new];NSApp.delegate=app;[NSApp run];return 0;}
