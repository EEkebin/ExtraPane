#import <AppKit/AppKit.h>
#include "transport.hpp"
#include "media.hpp"
#include "host.hpp"
#include "network.hpp"
#include "display.hpp"
#include "resource_ui.hpp"
#import <ApplicationServices/ApplicationServices.h>
#include <dm/video_protocol.hpp>
#include <dm/input_protocol.hpp>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <stdexcept>
#include <cstring>
#include <signal.h>
using namespace std::chrono_literals;
namespace wire=dm::video::wire;
namespace input=dm::input;
#include "keys.hpp"
struct InputQueue {
    std::mutex mutex;std::vector<input::Event> events;unsigned caps=0;bool overflow=false;
    void grant(unsigned value){std::lock_guard lock(mutex);caps=value;events.clear();overflow=false;}
    void reset(){std::lock_guard lock(mutex);events.clear();if(caps)events.push_back({});}
    void add(input::Event e){std::lock_guard lock(mutex);if(!(caps&input::capability(e.kind)))return;
        if(e.kind==input::Kind::mouse && e.action==input::Action::move && !events.empty() && events.back().kind==e.kind && events.back().action==e.action)events.pop_back();
        if(events.size()>=input::max_events){overflow=true;return;}events.push_back(e);
    }
    std::vector<uint8_t> take(uint64_t epoch,unsigned seq){std::lock_guard lock(mutex);if(overflow)throw std::runtime_error("Input queue overflow; disconnected to release input");
        auto h=input::encode_header({epoch,seq,unsigned(events.size())});auto p=input::encode_events(events);events.clear();std::vector<uint8_t> result(h.begin(),h.end());result.insert(result.end(),p.begin(),p.end());return result;}
};
struct Session {std::atomic_bool stopped=false;std::mutex mutex;std::shared_ptr<dm::mac::Transport> transport;InputQueue input;void setTransport(std::shared_ptr<dm::mac::Transport> value){std::lock_guard lock(mutex);transport=std::move(value);if(stopped)transport->cancel();}void cancel(){stopped=true;{std::lock_guard lock(mutex);if(transport)transport->cancel();}input.grant(0);}};
@class App;
@interface VideoView:NSView
@property(nonatomic,weak) App* owner;
@property(nonatomic,strong) AVSampleBufferDisplayLayer* display;
@property(nonatomic) NSSize sourceSize;
@property(nonatomic) BOOL remoteEnabled;
@end
@interface App:NSObject<NSApplicationDelegate,NSWindowDelegate> {
@public std::shared_ptr<Session> session;
    std::unique_ptr<dm::mac::Host> host;
    std::shared_ptr<dm::access::Gate> accessGate;
    std::set<unsigned> heldKeys,heldButtons;
}
@property(nonatomic,strong) NSWindow* window;
@property(nonatomic,strong) VideoView* video;
@property(nonatomic,strong) NSStackView* controls;
@property(nonatomic,strong) NSTextField* address;
@property(nonatomic,strong) NSTextField* port;
@property(nonatomic,strong) NSSecureTextField* accessCode;
@property(nonatomic,strong) NSButton* requireCode;
@property(nonatomic,strong) NSTextField* generatedCode;
@property(nonatomic,strong) NSTextField* status;
@property(nonatomic,strong) NSTextField* statistics;
@property(nonatomic,strong) NSPopUpButton* routes;
@property(nonatomic,strong) NSButton* connectButton;
@property(nonatomic,strong) NSButton* disconnectAllButton;
@property(nonatomic,strong) NSButton* serverButton;
@property(nonatomic,strong) NSTextField* serverStatus;
@property(nonatomic,strong) NSStatusItem* tray;
@property(nonatomic,strong) EPResourcePanel* resources;
@property(nonatomic) BOOL quitting;
@property(nonatomic,strong) dispatch_source_t quitSignal;
@property(nonatomic) BOOL paused;
@property(nonatomic) BOOL showStats;
- (void)resetInput;
- (void)showControls;
- (void)updateActive;
- (void)disconnect:(id)sender;
- (void)disconnectAll:(id)sender;
- (void)displayPower:(id)sender;
- (void)key:(NSEvent*)event down:(BOOL)down;
- (void)mouse:(NSEvent*)event action:(input::Action)action;
@end
@implementation VideoView
- (instancetype)initWithFrame:(NSRect)frame {if((self=[super initWithFrame:frame])){self.wantsLayer=YES;self.layer.backgroundColor=NSColor.blackColor.CGColor;self.display=[AVSampleBufferDisplayLayer layer];self.display.videoGravity=AVLayerVideoGravityResizeAspect;[self.layer addSublayer:self.display];self.sourceSize=NSMakeSize(16,9);}return self;}
- (BOOL)isFlipped{return YES;}
- (BOOL)acceptsFirstResponder{return YES;}
- (void)layout{[super layout];[CATransaction begin];[CATransaction setDisableActions:YES];self.display.frame=self.bounds;[CATransaction commit];}
- (void)updateTrackingAreas{[super updateTrackingAreas];for(NSTrackingArea* a in self.trackingAreas)[self removeTrackingArea:a];[self addTrackingArea:[[NSTrackingArea alloc]initWithRect:NSZeroRect options:NSTrackingMouseMoved|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil]];}
- (void)mouseDown:(NSEvent*)e{[self.window makeFirstResponder:self];[self.owner mouse:e action:input::Action::down];}
- (void)mouseUp:(NSEvent*)e{[self.owner mouse:e action:input::Action::up];}
- (void)rightMouseDown:(NSEvent*)e{[self mouseDown:e];}
- (void)rightMouseUp:(NSEvent*)e{[self mouseUp:e];}
- (void)otherMouseDown:(NSEvent*)e{[self mouseDown:e];}
- (void)otherMouseUp:(NSEvent*)e{[self mouseUp:e];}
- (void)mouseMoved:(NSEvent*)e{[self.owner mouse:e action:input::Action::move];}
- (void)mouseDragged:(NSEvent*)e{[self mouseMoved:e];}
- (void)rightMouseDragged:(NSEvent*)e{[self mouseMoved:e];}
- (void)otherMouseDragged:(NSEvent*)e{[self mouseMoved:e];}
- (void)scrollWheel:(NSEvent*)e{[self.owner mouse:e action:input::Action::wheel];}
- (void)keyDown:(NSEvent*)e{[self.owner key:e down:YES];}
- (void)keyUp:(NSEvent*)e{[self.owner key:e down:NO];}
- (void)flagsChanged:(NSEvent*)e{
    if(e.keyCode==57){[self.owner key:e down:YES];[self.owner key:e down:NO];return;}
    // Device-specific left/right bits retain both modifiers held together.
    unsigned mask=0;switch(e.keyCode){case 56:mask=2;break;case 60:mask=4;break;case 59:mask=1;break;case 62:mask=0x2000;break;case 58:mask=0x20;break;case 61:mask=0x40;break;case 55:mask=8;break;case 54:mask=0x10;break;default:return;}
    [self.owner key:e down:(e.modifierFlags&mask)!=0];
}
@end
static NSTextField* label(NSString* value){return [NSTextField wrappingLabelWithString:value];}
static NSButton* button(NSString* title,id target,SEL action){return [NSButton buttonWithTitle:title target:target action:action];}
@implementation App
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;self.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,1100,800) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];self.window.title=@"ExtraPane";self.window.delegate=self;self.window.minSize=NSMakeSize(800,680);self.window.collectionBehavior=NSWindowCollectionBehaviorFullScreenPrimary;
    NSStackView* root=[NSStackView stackViewWithViews:@[]];root.orientation=NSUserInterfaceLayoutOrientationVertical;root.spacing=10;root.edgeInsets=NSEdgeInsetsMake(12,12,12,12);self.window.contentView=root;
    self.address=[[NSTextField alloc]initWithFrame:NSZeroRect];self.address.placeholderString=@"Server IPv4 address";self.address.stringValue=[[NSUserDefaults standardUserDefaults]stringForKey:@"server"]?:@"";[self.address.widthAnchor constraintGreaterThanOrEqualToConstant:180].active=YES;
    self.port=[[NSTextField alloc]initWithFrame:NSZeroRect];self.port.stringValue=@"43824";self.port.placeholderString=@"TCP port";[self.port.widthAnchor constraintEqualToConstant:75].active=YES;
    self.accessCode=[[NSSecureTextField alloc]initWithFrame:NSZeroRect];self.accessCode.placeholderString=@"Access code (optional)";[self.accessCode.widthAnchor constraintGreaterThanOrEqualToConstant:150].active=YES;
    self.routes=[[NSPopUpButton alloc]initWithFrame:NSZeroRect pullsDown:NO];[self refresh:nil];
    self.connectButton=button(@"Connect display",self,@selector(displayPower:));self.connectButton.image=[NSImage imageWithSystemSymbolName:@"power" accessibilityDescription:nil];self.connectButton.imagePosition=NSImageLeft;self.connectButton.bezelColor=NSColor.systemGreenColor;self.connectButton.contentTintColor=NSColor.whiteColor;self.connectButton.accessibilityLabel=@"Connect display";self.connectButton.toolTip=@"Connect display";
    NSStackView* destinationRow=[NSStackView stackViewWithViews:@[self.address,self.port,self.accessCode,self.routes,button(@"Refresh",self,@selector(refresh:))]];destinationRow.spacing=8;
    self.disconnectAllButton=button(@"Disconnect all",self,@selector(disconnectAll:));self.disconnectAllButton.contentTintColor=NSColor.systemRedColor;self.disconnectAllButton.hidden=YES;
    NSStackView* actionRow=[NSStackView stackViewWithViews:@[self.connectButton,self.disconnectAllButton,button(@"Settings",self,@selector(settings:)),button(@"About",self,@selector(about:))]];actionRow.spacing=8;
    self.controls=[NSStackView stackViewWithViews:@[destinationRow,actionRow]];self.controls.orientation=NSUserInterfaceLayoutOrientationVertical;self.controls.alignment=NSLayoutAttributeLeading;self.controls.spacing=6;
    [root addArrangedSubview:self.controls];self.serverButton=button(@"Start Server",self,@selector(toggleServer:));self.serverStatus=label(@"Server stopped. Automatic listens on connected Ethernet/Wi-Fi addresses.");NSStackView* serverRow=[NSStackView stackViewWithViews:@[self.serverButton,self.serverStatus]];serverRow.spacing=12;[root addArrangedSubview:serverRow];[NSTimer scheduledTimerWithTimeInterval:0.5 target:self selector:@selector(refreshAccessStatus:) userInfo:nil repeats:YES];self.requireCode=[NSButton checkboxWithTitle:@"Require access code" target:nil action:nil];self.generatedCode=label(@"One use, expires in 5 minutes");NSStackView* gateRow=[NSStackView stackViewWithViews:@[self.requireCode,self.generatedCode,button(@"Generate new code",self,@selector(generateCode:))]];gateRow.spacing=12;[root addArrangedSubview:gateRow];self.status=label(@"Client ready");[root addArrangedSubview:self.status];
    self.video=[[VideoView alloc]initWithFrame:NSMakeRect(0,0,960,550)];self.video.owner=self;[root addArrangedSubview:self.video];[self.video.widthAnchor constraintEqualToAnchor:root.widthAnchor constant:-24].active=YES;[self.video.heightAnchor constraintGreaterThanOrEqualToConstant:240].active=YES;
    __weak App* resourceOwner=self;
    self.resources=[[EPResourcePanel alloc]initWithTarget:[resourceOwner]{App* app=resourceOwner;dm::mac::ResourceOptions options;if(!app)return options;options.target.address=app.address.stringValue.UTF8String;options.target.local=[(NSString*)app.routes.selectedItem.representedObject UTF8String]?:"";try{options.target.port=[app selectedPort];}catch(...){options.target.port=0;}options.code=app.accessCode.stringValue.UTF8String;return options;} changed:^{App* app=resourceOwner;if(app)[app updateActive];}];
    self.resources.highlightInvalidTarget=^{App* app=resourceOwner;if(!app)return;auto mark=[](NSView* field){field.wantsLayer=YES;field.layer.borderWidth=2;field.layer.borderColor=NSColor.systemRedColor.CGColor;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),^{field.layer.borderWidth=0;});};if(!dm::mac::ipv4(app.address.stringValue.UTF8String))mark(app.address);try{[app selectedPort];}catch(...){mark(app.port);}if(app.accessCode.stringValue.length&&!dm::access::validCode(app.accessCode.stringValue.UTF8String))mark(app.accessCode);};
    [root insertArrangedSubview:self.resources.view atIndex:4];[self.resources.view.widthAnchor constraintEqualToAnchor:root.widthAnchor constant:-24].active=YES;
    self.statistics=label(@"");self.statistics.hidden=YES;[root addArrangedSubview:self.statistics];[root addArrangedSubview:label(@"F10 controls · Control+Command+F fullscreen · F8 pause input · Ctrl+Shift+Q disconnect")];
    NSMenu* menu=[NSMenu new];NSMenuItem* appItem=[NSMenuItem new];[menu addItem:appItem];NSMenu* appMenu=[NSMenu new];[appMenu addItemWithTitle:@"Quit ExtraPane" action:@selector(terminate:) keyEquivalent:@"q"];appItem.submenu=appMenu;
    NSMenuItem* viewItem=[[NSMenuItem alloc]initWithTitle:@"View" action:nil keyEquivalent:@""];[menu addItem:viewItem];NSMenu* viewMenu=[[NSMenu alloc]initWithTitle:@"View"];viewItem.submenu=viewMenu;
    NSMenuItem* disconnectAll=[viewMenu addItemWithTitle:@"Disconnect all clients" action:@selector(disconnectAll:) keyEquivalent:@"q"];disconnectAll.target=self;disconnectAll.keyEquivalentModifierMask=NSEventModifierFlagControl|NSEventModifierFlagShift;
    NSMenuItem* fullscreen=[viewMenu addItemWithTitle:@"Toggle Full Screen" action:@selector(fullscreen:) keyEquivalent:@"f"];fullscreen.target=self;fullscreen.keyEquivalentModifierMask=NSEventModifierFlagControl|NSEventModifierFlagCommand;
    NSMenuItem* controls=[viewMenu addItemWithTitle:@"Show Controls" action:@selector(showControls) keyEquivalent:@""];controls.target=self;NSApp.mainMenu=menu;
    [self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    signal(SIGTERM,SIG_IGN);self.quitSignal=dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,SIGTERM,0,dispatch_get_main_queue());dispatch_source_set_event_handler(self.quitSignal,^{[NSApp performSelector:@selector(terminate:) withObject:nil afterDelay:0];});dispatch_resume(self.quitSignal);
    NSString* hostAddress=[[NSUserDefaults standardUserDefaults]stringForKey:@"hostAddress"];for(NSMenuItem* item in self.routes.itemArray)if([item.representedObject isEqual:hostAddress]){[self.routes selectItem:item];break;}
    if([[NSUserDefaults standardUserDefaults]boolForKey:@"startServerOnLaunch"])[self performSelector:@selector(toggleServer:) withObject:nil afterDelay:1];
    if([[NSUserDefaults standardUserDefaults]boolForKey:@"connectOnLaunch"])[self performSelector:@selector(connect:) withObject:nil afterDelay:1];
}
- (void)refresh:(id)sender{(void)sender;NSString* selected=self.routes.selectedItem.representedObject;[self.routes removeAllItems];[self.routes addItemWithTitle:@"Automatic route"];self.routes.lastItem.representedObject=@"";
    self.routes.toolTip=@"Client: automatic routing. Server: listen on all connected Ethernet/Wi-Fi addresses. Select an address to restrict either role to that interface.";
    try{for(const auto& entry:dm::mac::localAddresses()){
        [self.routes addItemWithTitle:[NSString stringWithFormat:@"%s ? %s",entry.interface.c_str(),entry.address.c_str()]];
        self.routes.lastItem.representedObject=[NSString stringWithUTF8String:entry.address.c_str()];
        if([self.routes.lastItem.representedObject isEqual:selected])[self.routes selectItem:self.routes.lastItem];
    }}catch(const std::exception& e){self.serverStatus.stringValue=[NSString stringWithUTF8String:e.what()];}
}
- (void)resetInput{heldKeys.clear();heldButtons.clear();if(session)session->input.reset();}
- (void)finishQuit{[NSApp replyToApplicationShouldTerminate:YES];}
- (void)updateActive {
    self.connectButton.title=session?(session->stopped?@"Stopping display":@"Disconnect display"):@"Connect display";self.connectButton.toolTip=self.connectButton.title;self.connectButton.accessibilityLabel=self.connectButton.title;self.connectButton.bezelColor=session?NSColor.systemRedColor:NSColor.systemGreenColor;self.connectButton.enabled=!session||!session->stopped;
    self.disconnectAllButton.hidden=!(session!=nullptr||self.resources.active);
    const BOOL active=session!=nullptr||self.resources.active||(host&&host->running());
    if(active&&!self.tray){
        self.tray=[[NSStatusBar systemStatusBar]statusItemWithLength:NSVariableStatusItemLength];self.tray.button.title=@"ExtraPane";
        NSMenu* menu=[NSMenu new];NSMenuItem* show=[menu addItemWithTitle:@"Open ExtraPane" action:@selector(showWindow:) keyEquivalent:@""];show.target=self;
        NSMenuItem* disconnect=[menu addItemWithTitle:@"Disconnect all clients" action:@selector(disconnectAll:) keyEquivalent:@""];disconnect.target=self;
        [menu addItemWithTitle:@"Quit ExtraPane" action:@selector(terminate:) keyEquivalent:@""];self.tray.menu=menu;
    }
    if(!active){if(self.tray){[[NSStatusBar systemStatusBar]removeStatusItem:self.tray];self.tray=nil;}if(self.quitting){self.quitting=NO;[self performSelector:@selector(finishQuit) withObject:nil afterDelay:0];}else if(!self.window.visible)[NSApp terminate:nil];}
}
- (void)disconnectAll:(id)sender{[self disconnect:sender];[self.resources stopAll];}
- (void)showWindow:(id)sender{(void)sender;[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];}
- (unsigned)selectedPort{NSString* text=self.port.stringValue;NSCharacterSet* invalid=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789"]invertedSet];if(!text.length || [text rangeOfCharacterFromSet:invalid].location!=NSNotFound || text.integerValue<1 || text.integerValue>65535)throw std::runtime_error("Enter a TCP port from 1 to 65535");return unsigned(text.integerValue);}
- (void)refreshAccessStatus:(NSTimer*)timer{(void)timer;if(!accessGate)return;auto state=accessGate->status();if(state==dm::access::Gate::Status::active)return;
    self.generatedCode.stringValue=state==dm::access::Gate::Status::used?@"Code used. Generate a new code.":state==dm::access::Gate::Status::expired?@"Code expired. Generate a new code.":@"Attempt limit reached. Generate a new code.";
}
- (void)generateCode:(id)sender{(void)sender;if(!accessGate){self.generatedCode.stringValue=@"Start Server with Require access code enabled";return;}try{self.generatedCode.stringValue=[NSString stringWithFormat:@"%s ? one use ? 5 minutes",accessGate->generate().c_str()];}catch(const std::exception& e){self.generatedCode.stringValue=[NSString stringWithUTF8String:e.what()];}}
- (void)toggleServer:(id)sender{(void)sender;if(host && host->running()){host->stop();self.serverButton.enabled=NO;self.serverStatus.stringValue=@"Stopping server and removing its monitors…";return;}
    if(@available(macOS 13.0,*)){}else{self.serverStatus.stringValue=@"Hosting requires macOS 13 or newer";return;}
    if(!dm::mac::VirtualDisplay::available()){self.serverStatus.stringValue=@"This macOS version does not support the virtual-monitor API";return;}
    NSString* address=self.routes.selectedItem.representedObject?:@"";
    if(!CGPreflightScreenCaptureAccess()){CGRequestScreenCaptureAccess();self.serverStatus.stringValue=@"Allow ExtraPane in Privacy & Security > Screen Recording, then retry (macOS may require reopening the app)";return;}
    if(!AXIsProcessTrusted()){NSDictionary* options=@{(__bridge id)kAXTrustedCheckOptionPrompt:@YES};AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);}
    unsigned selectedPort=0;try{selectedPort=[self selectedPort];}catch(const std::exception& e){self.serverStatus.stringValue=[NSString stringWithUTF8String:e.what()];return;}
    accessGate=self.requireCode.state==NSControlStateValueOn?std::make_shared<dm::access::Gate>():nullptr;if(accessGate)[self generateCode:nil];
    __weak App* weak=self;host=std::make_unique<dm::mac::Host>(address.UTF8String,[weak](std::string message){NSString* text=[NSString stringWithUTF8String:message.c_str()];NSLog(@"ExtraPane host: %@",text);dispatch_async(dispatch_get_main_queue(),^{App* app=weak;if(!app)return;app.serverStatus.stringValue=text;
        if([text isEqualToString:@"Server stopped"]){if(app->accessGate)app->accessGate->revoke();app->accessGate.reset();app.generatedCode.stringValue=@"No active code";app.requireCode.enabled=YES;app.serverButton.title=@"Start Server";app.serverButton.enabled=YES;[app updateActive];}
    });},selectedPort,accessGate);self.requireCode.enabled=NO;self.serverButton.title=@"Stop Server";self.serverStatus.stringValue=@"Starting listener…";
    [self updateActive];
}
- (void)fullscreen:(id)sender{(void)sender;[self resetInput];[self.window toggleFullScreen:nil];}
- (void)showControls{BOOL wasHidden=self.controls.hidden;self.controls.hidden=NO;if(wasHidden)[self resetInput];self.video.remoteEnabled=session && !self.paused;[NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(hideControls) object:nil];if(session && !self.paused)[self performSelector:@selector(hideControls) withObject:nil afterDelay:5];}
- (void)hideControls {if(session && !self.paused){NSPoint p=[self.controls convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil];if(!self.controls.hidden && NSPointInRect(p,self.controls.bounds)){[self performSelector:@selector(hideControls) withObject:nil afterDelay:1];return;}self.controls.hidden=YES;self.video.remoteEnabled=YES;[self.window makeFirstResponder:self.video];}}
- (void)displayPower:(id)sender{if(session)[self disconnect:sender];else [self connect:sender];}
- (void)disconnect:(id)sender{(void)sender;[NSObject cancelPreviousPerformRequestsWithTarget:self];[self resetInput];self.video.remoteEnabled=NO;if(session){session->cancel();self.status.stringValue=@"Disconnecting…";[self updateActive];}}
- (void)connect:(id)sender{(void)sender;if(session)return;try{
    dm::mac::ResourceTarget target{self.address.stringValue.UTF8String,[(NSString*)self.routes.selectedItem.representedObject UTF8String]?:"",[self selectedPort]};
    if(!dm::mac::ipv4(target.address.c_str()))throw std::runtime_error("Enter a reachable server IPv4 address");
    std::string code=self.accessCode.stringValue.UTF8String;auto current=std::make_shared<Session>();self.accessCode.stringValue=@"";session=current;[self updateActive];
    [[NSUserDefaults standardUserDefaults]setObject:self.address.stringValue forKey:@"server"];self.address.enabled=NO;self.routes.enabled=NO;self.status.stringValue=@"Connecting…";self.paused=NO;
    auto screen=self.window.screen;auto size=screen.frame.size;double scale=screen.backingScaleFactor;wire::ClientDisplay preferred{uint32_t(size.width*scale),uint32_t(size.height*scale),60};
    std::thread([current,self,preferred,target,code=std::move(code)]() mutable {@autoreleasepool{struct Wipe{std::string& code;~Wipe(){std::fill(code.begin(),code.end(),0);}}wipe{code};NSString* ending=@"Disconnected";try{
        auto admission=dm::mac::acquireAdmission(target,code,0,[current]{return current->stopped.load();});std::fill(code.begin(),code.end(),0);code.clear();
        auto ownedTransport=std::make_shared<dm::mac::Transport>(target.address.c_str(),target.local.c_str(),target.port,admission->credential);current->setTransport(ownedTransport);
        if(current->stopped)throw std::runtime_error("Disconnected");
        auto& transport=*ownedTransport;transport.connect();NSLog(@"ExtraPane: TLS ready");auto request=wire::request;request[7]=1;request[8]=3;wire::set_client_display(request,preferred);transport.write(request);
        auto offer=wire::decode_offer(transport.read(32,45));NSLog(@"ExtraPane: offer %ux%u caps=%u",offer.width,offer.height,offer.input_caps);dm::mac::Media media;
        auto prepare=[&]{if(offer.codec!=wire::Codec::h264 || (offer.input_caps&~3))throw std::runtime_error("Unsupported codec or input grant");current->input.grant(offer.input_caps);media.reset();
            dispatch_sync(dispatch_get_main_queue(),^{[self resetInput];[self.video.display flushAndRemoveImage];self.video.sourceSize=NSMakeSize(offer.width,offer.height);self.video.remoteEnabled=!self.paused;self.status.stringValue=[NSString stringWithFormat:@"Connected · TLS 1.3 · %u × %u · input: %@%@",offer.width,offer.height,(offer.input_caps&1)?@"mouse ":@"",(offer.input_caps&2)?@"keyboard":@""];[self.window makeFirstResponder:self.video];[self performSelector:@selector(hideControls) withObject:nil afterDelay:5];});transport.write(wire::ready);};prepare();
        uint32_t sequence=0,count=0;uint64_t previous=0,total=0;auto tick=std::chrono::steady_clock::now();
        while(!current->stopped && (!offer.frames || sequence<offer.frames)){
            if(!admission->alive())throw std::runtime_error("Server admission ended");
            auto header=wire::decode_header(transport.read(24));if(header.sequence!=sequence || header.micros<previous)throw std::runtime_error("Out-of-order frame");previous=header.micros;
            if(!header.bytes){auto next=wire::decode_offer(transport.read(32));if(next.frames!=offer.frames || next.codec!=offer.codec || next.input_caps!=offer.input_caps || (next.input_caps && next.input_epoch==offer.input_epoch))throw std::runtime_error("Invalid display reconfiguration");offer=next;prepare();continue;}
            auto bytes=transport.read(header.bytes);if(offer.input_caps)transport.write(current->input.take(offer.input_epoch,sequence));
            auto sample=media.sample(bytes,header.micros,offer.width,offer.height);if(sample){bool delivered=false;auto deadline=std::chrono::steady_clock::now()+500ms;
                while(!current->stopped && !delivered){__block bool ready=false,failed=false;dispatch_sync(dispatch_get_main_queue(),^{failed=self.video.display.status==AVQueuedSampleBufferRenderingStatusFailed;ready=self.video.display.readyForMoreMediaData;if(ready && !failed)[self.video.display enqueueSampleBuffer:sample];});
                    if(failed){CFRelease(sample);throw std::runtime_error("Apple H.264 renderer failed");}delivered=ready;if(!delivered){if(std::chrono::steady_clock::now()>deadline){CFRelease(sample);throw std::runtime_error("Decoder stalled; disconnected to bound latency");}std::this_thread::sleep_for(2ms);}}
                CFRelease(sample);
            }
            ++sequence;++count;total+=bytes.size();auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-tick).count();if(seconds>=1){NSString* stats=[NSString stringWithFormat:@"Receive %.1f FPS · %.1f Mbps · Apple H.264 display layer",count/seconds,total*8e-6/seconds];dispatch_async(dispatch_get_main_queue(),^{self.statistics.stringValue=stats;});count=0;total=0;tick=now;}
        }
        if(!current->stopped && offer.frames){const uint8_t done[]={'D','O','N','E'};transport.write(done);}
    }catch(const std::exception& e){if(!current->stopped)ending=[NSString stringWithUTF8String:e.what()];}NSLog(@"ExtraPane: session ended: %@",ending);
    current->cancel();dispatch_async(dispatch_get_main_queue(),^{if(self->session!=current)return;[NSObject cancelPreviousPerformRequestsWithTarget:self];self->session.reset();[self resetInput];self.video.remoteEnabled=NO;[self.video.display flushAndRemoveImage];self.controls.hidden=NO;self.connectButton.enabled=YES;self.address.enabled=YES;self.routes.enabled=YES;self.status.stringValue=ending;[self updateActive];});}}).detach();
    }catch(const std::exception& e){self.status.stringValue=[NSString stringWithUTF8String:e.what()];session.reset();[self updateActive];}}
- (void)settings:(id)sender{(void)sender;BOOL wasPaused=self.paused;self.paused=YES;[self resetInput];[self showControls];NSAlert* a=[NSAlert new];a.messageText=@"Client settings";a.informativeText=@"Mouse and physical keyboard use the host layout. Command maps to the Windows key. macOS-reserved shortcuts remain local. Trackpad gestures are mouse/scroll input, not remote multi-touch. Native pen/touch and system audio playback are not implemented. Camera and microphone sending use the resource rows; virtual receiving endpoints are unavailable. Mac server shortcuts: Ctrl becomes Command, Windows/Super becomes Control, and Alt becomes Option (left/right preserved). Hosting requires Screen Recording permission; remote mouse/keyboard also requires Accessibility.\n\nTLS encrypts the connection but does not verify peer identity. Use a trusted LAN. Statistics measure reception, not end-to-end latency.";[a addButtonWithTitle:self.showStats?@"Hide statistics":@"Show statistics"];[a addButtonWithTitle:@"Close"];if([a runModal]==NSAlertFirstButtonReturn)self.showStats=!self.showStats;self.statistics.hidden=!self.showStats;self.paused=wasPaused;[self showControls];}
- (void)about:(id)sender{(void)sender;BOOL wasPaused=self.paused;self.paused=YES;[self resetInput];[self showControls];NSAlert* a=[NSAlert new];a.messageText=@"ExtraPane";a.informativeText=@"Another screen for your desktop.\nRunning version: " EXTRAPANE_MAC_VERSION @"\nmacOS client preview · Apple Silicon and Intel\nA project-wide license has not been selected.";[a addButtonWithTitle:@"GitHub / README"];[a addButtonWithTitle:@"Close"];if([a runModal]==NSAlertFirstButtonReturn)[[NSWorkspace sharedWorkspace]openURL:[NSURL URLWithString:@"https://github.com/EEkebin/ExtraPane"]];self.paused=wasPaused;[self showControls];}
- (void)key:(NSEvent*)event down:(BOOL)down{
    if(!session)return;unsigned code=event.keyCode;BOOL repeat=event.type==NSEventTypeKeyDown && event.isARepeat;BOOL quit=code==12 && (event.modifierFlags&NSEventModifierFlagControl) && (event.modifierFlags&NSEventModifierFlagShift);
    if(code==109 || code==103 || code==100 || quit){if(down && !repeat){[self resetInput];if(quit){[self disconnect:nil];[self.resources stopAll];}else if(code==103)[self.window toggleFullScreen:nil];else if(code==100){self.paused=!self.paused;self.video.remoteEnabled=!self.paused;if(self.paused)[self showControls];else [self hideControls];}else [self showControls];}return;}
    if(!self.video.remoteEnabled)return;auto key=mapKey(code);if(!key.vk)return;
    if(down){if(repeat && !heldKeys.contains(code))return;heldKeys.insert(code);}else if(!heldKeys.erase(code))return;
    input::Event e;e.kind=input::Kind::key;e.action=down?input::Action::down:input::Action::up;e.id=key.vk;e.code=key.scan;e.value=key.ext;session->input.add(e);
}
- (void)mouse:(NSEvent*)event action:(input::Action)action{
    if(!session)return;auto p=[self.video convertPoint:event.locationInWindow fromView:nil];auto bounds=self.video.bounds;auto size=self.video.sourceSize;double factor=std::min(bounds.size.width/size.width,bounds.size.height/size.height);NSRect picture=NSMakeRect((bounds.size.width-size.width*factor)/2,(bounds.size.height-size.height*factor)/2,size.width*factor,size.height*factor);
    if(action==input::Action::move && heldButtons.empty() && p.y<20 && std::abs(p.x-bounds.size.width/2)<50){[self showControls];return;}
    if(!self.video.remoteEnabled || !picture.size.width || !picture.size.height)return;unsigned b=unsigned(event.buttonNumber)+1;if(b>5)return;
    if(action==input::Action::down){if(!NSPointInRect(p,picture))return;heldButtons.insert(b);}else if(action==input::Action::up){if(!heldButtons.erase(b))return;}else if(heldButtons.empty() && !NSPointInRect(p,picture))return;
    input::Event e;e.kind=input::Kind::mouse;e.action=action;e.x=uint32_t(std::clamp((p.x-picture.origin.x)/picture.size.width,0.0,1.0)*65535);e.y=uint32_t(std::clamp((p.y-picture.origin.y)/picture.size.height,0.0,1.0)*65535);
    if(action==input::Action::wheel){for(unsigned axis=0;axis<2;++axis){double delta=axis?event.scrollingDeltaX:event.scrollingDeltaY;e.code=axis;e.value=int32_t(std::clamp(delta*(event.hasPreciseScrollingDeltas?3:120),-32767.0,32767.0));if(e.value)session->input.add(e);}}
    else{if(action!=input::Action::move)e.code=b;session->input.add(e);}
}
- (void)windowDidResignKey:(NSNotification*)note{(void)note;[self resetInput];self.video.remoteEnabled=NO;}
- (void)windowDidBecomeKey:(NSNotification*)note{(void)note;self.video.remoteEnabled=session && !self.paused;}
- (BOOL)windowShouldClose:(NSWindow*)window{if(session||self.resources.active||(host&&host->running())){[window orderOut:nil];[self updateActive];return NO;}return YES;}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app{(void)app;return !session&&!self.resources.active&&(!host||!host->running());}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app{(void)app;if(session||self.resources.active||(host&&host->running())){self.quitting=YES;[self disconnect:nil];[self.resources stopAll];if(host&&host->running())host->stop();[self performSelector:@selector(updateActive) withObject:nil afterDelay:0];return NSTerminateLater;}return NSTerminateNow;}
- (void)applicationWillTerminate:(NSNotification*)note{(void)note;[self disconnect:nil];[self.resources stopAll];host.reset();}
@end
void mediaCheck();
static int checks(){try{
    auto require=[](bool condition){if(!condition)throw std::runtime_error("macOS input/media check failed");};
    require(mapKey(0).vk=='A' && mapKey(62).ext==1 && mapKey(126).scan==72 && !mapKey(255).vk);
    require(mapHostKey(29,0)==55 && mapHostKey(29,1)==54);
    require(mapHostKey(91,1)==59 && mapHostKey(92,1)==62);
    require(mapHostKey(56,0)==58 && mapHostKey(56,1)==61);
    require(mapHostKey(42,0)==56 && mapHostKey(54,0)==60);
    require(mapHostKey(30,0)==0 && mapHostKey(28,1)==76 && mapHostKey(255,0)==256 && mapHostKey(29,2)==256);
    InputQueue queue;queue.grant(3);input::Event e;e.kind=input::Kind::key;e.action=input::Action::down;e.id='A';e.code=30;queue.add(e);auto batch=queue.take(1,0);require(batch.size()==96);queue.reset();require(queue.take(1,1).size()==96);
    const uint8_t packet[]={0,0,0,1,0x67,1,2,0,0,1,0x68,3,4};auto units=dm::mac::annexb(packet);require(units.size()==2 && units[0].size()==3 && units[1][0]==0x68);
    bool rejected=false;try{const uint8_t bad[]={4,5,6};dm::mac::annexb(bad);}catch(...){rejected=true;}require(rejected);
    require(dm::mac::ipv4("10.0.0.1") && !dm::mac::ipv4("127.0.0.1") && !dm::mac::ipv4("hostname"));
    mediaCheck();puts("macOS protocol, input cleanup and native H.264 color round-trip checks passed");return 0;
}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}}
int main(int argc,const char* argv[]){@autoreleasepool{if(argc==6 && !strcmp(argv[1],"--virtual-display-owner"))return dm::mac::displayOwner(unsigned(strtoul(argv[2],nullptr,10)),unsigned(strtoul(argv[3],nullptr,10)),unsigned(strtoul(argv[4],nullptr,10)),unsigned(strtoul(argv[5],nullptr,10)));if(argc>1 && !strcmp(argv[1],"--display-list"))return dm::mac::displayList();if(argc>1 && !strcmp(argv[1],"--virtual-display-check"))return dm::mac::displayCheck();if(argc>1 && !strcmp(argv[1],"--self-test"))return checks();[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];App* app=[App new];NSApp.delegate=app;
    if(argc>1 && !strcmp(argv[1],"--ui-smoke"))dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{if(!resourceSourceMenuCheck() || !app.window || !app.video || !app.address || !app.connectButton.enabled || !app.resources.view || app.resources.active)exit(1);[app.window setContentSize:NSMakeSize(800,680)];[app.window.contentView layoutSubtreeIfNeeded];auto fits=[&](NSView* view){return NSContainsRect(app.window.contentView.bounds,[view convertRect:view.bounds toView:app.window.contentView]);};if(app.resources.view.frame.size.height<139||app.video.frame.size.height<239||!fits(app.resources.view)||!fits(app.video)||!fits(app.address)||!fits(app.routes)){NSLog(@"Resource UI layout did not fit compact window");exit(1);}puts("macOS AppKit startup/compact-layout/shutdown passed");[NSApp terminate:nil];});
    [NSApp run];return 0;}}
