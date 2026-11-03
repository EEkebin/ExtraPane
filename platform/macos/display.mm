#include "display.hpp"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
// Runtime-only declarations of Apple's undocumented virtual-display interface.
// No hard class references: missing/changed selectors disable hosting safely.
@interface NSObject (ExtraPaneVirtualDisplay)
- (instancetype)initWithDescriptor:(id)descriptor;
- (instancetype)initWithWidth:(unsigned)width height:(unsigned)height refreshRate:(double)rate;
- (BOOL)applySettings:(id)settings;
- (CGDirectDisplayID)displayID;
- (void)setQueue:(dispatch_queue_t)queue;
- (void)setName:(NSString*)name;
- (void)setMaxPixelsWide:(unsigned)value;
- (void)setMaxPixelsHigh:(unsigned)value;
- (void)setSizeInMillimeters:(CGSize)value;
- (void)setVendorID:(unsigned)value;
- (void)setProductID:(unsigned)value;
- (void)setSerialNum:(unsigned)value;
- (void)setSerialNumber:(unsigned)value;
- (void)setHiDPI:(unsigned)value;
- (void)setModes:(NSArray*)modes;
@end
namespace dm::mac {
bool displayOnline(CGDirectDisplayID display){CGDirectDisplayID ids[64]{};uint32_t count=0;if(CGGetOnlineDisplayList(64,ids,&count)!=kCGErrorSuccess)return false;for(unsigned i=0;i<count;++i)if(ids[i]==display)return true;return false;}
int displayList(){CGDirectDisplayID ids[64]{};uint32_t count=0;if(CGGetOnlineDisplayList(64,ids,&count)!=kCGErrorSuccess)return 1;for(unsigned i=0;i<count;++i){auto b=CGDisplayBounds(ids[i]);printf("DISPLAY id=%u primary=%d mirrored=%d bounds=%.0f,%.0f %.0fx%.0f\n",ids[i],CGDisplayIsMain(ids[i]),CGDisplayIsInMirrorSet(ids[i]),b.origin.x,b.origin.y,b.size.width,b.size.height);}printf("DISPLAY_COUNT %u\n",count);return 0;}
class NativeDisplay {
    id object_=nil;
public:
    CGDirectDisplayID id_=0;
    NativeDisplay(unsigned width,unsigned height,unsigned hz,unsigned slot);
};
bool VirtualDisplay::available(){
    const char* classes[]={"CGVirtualDisplay","CGVirtualDisplayDescriptor","CGVirtualDisplaySettings","CGVirtualDisplayMode"};
    for(auto name:classes)if(!NSClassFromString([NSString stringWithUTF8String:name]))return false;
    return [NSClassFromString(@"CGVirtualDisplay") instancesRespondToSelector:@selector(initWithDescriptor:)] && [NSClassFromString(@"CGVirtualDisplay") instancesRespondToSelector:@selector(applySettings:)];
}
NativeDisplay::NativeDisplay(unsigned width,unsigned height,unsigned hz,unsigned slot){
    if(!VirtualDisplay::available())throw std::runtime_error("This macOS version does not expose the required virtual-display API");
    if(width<640 || height<480 || width>4096 || height>4096 || uint64_t(width)*height>8847360 || !hz || hz>60 || slot>3)throw std::runtime_error("Unsupported virtual display dimensions");
    @try {
        id descriptor=[NSClassFromString(@"CGVirtualDisplayDescriptor") new];
        [descriptor setName:[NSString stringWithFormat:@"ExtraPane %u",slot+1]];[descriptor setQueue:dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE,0)];
        [descriptor setMaxPixelsWide:4096];[descriptor setMaxPixelsHigh:4096];[descriptor setSizeInMillimeters:CGSizeMake(width*25.4/110,height*25.4/110)];
        [descriptor setVendorID:0x4545];[descriptor setProductID:0x4550];[descriptor setSerialNum:slot+1];
        if([descriptor respondsToSelector:@selector(setSerialNumber:)])[descriptor setSerialNumber:slot+1];
        object_=[[NSClassFromString(@"CGVirtualDisplay") alloc]initWithDescriptor:descriptor];
        if(!object_)throw std::runtime_error("macOS refused to create a virtual monitor");
        id settings=[NSClassFromString(@"CGVirtualDisplaySettings") new];[settings setHiDPI:0];
        NSMutableArray* modes=[NSMutableArray new];
        auto add=[&](unsigned w,unsigned h){id mode=[[NSClassFromString(@"CGVirtualDisplayMode") alloc]initWithWidth:w height:h refreshRate:hz];if(mode)[modes addObject:mode];};
        add(width,height);if(width!=1920 || height!=1080)add(1920,1080);if(width!=1280 || height!=720)add(1280,720);
        [settings setModes:modes];if(![object_ applySettings:settings])throw std::runtime_error("macOS rejected the virtual-monitor modes");id_=[object_ displayID];
        for(unsigned retry=0;retry<100 && !displayOnline(id_);++retry)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(!id_ || !displayOnline(id_))throw std::runtime_error("Virtual monitor did not become available");
        // Only this newly created display is changed; keep the user's existing layout.
        if(CGDisplayIsInMirrorSet(id_)){CGDisplayConfigRef config=nullptr;if(CGBeginDisplayConfiguration(&config)==kCGErrorSuccess){CGConfigureDisplayMirrorOfDisplay(config,id_,kCGNullDirectDisplay);CGCompleteDisplayConfiguration(config,kCGConfigureForSession);}}
        if(CGDisplayIsInMirrorSet(id_) || CGDisplayIsMain(id_))throw std::runtime_error("Virtual monitor is not an independent secondary display");
    } @catch(NSException* exception){object_=nil;throw std::runtime_error(std::string("Virtual-display API incompatible: ")+exception.reason.UTF8String);}
}
int displayOwner(unsigned width,unsigned height,unsigned hz,unsigned slot){@autoreleasepool{try{
    signal(SIGTERM,SIG_DFL);
    NativeDisplay display(width,height,hz,slot);printf("%u\n",display.id_);fflush(stdout);
    char value;while(read(STDIN_FILENO,&value,1)>0){};
    // Process lifetime is the ownership boundary. macOS can defer removal when
    // the last CGVirtualDisplay object is merely released in a live process.
    _Exit(0);
}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}}}
VirtualDisplay::VirtualDisplay(unsigned width,unsigned height,unsigned hz,unsigned slot){
    try{process_=[NSTask new];control_=[NSPipe pipe];NSPipe* output=[NSPipe pipe];process_.executableURL=NSBundle.mainBundle.executableURL;
        process_.arguments=@[@"--virtual-display-owner",@(width).stringValue,@(height).stringValue,@(hz).stringValue,@(slot).stringValue];process_.standardInput=control_;process_.standardOutput=output;
        NSError* error=nil;if(![process_ launchAndReturnError:&error])throw std::runtime_error(error.localizedDescription.UTF8String);
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);std::string line;
        while(line.find('\n')==std::string::npos){if(std::chrono::steady_clock::now()>deadline || !process_.running)throw std::runtime_error("Virtual-monitor helper did not start");pollfd fd{output.fileHandleForReading.fileDescriptor,POLLIN,0};if(poll(&fd,1,100)>0){char bytes[32];auto n=read(fd.fd,bytes,sizeof(bytes));if(n<=0)throw std::runtime_error("Virtual-monitor helper ended");line.append(bytes,size_t(n));if(line.size()>32)throw std::runtime_error("Invalid virtual-monitor helper output");}}
        id_=unsigned(std::stoul(line));if(!id_ || !displayOnline(id_) || CGDisplayIsMain(id_) || CGDisplayIsInMirrorSet(id_))throw std::runtime_error("Virtual monitor is not an independent secondary display");
    }catch(...){close();throw;}
}
void VirtualDisplay::close() noexcept {
    @try{[control_.fileHandleForWriting closeFile];auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(process_.running && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(process_.running)[process_ terminate];
        process_=nil;control_=nil;
        deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(id_ && displayOnline(id_) && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }@catch(NSException*){}
}
VirtualDisplay::~VirtualDisplay(){close();}
int displayCheck(){@autoreleasepool{try{
    uint32_t before=0,after=0;CGGetOnlineDisplayList(0,nullptr,&before);CGDirectDisplayID created=0;
    @autoreleasepool {VirtualDisplay monitor(1280,720,60,0);created=monitor.displayID();CGGetOnlineDisplayList(0,nullptr,&after);auto bounds=CGDisplayBounds(created);
        printf("VIRTUAL_MONITOR id=%u online=%d primary=%d mirrored=%d count=%u->%u bounds=%.0f,%.0f %.0fx%.0f\n",created,CGDisplayIsOnline(created),CGDisplayIsMain(created),CGDisplayIsInMirrorSet(created),before,after,bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height);fflush(stdout);
        if(after!=before+1)throw std::runtime_error("Virtual monitor enumeration count did not increase");std::this_thread::sleep_for(std::chrono::seconds(3));}
    for(unsigned retry=0;retry<100 && displayOnline(created);++retry)CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.05,false);
    CGGetOnlineDisplayList(0,nullptr,&after);printf("CLEANUP online=%d count=%u\n",displayOnline(created),after);if(displayOnline(created) || before!=after)throw std::runtime_error("Virtual monitor cleanup failed");puts("VIRTUAL_MONITOR_REMOVED");return 0;
}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}}}
}
