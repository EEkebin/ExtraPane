#import <AppKit/AppKit.h>
#include "transport.hpp"
#include "media.hpp"
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
using namespace std::chrono_literals;
namespace wire=dm::video::wire;
namespace input=dm::input;
struct Key {unsigned vk=0,scan=0,ext=0;};
static Key mapKey(unsigned code){
    // macOS hardware positions -> Windows set-1. Host layout determines text.
    static const unsigned mac[]={0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
    static const unsigned scans[]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    for(unsigned i=0;i<26;++i)if(code==mac[i])return {'A'+i,scans[i],0};
    struct Entry{unsigned mac,vk,scan,ext;};
    static const Entry other[]={
        {18,'1',2,0},{19,'2',3,0},{20,'3',4,0},{21,'4',5,0},{23,'5',6,0},{22,'6',7,0},{26,'7',8,0},{28,'8',9,0},{25,'9',10,0},{29,'0',11,0},
        {27,0xbd,12,0},{24,0xbb,13,0},{51,8,14,0},{48,9,15,0},{33,0xdb,26,0},{30,0xdd,27,0},{36,13,28,0},{41,0xba,39,0},{39,0xde,40,0},{50,0xc0,41,0},{42,0xdc,43,0},{43,0xbc,51,0},{47,0xbe,52,0},{44,0xbf,53,0},{49,32,57,0},{53,27,1,0},
        {57,0x14,58,0},{56,0xa0,42,0},{60,0xa1,54,0},{59,0xa2,29,0},{62,0xa3,29,1},{58,0xa4,56,0},{61,0xa5,56,1},{55,0x5b,91,1},{54,0x5c,92,1},
        {123,0x25,75,1},{124,0x27,77,1},{125,0x28,80,1},{126,0x26,72,1},{115,0x24,71,1},{119,0x23,79,1},{116,0x21,73,1},{121,0x22,81,1},{117,0x2e,83,1},{114,0x2d,82,1},
        {122,0x70,59,0},{120,0x71,60,0},{99,0x72,61,0},{118,0x73,62,0},{96,0x74,63,0},{97,0x75,64,0},{98,0x76,65,0},{100,0x77,66,0},{101,0x78,67,0},{109,0x79,68,0},{103,0x7a,87,0},{111,0x7b,88,0},
        {82,0x60,82,0},{83,0x61,79,0},{84,0x62,80,0},{85,0x63,81,0},{86,0x64,75,0},{87,0x65,76,0},{88,0x66,77,0},{89,0x67,71,0},{91,0x68,72,0},{92,0x69,73,0},{65,0x6e,83,0},{67,0x6a,55,0},{69,0x6b,78,0},{75,0x6f,53,1},{78,0x6d,74,0},{76,13,28,1}};
    for(auto e:other)if(e.mac==code)return {e.vk,e.scan,e.ext};return {};
}
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
struct Session {std::atomic_bool stopped=false;std::shared_ptr<dm::mac::Transport> transport;InputQueue input;void cancel(){stopped=true;transport->cancel();input.grant(0);}};
@class App;
@interface VideoView:NSView
@property(nonatomic,weak) App* owner;
@property(nonatomic,strong) AVSampleBufferDisplayLayer* display;
@property(nonatomic) NSSize sourceSize;
@property(nonatomic) BOOL remoteEnabled;
@end
@interface App:NSObject<NSApplicationDelegate,NSWindowDelegate> {
@public std::shared_ptr<Session> session;
    std::set<unsigned> heldKeys,heldButtons;
}
@property(nonatomic,strong) NSWindow* window;
@property(nonatomic,strong) VideoView* video;
@property(nonatomic,strong) NSStackView* controls;
@property(nonatomic,strong) NSTextField* address;
@property(nonatomic,strong) NSTextField* status;
@property(nonatomic,strong) NSTextField* statistics;
@property(nonatomic,strong) NSPopUpButton* routes;
@property(nonatomic,strong) NSButton* connectButton;
@property(nonatomic) BOOL paused;
@property(nonatomic) BOOL showStats;
- (void)resetInput;
- (void)showControls;
- (void)disconnect:(id)sender;
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
    (void)notification;self.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,1000,700) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];self.window.title=@"ExtraPane";self.window.delegate=self;self.window.minSize=NSMakeSize(680,440);self.window.collectionBehavior=NSWindowCollectionBehaviorFullScreenPrimary;
    NSStackView* root=[NSStackView stackViewWithViews:@[]];root.orientation=NSUserInterfaceLayoutOrientationVertical;root.spacing=10;root.edgeInsets=NSEdgeInsetsMake(12,12,12,12);self.window.contentView=root;
    self.address=[[NSTextField alloc]initWithFrame:NSZeroRect];self.address.placeholderString=@"Server IPv4 address";self.address.stringValue=[[NSUserDefaults standardUserDefaults]stringForKey:@"server"]?:@"";[self.address.widthAnchor constraintGreaterThanOrEqualToConstant:180].active=YES;
    self.routes=[[NSPopUpButton alloc]initWithFrame:NSZeroRect pullsDown:NO];[self refresh:nil];
    self.connectButton=button(@"Connect",self,@selector(connect:));
    self.controls=[NSStackView stackViewWithViews:@[self.address,self.routes,button(@"Refresh",self,@selector(refresh:)),self.connectButton,button(@"Disconnect",self,@selector(disconnect:)),button(@"Settings",self,@selector(settings:)),button(@"About",self,@selector(about:))]];self.controls.spacing=8;
    [root addArrangedSubview:self.controls];self.status=label(@"Client ready. macOS hosting is not available in this preview.");[root addArrangedSubview:self.status];
    self.video=[[VideoView alloc]initWithFrame:NSMakeRect(0,0,960,550)];self.video.owner=self;[root addArrangedSubview:self.video];[self.video.widthAnchor constraintEqualToAnchor:root.widthAnchor constant:-24].active=YES;[self.video.heightAnchor constraintGreaterThanOrEqualToConstant:240].active=YES;
    self.statistics=label(@"");self.statistics.hidden=YES;[root addArrangedSubview:self.statistics];[root addArrangedSubview:label(@"F10 controls · F11 fullscreen · F8 pause input · Ctrl+Shift+Q disconnect")];
    NSMenu* menu=[NSMenu new];NSMenuItem* appItem=[NSMenuItem new];[menu addItem:appItem];NSMenu* appMenu=[NSMenu new];[appMenu addItemWithTitle:@"Quit ExtraPane" action:@selector(terminate:) keyEquivalent:@"q"];appItem.submenu=appMenu;NSApp.mainMenu=menu;
    [self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
}
- (void)refresh:(id)sender{(void)sender;[self.routes removeAllItems];[self.routes addItemWithTitle:@"Automatic route"];self.routes.lastItem.representedObject=@"";
    ifaddrs* list=nullptr;if(getifaddrs(&list))return;for(auto a=list;a;a=a->ifa_next){if(!a->ifa_addr || a->ifa_addr->sa_family!=AF_INET || !(a->ifa_flags&IFF_UP) || !(a->ifa_flags&IFF_RUNNING) || a->ifa_flags&IFF_LOOPBACK || strncmp(a->ifa_name,"en",2))continue;
        char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,&((sockaddr_in*)a->ifa_addr)->sin_addr,ip,sizeof(ip));if(!dm::mac::ipv4(ip))continue;[self.routes addItemWithTitle:[NSString stringWithFormat:@"%s — %s",a->ifa_name,ip]];self.routes.lastItem.representedObject=[NSString stringWithUTF8String:ip];}freeifaddrs(list);
}
- (void)resetInput{heldKeys.clear();heldButtons.clear();if(session)session->input.reset();}
- (void)showControls{self.controls.hidden=NO;self.paused=YES;self.video.remoteEnabled=NO;[self resetInput];}
- (void)hideControls {if(session && !self.paused){self.controls.hidden=YES;self.video.remoteEnabled=YES;[self.window makeFirstResponder:self.video];}}
- (void)disconnect:(id)sender{(void)sender;[NSObject cancelPreviousPerformRequestsWithTarget:self];[self resetInput];self.video.remoteEnabled=NO;if(session){session->cancel();self.status.stringValue=@"Disconnecting…";}}
- (void)connect:(id)sender{(void)sender;if(session)return;try{
    auto current=std::make_shared<Session>();current->transport=std::make_shared<dm::mac::Transport>(self.address.stringValue.UTF8String,[(NSString*)self.routes.selectedItem.representedObject UTF8String]);session=current;
    [[NSUserDefaults standardUserDefaults]setObject:self.address.stringValue forKey:@"server"];self.connectButton.enabled=NO;self.address.enabled=NO;self.routes.enabled=NO;self.status.stringValue=@"Connecting…";self.paused=NO;
    auto screen=self.window.screen;auto size=screen.frame.size;double scale=screen.backingScaleFactor;wire::ClientDisplay preferred{uint32_t(size.width*scale),uint32_t(size.height*scale),60};
    std::thread([current,self,preferred]{@autoreleasepool{NSString* ending=@"Disconnected";try{
        auto& transport=*current->transport;transport.connect();auto request=wire::request;request[7]=1;request[8]=3;wire::set_client_display(request,preferred);transport.write(request);
        auto offer=wire::decode_offer(transport.read(32,45));dm::mac::Media media;
        auto prepare=[&]{if(offer.codec!=wire::Codec::h264 || (offer.input_caps&~3))throw std::runtime_error("Unsupported codec or input grant");current->input.grant(offer.input_caps);media.reset();
            dispatch_sync(dispatch_get_main_queue(),^{[self resetInput];[self.video.display flushAndRemoveImage];self.video.sourceSize=NSMakeSize(offer.width,offer.height);self.video.remoteEnabled=!self.paused;self.status.stringValue=[NSString stringWithFormat:@"Connected · TLS 1.3 · %u × %u · input: %@%@",offer.width,offer.height,(offer.input_caps&1)?@"mouse ":@"",(offer.input_caps&2)?@"keyboard":@""];[self.window makeFirstResponder:self.video];[self performSelector:@selector(hideControls) withObject:nil afterDelay:5];});transport.write(wire::ready);};prepare();
        uint32_t sequence=0,count=0;uint64_t previous=0,total=0;auto tick=std::chrono::steady_clock::now();
        while(!current->stopped && (!offer.frames || sequence<offer.frames)){
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
    }catch(const std::exception& e){if(!current->stopped)ending=[NSString stringWithUTF8String:e.what()];}
    current->cancel();dispatch_async(dispatch_get_main_queue(),^{if(self->session!=current)return;[NSObject cancelPreviousPerformRequestsWithTarget:self];self->session.reset();[self resetInput];self.video.remoteEnabled=NO;[self.video.display flushAndRemoveImage];self.controls.hidden=NO;self.connectButton.enabled=YES;self.address.enabled=YES;self.routes.enabled=YES;self.status.stringValue=ending;});}}).detach();
    }catch(const std::exception& e){self.status.stringValue=[NSString stringWithUTF8String:e.what()];session.reset();}}
- (void)settings:(id)sender{(void)sender;[self showControls];NSAlert* a=[NSAlert new];a.messageText=@"Client settings";a.informativeText=@"Mouse and physical keyboard use the host layout. Command maps to the Windows key. macOS-reserved shortcuts remain local. Trackpad gestures are mouse/scroll input, not remote multi-touch. Pen, audio, microphone and Mac hosting are not implemented.\n\nTLS encrypts the connection but does not verify peer identity. Use a trusted LAN. Statistics measure reception, not end-to-end latency.";[a addButtonWithTitle:self.showStats?@"Hide statistics":@"Show statistics"];[a addButtonWithTitle:@"Resume"];if([a runModal]==NSAlertFirstButtonReturn)self.showStats=!self.showStats;self.statistics.hidden=!self.showStats;self.paused=NO;[self hideControls];}
- (void)about:(id)sender{(void)sender;[self showControls];NSAlert* a=[NSAlert new];a.messageText=@"ExtraPane";a.informativeText=@"Another screen for your desktop.\nRunning version: " EXTRAPANE_MAC_VERSION @"\nmacOS client preview · Apple Silicon and Intel\nA project-wide license has not been selected.";[a addButtonWithTitle:@"GitHub / README"];[a addButtonWithTitle:@"Close"];if([a runModal]==NSAlertFirstButtonReturn)[[NSWorkspace sharedWorkspace]openURL:[NSURL URLWithString:@"https://github.com/EEkebin/ExtraPane"]];}
- (void)key:(NSEvent*)event down:(BOOL)down{
    if(!session)return;unsigned code=event.keyCode;BOOL repeat=event.type==NSEventTypeKeyDown && event.isARepeat;BOOL quit=code==12 && (event.modifierFlags&NSEventModifierFlagControl) && (event.modifierFlags&NSEventModifierFlagShift);
    if(code==109 || code==103 || code==100 || quit){if(down && !repeat){[self resetInput];if(quit)[self disconnect:nil];else if(code==103)[self.window toggleFullScreen:nil];else if(code==100){self.paused=!self.paused;self.video.remoteEnabled=!self.paused;if(self.paused)[self showControls];else [self hideControls];}else [self showControls];}return;}
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
- (BOOL)windowShouldClose:(NSWindow*)window{(void)window;[self disconnect:nil];return YES;}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app{(void)app;return YES;}
- (void)applicationWillTerminate:(NSNotification*)note{(void)note;[self disconnect:nil];}
@end
void mediaCheck();
static int checks(){try{
    auto require=[](bool condition){if(!condition)throw std::runtime_error("macOS input/media check failed");};
    require(mapKey(0).vk=='A' && mapKey(62).ext==1 && mapKey(126).scan==72 && !mapKey(255).vk);
    InputQueue queue;queue.grant(3);input::Event e;e.kind=input::Kind::key;e.action=input::Action::down;e.id='A';e.code=30;queue.add(e);auto batch=queue.take(1,0);require(batch.size()==96);queue.reset();require(queue.take(1,1).size()==96);
    const uint8_t packet[]={0,0,0,1,0x67,1,2,0,0,1,0x68,3,4};auto units=dm::mac::annexb(packet);require(units.size()==2 && units[0].size()==3 && units[1][0]==0x68);
    bool rejected=false;try{const uint8_t bad[]={4,5,6};dm::mac::annexb(bad);}catch(...){rejected=true;}require(rejected);
    require(dm::mac::ipv4("10.0.0.1") && !dm::mac::ipv4("127.0.0.1") && !dm::mac::ipv4("hostname"));
    mediaCheck();puts("macOS protocol, input cleanup and native H.264 color round-trip checks passed");return 0;
}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}}
int main(int argc,const char* argv[]){@autoreleasepool{if(argc>1 && !strcmp(argv[1],"--self-test"))return checks();[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];App* app=[App new];NSApp.delegate=app;
    if(argc>1 && !strcmp(argv[1],"--ui-smoke"))dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{if(!app.window || !app.video || !app.address || !app.connectButton.enabled)exit(1);puts("macOS AppKit startup/shutdown passed");[NSApp terminate:nil];});
    [NSApp run];return 0;}}
