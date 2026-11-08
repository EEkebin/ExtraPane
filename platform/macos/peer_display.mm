#import <AppKit/AppKit.h>
#include "peer_display.hpp"
#include "transport.hpp"
#include "media.hpp"
#include "keys.hpp"
#include <dm/video_protocol.hpp>
#include <dm/input_protocol.hpp>
#include <mutex>
#include <set>
#include <thread>
#include <algorithm>
#include <chrono>
namespace wire=dm::video::wire;namespace input=dm::input;
struct PeerInputQueue {
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
struct PeerSession {std::atomic_bool stopped=false;std::mutex mutex;std::shared_ptr<dm::mac::Transport> transport;PeerInputQueue input;void setTransport(std::shared_ptr<dm::mac::Transport> value){std::lock_guard lock(mutex);transport=std::move(value);if(stopped)transport->cancel();}void cancel(){stopped=true;{std::lock_guard lock(mutex);if(transport)transport->cancel();}input.grant(0);}};

@class EPDeviceWindow;
@interface EPDeviceVideo:NSView
@property(nonatomic,weak) EPDeviceWindow* owner;
@property(nonatomic,strong) AVSampleBufferDisplayLayer* display;
@property(nonatomic) NSSize sourceSize;
@property(nonatomic) BOOL remoteEnabled;
@end
@interface EPDeviceWindow:NSObject<NSWindowDelegate> {
@public std::shared_ptr<PeerSession> session;std::set<unsigned> heldKeys,heldButtons;
}
@property(nonatomic,strong) NSWindow* window;
@property(nonatomic,strong) EPDeviceVideo* video;
- (void)resetInput;
- (void)key:(NSEvent*)event down:(BOOL)down;
- (void)mouse:(NSEvent*)event action:(input::Action)action;
@end
@implementation EPDeviceVideo
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

@implementation EPDeviceWindow
- (void)resetInput{heldKeys.clear();heldButtons.clear();if(session)session->input.reset();}
- (void)key:(NSEvent*)event down:(BOOL)down{
    if(!session)return;unsigned code=event.keyCode;BOOL repeat=event.type==NSEventTypeKeyDown && event.isARepeat;BOOL quit=code==12 && (event.modifierFlags&NSEventModifierFlagControl) && (event.modifierFlags&NSEventModifierFlagShift);
    if(quit){if(down)[self.window close];return;}
    if(!self.video.remoteEnabled)return;auto key=mapKey(code);if(!key.vk)return;
    if(down){if(repeat && !heldKeys.contains(code))return;heldKeys.insert(code);}else if(!heldKeys.erase(code))return;
    input::Event e;e.kind=input::Kind::key;e.action=down?input::Action::down:input::Action::up;e.id=key.vk;e.code=key.scan;e.value=key.ext;session->input.add(e);
}
- (void)mouse:(NSEvent*)event action:(input::Action)action{
    if(!session)return;auto p=[self.video convertPoint:event.locationInWindow fromView:nil];auto bounds=self.video.bounds;auto size=self.video.sourceSize;double factor=std::min(bounds.size.width/size.width,bounds.size.height/size.height);NSRect picture=NSMakeRect((bounds.size.width-size.width*factor)/2,(bounds.size.height-size.height*factor)/2,size.width*factor,size.height*factor);
    
    if(!self.video.remoteEnabled || !picture.size.width || !picture.size.height)return;unsigned b=unsigned(event.buttonNumber)+1;if(b>5)return;
    if(action==input::Action::down){if(!NSPointInRect(p,picture))return;heldButtons.insert(b);}else if(action==input::Action::up){if(!heldButtons.erase(b))return;}else if(heldButtons.empty() && !NSPointInRect(p,picture))return;
    input::Event e;e.kind=input::Kind::mouse;e.action=action;e.x=uint32_t(std::clamp((p.x-picture.origin.x)/picture.size.width,0.0,1.0)*65535);e.y=uint32_t(std::clamp((p.y-picture.origin.y)/picture.size.height,0.0,1.0)*65535);
    if(action==input::Action::wheel){for(unsigned axis=0;axis<2;++axis){double delta=axis?event.scrollingDeltaX:event.scrollingDeltaY;e.code=axis;e.value=int32_t(std::clamp(delta*(event.hasPreciseScrollingDeltas?3:120),-32767.0,32767.0));if(e.value)session->input.add(e);}}
    else{if(action!=input::Action::move)e.code=b;session->input.add(e);}
}
- (void)windowDidResignKey:(NSNotification*)note{(void)note;[self resetInput];self.video.remoteEnabled=NO;}
- (void)windowDidBecomeKey:(NSNotification*)note{(void)note;self.video.remoteEnabled=session && !session->stopped;}

- (void)windowWillClose:(NSNotification*)note{(void)note;[self resetInput];if(session)session->cancel();}
@end
namespace dm::mac::peer {
struct PeerDisplayWindow::State {
 std::shared_ptr<PeerSession> session=std::make_shared<PeerSession>();std::atomic_bool done=false;
 __strong EPDeviceWindow* ui=nil;
};
PeerDisplayWindow::PeerDisplayWindow(DisplayOptions options):state_(std::make_shared<State>()){
 auto state=state_;const unsigned monitor=options.monitor;dispatch_async(dispatch_get_main_queue(),^{
 if(state->session->stopped)return;
 auto ui=[EPDeviceWindow new];state->ui=ui;ui->session=state->session;
 ui.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(80,80,1000,650) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
 ui.window.title=@"ExtraPane - Display";ui.window.delegate=ui;ui.window.releasedWhenClosed=NO;ui.window.acceptsMouseMovedEvents=YES;ui.window.collectionBehavior=NSWindowCollectionBehaviorFullScreenPrimary;
 ui.video=[[EPDeviceVideo alloc]initWithFrame:ui.window.contentView.bounds];ui.video.owner=ui;ui.video.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;ui.window.contentView=ui.video;
 if(monitor)for(NSScreen* screen in NSScreen.screens)if([screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue]==monitor){[ui.window setFrameOrigin:NSMakePoint(screen.frame.origin.x+40,screen.frame.origin.y+40)];break;}
 [ui.window makeKeyAndOrderFront:nil];[ui.window makeFirstResponder:ui.video];
 });
 std::thread([state,options=std::move(options)]()mutable{@autoreleasepool{try{
 auto current=state->session;auto transport=std::make_shared<Transport>("127.0.0.1","0.0.0.0",options.port,options.credential);std::fill(options.credential.begin(),options.credential.end(),0);current->setTransport(transport);if(current->stopped)throw std::runtime_error("Cancelled");transport->connect();
 const unsigned wanted=options.input?3:0;auto request=wire::request;request[7]=wanted?1:0;request[8]=wanted;
 wire::set_screen_request(request,{options.existing?wire::DisplayMode::existing_screen:wire::DisplayMode::extend_desktop,options.existing?1u:0u});
 wire::set_client_display(request,options.existing?wire::ClientDisplay{}:wire::ClientDisplay{options.width,options.height,options.refresh,options.exact});transport->write(request);
 auto offer=wire::decode_offer(transport->read(32,45));if(options.exact&&!options.existing&&(offer.width!=options.width||offer.height!=options.height||offer.fps!=options.refresh))throw std::runtime_error("Server did not accept the requested custom display mode");Media media;
 auto prepare=[&]{if(offer.codec!=wire::Codec::h264||(offer.input_caps&~wanted))throw std::runtime_error("Unsupported media grant");current->input.grant(offer.input_caps);media.reset();auto width=offer.width,height=offer.height;dispatch_async(dispatch_get_main_queue(),^{if(state->ui&&!current->stopped){[state->ui resetInput];[state->ui.video.display flushAndRemoveImage];state->ui.video.sourceSize=NSMakeSize(width,height);state->ui.video.remoteEnabled=state->ui.window.isKeyWindow;}});transport->write(wire::ready);};prepare();
 uint32_t sequence=0;uint64_t previous=0;
 while(!current->stopped&&(!offer.frames||sequence<offer.frames)){
 auto header=wire::decode_header(transport->read(24));if(header.sequence!=sequence||header.micros<previous)throw std::runtime_error("Invalid frame order");previous=header.micros;
 if(!header.bytes){auto next=wire::decode_offer(transport->read(32));if(next.frames!=offer.frames||next.codec!=offer.codec||next.input_caps!=offer.input_caps||(next.input_caps&&next.input_epoch==offer.input_epoch))throw std::runtime_error("Invalid display reconfiguration");offer=next;prepare();continue;}
 auto bytes=transport->read(header.bytes);if(offer.input_caps)transport->write(current->input.take(offer.input_epoch,sequence));
 if(auto sample=media.sample(bytes,header.micros,offer.width,offer.height)){
 auto frame=std::shared_ptr<void>(sample,[](void* value){CFRelease(value);});bool accepted=false;auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
 while(!current->stopped&&!accepted){auto delivered=std::make_shared<std::atomic_int>(0);dispatch_async(dispatch_get_main_queue(),^{
 if(current->stopped||!state->ui)delivered->store(-1);else if(state->ui.video.display.status==AVQueuedSampleBufferRenderingStatusFailed)delivered->store(-1);else if(!state->ui.video.display.readyForMoreMediaData)delivered->store(2);else{[state->ui.video.display enqueueSampleBuffer:static_cast<CMSampleBufferRef>(frame.get())];delivered->store(1);}
 });while(!current->stopped&&!delivered->load()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(2));if(current->stopped)break;if(delivered->load()<1||std::chrono::steady_clock::now()>=until)throw std::runtime_error("Renderer stalled");accepted=delivered->load()==1;if(!accepted)std::this_thread::sleep_for(std::chrono::milliseconds(2));}

 }++sequence;
 }
 }catch(const std::exception&){}state->session->cancel();state->done=true;dispatch_async(dispatch_get_main_queue(),^{[state->ui.window close];state->ui=nil;});}}).detach();
}
PeerDisplayWindow::~PeerDisplayWindow(){stop();}
void PeerDisplayWindow::stop(){auto state=state_;state->session->cancel();dispatch_async(dispatch_get_main_queue(),^{[state->ui.window close];});}
bool PeerDisplayWindow::done()const{return state_->done||state_->session->stopped;}
}
