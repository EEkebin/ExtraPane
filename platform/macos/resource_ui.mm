#include "resource_ui.hpp"
#include "resource_capture.hpp"
#include "transport.hpp"
#include <dm/access.hpp>
#include <algorithm>
#include <memory>

using namespace dm::mac;
namespace capture=dm::mac::resource;
static NSString* text(const std::string& value){return [NSString stringWithUTF8String:value.c_str()]?:@"";}
static NSButton* action(NSString* title,id target,SEL selector,bool red=false){auto* b=[NSButton buttonWithTitle:title target:target action:selector];if(red)b.contentTintColor=NSColor.systemRedColor;return b;}
static NSTextField* field(NSString* hint,NSString* value){auto* f=[NSTextField textFieldWithString:value];f.placeholderString=hint;return f;}
static unsigned portValue(NSString* value){if(!value.length||[value rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet].location!=NSNotFound||value.integerValue<1||value.integerValue>65535)return 0;return unsigned(value.integerValue);}
static void invalid(NSView* view){view.wantsLayer=YES;view.layer.borderWidth=2;view.layer.borderColor=NSColor.systemRedColor.CGColor;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),^{view.layer.borderWidth=0;});}
@interface EPResourceStack : NSStackView
@end
@implementation EPResourceStack
- (BOOL)isFlipped{return YES;}
@end
@class EPResourceRow;
@interface EPResourcePanel () {
@public std::function<ResourceOptions()> targetProvider;
}
@property(nonatomic,strong) NSView* view;
@property(nonatomic,strong) NSStackView* cameras;
@property(nonatomic,strong) NSStackView* microphones;
@property(nonatomic,strong) NSButton* single;
@property(nonatomic,strong) NSTextField* notice;
@property(nonatomic,strong) NSMutableArray<EPResourceRow*>* rows;
@property(nonatomic,copy) void (^changed)(void);
@property(nonatomic) NSUInteger noticeGeneration;
- (void)notify:(NSString*)message;
- (void)update;
@end
@interface EPResourceRow : NSObject<NSMenuDelegate> {
@public std::shared_ptr<ResourceSession> session;
    std::vector<capture::Choice> devices;
}
@property(nonatomic,weak) EPResourcePanel* owner;
@property(nonatomic) BOOL camera;
@property(nonatomic) BOOL pending;
@property(nonatomic) BOOL stopping;
@property(nonatomic) NSUInteger generation;
@property(nonatomic,strong) NSStackView* view;
@property(nonatomic,strong) NSStackView* configuration;
@property(nonatomic,strong) NSStackView* destination;
@property(nonatomic,strong) NSButton* enabled;
@property(nonatomic,strong) NSButton* disconnect;
@property(nonatomic,strong) NSButton* remove;
@property(nonatomic,strong) NSButton* mute;
@property(nonatomic,strong) NSPopUpButton* source;
@property(nonatomic,strong) NSTextField* address;
@property(nonatomic,strong) NSTextField* port;
@property(nonatomic,strong) NSSecureTextField* code;
@property(nonatomic,strong) NSTextField* summary;
@property(nonatomic,strong) NSTextField* status;
- (instancetype)initWithOwner:(EPResourcePanel*)owner camera:(BOOL)camera;
- (void)render;
- (void)stop:(id)sender;
@end
@implementation EPResourceRow
- (instancetype)initWithOwner:(EPResourcePanel*)owner camera:(BOOL)camera {
    if((self=[super init])){
        self.owner=owner;self.camera=camera;self.view=[NSStackView stackViewWithViews:@[]];self.view.orientation=NSUserInterfaceLayoutOrientationVertical;self.view.alignment=NSLayoutAttributeLeading;self.view.spacing=6;
        self.enabled=[NSButton checkboxWithTitle:camera?@"Camera":@"Microphone" target:self action:@selector(toggle:)];
        self.disconnect=action(@"Disconnect",self,@selector(stop:),true);self.remove=action(@"Remove",self,@selector(removeRow:));
        self.mute=[NSButton checkboxWithTitle:@"Mute" target:self action:@selector(muteChanged:)];self.mute.hidden=camera;
        [self.view addArrangedSubview:[NSStackView stackViewWithViews:@[self.enabled,self.mute,self.disconnect,self.remove]]];
        self.source=[[NSPopUpButton alloc]initWithFrame:NSZeroRect pullsDown:NO];self.source.menu.delegate=self;self.source.menu.autoenablesItems=NO;[self.source addItemWithTitle:@"Select a source…"];
        self.address=field(@"Server IPv4 address",@"");self.port=field(@"TCP port",@"43824");self.code=[NSSecureTextField new];self.code.placeholderString=@"Access code (optional)";
        [self.address.widthAnchor constraintGreaterThanOrEqualToConstant:150].active=YES;[self.port.widthAnchor constraintEqualToConstant:75].active=YES;[self.code.widthAnchor constraintGreaterThanOrEqualToConstant:130].active=YES;
        self.destination=[NSStackView stackViewWithViews:@[self.address,self.port,self.code]];
        self.configuration=[NSStackView stackViewWithViews:@[self.source,self.destination]];self.configuration.orientation=NSUserInterfaceLayoutOrientationVertical;self.configuration.alignment=NSLayoutAttributeLeading;[self.view addArrangedSubview:self.configuration];
        self.summary=[NSTextField wrappingLabelWithString:@""];self.status=[NSTextField wrappingLabelWithString:@""];[self.view addArrangedSubview:self.summary];[self.view addArrangedSubview:self.status];[self render];
    }return self;
}
- (void)menuWillOpen:(NSMenu*)menu {
    (void)menu;NSString* selected=self.source.selectedItem.representedObject;[self.source removeAllItems];devices.clear();[self.source addItemWithTitle:@"Select a source…"];
    try{devices=capture::enumerate(self.camera?capture::Kind::Camera:capture::Kind::Microphone);for(const auto& device:devices){[self.source addItemWithTitle:text(device.label+(device.available?"":" — "+device.reason))];self.source.lastItem.representedObject=text(device.id);self.source.lastItem.enabled=device.available;if([self.source.lastItem.representedObject isEqual:selected])[self.source selectItem:self.source.lastItem];}}
    catch(const std::exception& e){self.status.stringValue=text(e.what());}
}
- (void)render {
    const BOOL active=session!=nullptr||self.pending;self.enabled.state=active&&!self.stopping?NSControlStateValueOn:NSControlStateValueOff;self.enabled.enabled=!self.stopping;
    self.configuration.hidden=active;self.destination.hidden=self.owner.single.state==NSControlStateValueOn;self.summary.hidden=!active;self.disconnect.hidden=!active;self.disconnect.enabled=!self.stopping;self.remove.enabled=!active;self.mute.enabled=session!=nullptr&&!self.stopping;
}
- (void)removeRow:(id)sender{(void)sender;if(session||self.pending)return;[self.view removeFromSuperview];[self.owner.rows removeObject:self];[self.owner update];}
- (void)muteChanged:(id)sender{(void)sender;if(session)session->setMuted(self.mute.state==NSControlStateValueOn);}
- (void)stop:(id)sender{(void)sender;if(self.pending){self.pending=NO;++self.generation;}if(session){self.stopping=YES;self.status.stringValue=@"Stopping…";session->cancel();}[self render];[self.owner update];}
- (void)toggle:(id)sender{
    (void)sender;if(self.enabled.state!=NSControlStateValueOn){[self stop:nil];return;}if(session||self.pending)return;
    try{
        auto options=self.owner->targetProvider();options.kind=self.camera?extrapane::ResourceKind::camera:extrapane::ResourceKind::microphone;
        const bool common=self.owner.single.state==NSControlStateValueOn;
        if(!common){options.target.address=self.address.stringValue.UTF8String;options.target.port=portValue(self.port.stringValue);options.code=self.code.stringValue.UTF8String;}
        bool valid=true;if(!dm::mac::ipv4(options.target.address.c_str())){if(!common)invalid(self.address);valid=false;}if(!options.target.port||options.target.port>65535){if(!common)invalid(self.port);valid=false;}if(!options.code.empty()&&!dm::access::validCode(options.code)){if(!common)invalid(self.code);valid=false;}
        const auto index=self.source.indexOfSelectedItem-1;if(index<0||size_t(index)>=devices.size()||!devices[size_t(index)].available){invalid(self.source);valid=false;}
        if(!valid){if(common&&self.owner.highlightInvalidTarget)self.owner.highlightInvalidTarget();[self.owner notify:common?@"Complete the server address, port, access code and selected source.":@"Check the highlighted connection fields."];[self render];return;}
        const auto selected=devices[size_t(index)];options.sourceId=selected.id;options.width=selected.width;options.height=selected.height;options.fps=selected.fps;
        self.pending=YES;self.stopping=NO;const NSUInteger generation=++self.generation;
        self.summary.stringValue=[NSString stringWithFormat:@"Connecting to %s:%u — %s",options.target.address.c_str(),options.target.port,selected.label.c_str()];self.status.stringValue=@"Waiting for capture permission…";[self render];[self.owner update];
        __weak EPResourceRow* weak=self;
        capture::requestPermission(self.camera?capture::Kind::Camera:capture::Kind::Microphone,[weak,generation,options](bool allowed,std::string reason){dispatch_async(dispatch_get_main_queue(),^{
            EPResourceRow* row=weak;if(!row||row.generation!=generation||!row.pending)return;
            row.pending=NO;if(!allowed){row.status.stringValue=reason.empty()?@"Capture permission was not granted":text(reason);[row render];[row.owner update];return;}
            try{
                ResourceCallbacks callbacks;
                callbacks.status=[weak,generation](std::string value){dispatch_async(dispatch_get_main_queue(),^{EPResourceRow* row=weak;if(row&&row.generation==generation&&!row.stopping)row.status.stringValue=text(value);});};
                const auto destination=options.target;callbacks.connected=[weak,generation,destination](unsigned width,unsigned height,unsigned fps){dispatch_async(dispatch_get_main_queue(),^{EPResourceRow* row=weak;if(!row||row.generation!=generation||row.stopping)return;row.summary.stringValue=[NSString stringWithFormat:@"%s:%u — %@",destination.address.c_str(),destination.port,row.camera?[NSString stringWithFormat:@"%u × %u at %u FPS",width,height,fps]:@"48 kHz mono microphone"];row.status.stringValue=@"";[row.owner notify:[NSString stringWithFormat:@"%@ connected to %s:%u",row.camera?@"Camera":@"Microphone",destination.address.c_str(),destination.port]];});};
                callbacks.ended=[weak,generation](std::string value){dispatch_async(dispatch_get_main_queue(),^{EPResourceRow* row=weak;if(!row||row.generation!=generation)return;row->session.reset();row.stopping=NO;row.status.stringValue=value=="Disconnected"?@"":text(value);[row render];[row.owner update];});};
                row->session=std::make_shared<ResourceSession>(options,std::move(callbacks));row->session->setMuted(row.mute.state==NSControlStateValueOn);row->session->start();row.code.stringValue=@"";
            }catch(const std::exception& error){row->session.reset();row.status.stringValue=text(error.what());}
            [row render];[row.owner update];
        });});
    }catch(const std::exception& error){self.pending=NO;self.status.stringValue=text(error.what());[self render];[self.owner update];}
}
@end
@implementation EPResourcePanel
- (instancetype)initWithTarget:(std::function<ResourceOptions()>)target changed:(void (^)(void))changed {
    if((self=[super init])){
        targetProvider=std::move(target);self.changed=changed;self.rows=[NSMutableArray new];
        auto* stack=[EPResourceStack stackViewWithViews:@[]];stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=10;stack.edgeInsets=NSEdgeInsetsMake(8,8,8,20);
        self.single=[NSButton checkboxWithTitle:@"Use server address above for all resources" target:self action:@selector(layoutChanged:)];self.single.state=NSControlStateValueOn;
        [stack addArrangedSubview:[NSStackView stackViewWithViews:@[self.single,action(@"Disconnect all capture",self,@selector(stopAll),true)]]];
        self.notice=[NSTextField wrappingLabelWithString:@""];[stack addArrangedSubview:self.notice];
        self.cameras=[NSStackView stackViewWithViews:@[]];self.cameras.orientation=NSUserInterfaceLayoutOrientationVertical;self.cameras.alignment=NSLayoutAttributeLeading;
        self.microphones=[NSStackView stackViewWithViews:@[]];self.microphones.orientation=NSUserInterfaceLayoutOrientationVertical;self.microphones.alignment=NSLayoutAttributeLeading;
        [stack addArrangedSubview:[NSStackView stackViewWithViews:@[action(@"Cameras ▾",self,@selector(toggleCameras:)),action(@"+ Add camera",self,@selector(addCamera:)),action(@"Disconnect cameras",self,@selector(stopCameras:),true)]]];[stack addArrangedSubview:self.cameras];
        [stack addArrangedSubview:[NSStackView stackViewWithViews:@[action(@"Microphones ▾",self,@selector(toggleMicrophones:)),action(@"+ Add microphone",self,@selector(addMicrophone:)),action(@"Disconnect microphones",self,@selector(stopMicrophones:),true)]]];[stack addArrangedSubview:self.microphones];
        [stack addArrangedSubview:[NSTextField wrappingLabelWithString:@"USB sharing and virtual camera/microphone receiving are not available on macOS yet. Each capture uses the selected network above."]];
        auto* scroll=[NSScrollView new];scroll.hasVerticalScroller=YES;scroll.drawsBackground=NO;scroll.documentView=stack;stack.translatesAutoresizingMaskIntoConstraints=NO;[stack.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active=YES;[scroll.heightAnchor constraintGreaterThanOrEqualToConstant:140].active=YES;auto* preferredHeight=[scroll.heightAnchor constraintEqualToConstant:210];preferredHeight.priority=NSLayoutPriorityDefaultHigh;preferredHeight.active=YES;self.view=scroll;
        [self addCamera:nil];[self addMicrophone:nil];
    }return self;
}
- (void)addCamera:(id)sender{(void)sender;auto* row=[[EPResourceRow alloc]initWithOwner:self camera:YES];[self.rows addObject:row];[self.cameras addArrangedSubview:row.view];}
- (void)addMicrophone:(id)sender{(void)sender;auto* row=[[EPResourceRow alloc]initWithOwner:self camera:NO];[self.rows addObject:row];[self.microphones addArrangedSubview:row.view];}
- (void)toggleCameras:(NSButton*)sender{self.cameras.hidden=!self.cameras.hidden;sender.title=self.cameras.hidden?@"Cameras ▸":@"Cameras ▾";}
- (void)toggleMicrophones:(NSButton*)sender{self.microphones.hidden=!self.microphones.hidden;sender.title=self.microphones.hidden?@"Microphones ▸":@"Microphones ▾";}
- (void)stopCameras:(id)sender{(void)sender;for(EPResourceRow* row in self.rows)if(row.camera)[row stop:nil];}
- (void)stopMicrophones:(id)sender{(void)sender;for(EPResourceRow* row in self.rows)if(!row.camera)[row stop:nil];}
- (void)stopAll{for(EPResourceRow* row in self.rows)[row stop:nil];}
- (BOOL)active{for(EPResourceRow* row in self.rows)if(row.pending||row->session)return YES;return NO;}
- (void)layoutChanged:(id)sender{(void)sender;for(EPResourceRow* row in self.rows)[row render];}
- (void)update{if(self.changed)self.changed();}
- (void)notify:(NSString*)message{self.notice.stringValue=message;const NSUInteger generation=++self.noticeGeneration;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),^{if(self.noticeGeneration==generation)self.notice.stringValue=@"";});}
@end
