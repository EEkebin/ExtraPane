#include "resource_capture.hpp"
#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>

namespace {
using namespace dm::mac::resource;
using namespace std::chrono_literals;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
std::string text(NSString* value){return value.UTF8String?value.UTF8String:"Unknown capture error";}
AVMediaType mediaType(Kind kind){return kind==Kind::Camera?AVMediaTypeVideo:AVMediaTypeAudio;}
struct CameraMode {AVCaptureDeviceFormat* format=nil;unsigned width=0,height=0,fps=0;};
CameraMode modeFor(AVCaptureDevice* device,const Choice* requested=nullptr){
    CameraMode best;std::uint64_t bestScore=0;
    for(AVCaptureDeviceFormat* format in device.formats){
        const auto size=CMVideoFormatDescriptionGetDimensions(format.formatDescription);
        if(size.width<2||size.height<2||size.width>3840||size.height>2160||size.width%2||size.height%2)continue;
        for(AVFrameRateRange* range in format.videoSupportedFrameRateRanges){
            unsigned fps=static_cast<unsigned>(std::floor(std::min(30.0,range.maxFrameRate)));
            if(requested)fps=requested->fps;
            if(fps<1||fps>60||fps<range.minFrameRate||fps>range.maxFrameRate)continue;
            if(requested&&(requested->width!=static_cast<unsigned>(size.width)||requested->height!=static_cast<unsigned>(size.height)))continue;
            const auto pixels=static_cast<std::uint64_t>(size.width)*size.height;
            const auto score=pixels<=1920*1080?pixels+10000000:10000000-pixels;
            if(score>bestScore){bestScore=score;best={format,static_cast<unsigned>(size.width),static_cast<unsigned>(size.height),fps};}
        }
    }return best;
}
struct Shared {
    std::mutex mutex;std::condition_variable changed;std::deque<Packet> packets;
    std::size_t bytes=0;std::string error;std::atomic_bool stopping=false;std::atomic_uint pending=0;
    std::function<bool(std::int64_t)> muted;
    void fail(std::string reason){std::lock_guard lock(mutex);if(error.empty())error=std::move(reason);changed.notify_all();}
    void push(Packet packet){
        std::lock_guard lock(mutex);if(stopping||!error.empty())return;
        if(packet.bytes.empty()||packet.bytes.size()>2*1024*1024||packets.size()>=32||packet.bytes.size()>8*1024*1024-bytes){error="Capture recipient cannot keep up; reconnect this resource";packets.clear();bytes=0;}
        else{bytes+=packet.bytes.size();packets.push_back(std::move(packet));}changed.notify_all();
    }
};
std::int64_t timestamp(CMTime time){require(CMTIME_IS_NUMERIC(time),"Capture timestamp is invalid");return CMTimeConvertScale(time,10000000,kCMTimeRoundingMethod_Default).value;}
void encoded(void* context,void*,OSStatus status,VTEncodeInfoFlags flags,CMSampleBufferRef sample){
    auto& shared=*static_cast<Shared*>(context);--shared.pending;if(shared.stopping)return;
    try{
        require(status==noErr&&sample&&!(flags&kVTEncodeInfo_FrameDropped),"H.264 encoding failed");
        Packet packet;packet.timestamp100ns=timestamp(CMSampleBufferGetPresentationTimeStamp(sample));packet.keyframe=true;
        auto attachments=CMSampleBufferGetSampleAttachmentsArray(sample,false);
        if(attachments&&CFArrayGetCount(attachments)){auto info=static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments,0));packet.keyframe=CFDictionaryGetValue(info,kCMSampleAttachmentKey_NotSync)!=kCFBooleanTrue;}
        auto append=[&](const std::uint8_t* bytes,std::size_t size){require(size<=2*1024*1024&&packet.bytes.size()+size+4<=2*1024*1024,"H.264 frame exceeds protocol bounds");packet.bytes.insert(packet.bytes.end(),{0,0,0,1});packet.bytes.insert(packet.bytes.end(),bytes,bytes+size);};
        auto format=CMSampleBufferGetFormatDescription(sample);size_t count=0;int length=0;
        require(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,0,nullptr,nullptr,&count,&length)==noErr&&length==4&&count<=16,"Invalid H.264 configuration");
        for(size_t i=0;i<count;++i){const std::uint8_t* bytes=nullptr;size_t size=0;require(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,i,&bytes,&size,nullptr,nullptr)==noErr,"Missing H.264 parameter set");append(bytes,size);}
        auto block=CMSampleBufferGetDataBuffer(sample);require(block,"Missing H.264 output");const auto size=CMBlockBufferGetDataLength(block);
        require(size>0&&size<=2*1024*1024,"Invalid H.264 output size");std::vector<std::uint8_t> bytes(size);require(CMBlockBufferCopyDataBytes(block,0,size,bytes.data())==noErr,"Cannot copy H.264 output");
        for(size_t i=0;i<bytes.size();){require(bytes.size()-i>=4,"Truncated H.264 length");std::uint32_t length=0;for(int j=0;j<4;++j)length=(length<<8)|bytes[i++];require(length>0&&length<=bytes.size()-i,"Invalid H.264 unit");append(bytes.data()+i,length);i+=length;}
        shared.push(std::move(packet));
    }catch(const std::exception& error){shared.fail(error.what());}catch(...){shared.fail("H.264 callback failed");}
}
}

@interface ExtraPaneResourceSink:NSObject<AVCaptureVideoDataOutputSampleBufferDelegate,AVCaptureAudioDataOutputSampleBufferDelegate> {
@public std::shared_ptr<Shared> shared;VTCompressionSessionRef encoder;BOOL camera;CMTime previousAudioEnd;unsigned width,height;
    __weak AVCaptureSession* captureSession;
}
@end
@implementation ExtraPaneResourceSink
- (void)captureOutput:(AVCaptureOutput*)output didOutputSampleBuffer:(CMSampleBufferRef)sample fromConnection:(AVCaptureConnection*)connection {
    (void)output;(void)connection;if(shared->stopping)return;
    try{
        require(CMSampleBufferIsValid(sample)&&CMSampleBufferDataIsReady(sample),"Capture sample is unavailable");
        if(camera){
            if(shared->pending>=3)return; // Drop unencoded input, never encoded dependencies.
            auto pixels=CMSampleBufferGetImageBuffer(sample);require(pixels,"Missing camera pixels");
            require(CVPixelBufferGetWidth(pixels)==width&&CVPixelBufferGetHeight(pixels)==height,"Camera changed the negotiated resolution");
            ++shared->pending;
            const auto result=VTCompressionSessionEncodeFrame(encoder,pixels,CMSampleBufferGetPresentationTimeStamp(sample),CMSampleBufferGetDuration(sample),nullptr,nullptr,nullptr);
            if(result!=noErr){--shared->pending;shared->fail("Cannot submit camera frame for H.264 encoding");}
        }else{
            auto format=CMSampleBufferGetFormatDescription(sample);auto* audio=CMAudioFormatDescriptionGetStreamBasicDescription(format);
            require(audio&&audio->mFormatID==kAudioFormatLinearPCM&&audio->mSampleRate==48000&&audio->mChannelsPerFrame==1&&audio->mBitsPerChannel==16&&audio->mBytesPerFrame==2&&(audio->mFormatFlags&kAudioFormatFlagIsSignedInteger)&&!(audio->mFormatFlags&(kAudioFormatFlagIsFloat|kAudioFormatFlagIsBigEndian|kAudioFormatFlagIsNonInterleaved)),"Microphone did not supply PCM16 little-endian 48 kHz mono");
            auto block=CMSampleBufferGetDataBuffer(sample);require(block,"Missing microphone samples");auto size=CMBlockBufferGetDataLength(block);require(size>0&&size<=192000&&size%2==0,"Invalid microphone sample size");
            Packet packet;packet.timestamp100ns=timestamp(CMSampleBufferGetPresentationTimeStamp(sample));packet.bytes.resize(size);
            require(CMBlockBufferCopyDataBytes(block,0,size,packet.bytes.data())==noErr,"Cannot read microphone samples");
            // Delegate delivery can lag behind capture, including across a full
            // mute/unmute cycle. Compare capture time, not callback arrival time.
            auto clock=captureSession.masterClock;require(clock,"Microphone capture clock is unavailable");
            const auto hostStart=CMSyncConvertTime(CMSampleBufferGetPresentationTimeStamp(sample),clock,CMClockGetHostTimeClock());
            packet.muted=shared->muted&&shared->muted(timestamp(hostStart));
            if(packet.muted)std::fill(packet.bytes.begin(),packet.bytes.end(),0);
            const auto start=CMSampleBufferGetPresentationTimeStamp(sample);
            packet.discontinuity=!CMTIME_IS_NUMERIC(previousAudioEnd)||std::abs(CMTimeGetSeconds(CMTimeSubtract(start,previousAudioEnd)))>0.002;
            previousAudioEnd=CMTimeAdd(start,CMTimeMake(size/2,48000));shared->push(std::move(packet));
        }
    }catch(const std::exception& error){shared->fail(error.what());}catch(...){shared->fail("Capture callback failed");}
}
@end

namespace dm::mac::resource {
std::vector<Choice> enumerate(Kind kind){
    @autoreleasepool {
        std::vector<Choice> choices;const auto authorization=[AVCaptureDevice authorizationStatusForMediaType:mediaType(kind)];
        for(AVCaptureDevice* device in [AVCaptureDevice devicesWithMediaType:mediaType(kind)]){
            Choice choice;choice.kind=kind;choice.id=text(device.uniqueID);choice.label=text(device.localizedName);choice.available=device.connected&&!device.suspended;
            if(!choice.available)choice.reason="Capture device is disconnected or suspended";
            if(kind==Kind::Camera){const auto mode=modeFor(device);choice.width=mode.width;choice.height=mode.height;choice.fps=mode.fps;if(!mode.format){choice.available=false;choice.reason="No compatible camera format up to 3840 x 2160 at 60 FPS";}}
            if(authorization==AVAuthorizationStatusDenied||authorization==AVAuthorizationStatusRestricted){choice.available=false;choice.reason="Allow ExtraPane in Privacy & Security settings";}
            choices.push_back(std::move(choice));
        }return choices;
    }
}
void requestPermission(Kind kind,std::function<void(bool,std::string)> callback){
    const auto type=mediaType(kind);const auto status=[AVCaptureDevice authorizationStatusForMediaType:type];
    if(status==AVAuthorizationStatusAuthorized){callback(true,{});return;}
    if(status!=AVAuthorizationStatusNotDetermined){callback(false,"Allow ExtraPane camera or microphone access in Privacy & Security settings");return;}
    [AVCaptureDevice requestAccessForMediaType:type completionHandler:^(BOOL granted){callback(granted,granted?std::string{}:"Camera or microphone access was denied");}];
}
struct Capture::State {
    Choice choice;std::shared_ptr<Shared> shared=std::make_shared<Shared>();
    AVCaptureSession* session=nil;AVCaptureDevice* device=nil;AVCaptureDeviceInput* input=nil;AVCaptureOutput* output=nil;
    ExtraPaneResourceSink* sink=nil;AVCaptureConnection* connection=nil;id errorObserver=nil;id interruptionObserver=nil;
    VTCompressionSessionRef encoder=nullptr;dispatch_queue_t queue=dispatch_queue_create("io.github.EEkebin.ExtraPane.resource.capture",DISPATCH_QUEUE_SERIAL);
    bool configuring=false,deviceLocked=false;
    ~State(){
        shared->stopping=true;shared->changed.notify_all();
        if(errorObserver)[NSNotificationCenter.defaultCenter removeObserver:errorObserver];
        if(interruptionObserver)[NSNotificationCenter.defaultCenter removeObserver:interruptionObserver];
        @try {if(configuring)[session commitConfiguration];[session stopRunning];} @catch(NSException* exception) {(void)exception;}
        if(deviceLocked){[device unlockForConfiguration];deviceLocked=false;}
        if([output isKindOfClass:AVCaptureVideoDataOutput.class])[(AVCaptureVideoDataOutput*)output setSampleBufferDelegate:nil queue:nullptr];
        if([output isKindOfClass:AVCaptureAudioDataOutput.class])[(AVCaptureAudioDataOutput*)output setSampleBufferDelegate:nil queue:nullptr];
        dispatch_sync(queue,^{});
        if(encoder){VTCompressionSessionCompleteFrames(encoder,kCMTimeInvalid);VTCompressionSessionInvalidate(encoder);CFRelease(encoder);encoder=nullptr;}
        sink=nil;session=nil;
    }
};
Capture::Capture(Choice choice,std::function<bool(std::int64_t)> muted):state_(std::make_unique<State>()){
    @autoreleasepool { @try {
        require(!NSThread.isMainThread,"Capture must start on a worker thread");auto& s=*state_;s.choice=std::move(choice);s.shared->muted=std::move(muted);
        require([AVCaptureDevice authorizationStatusForMediaType:mediaType(s.choice.kind)]==AVAuthorizationStatusAuthorized,"Camera or microphone permission is required");
        for(AVCaptureDevice* device in [AVCaptureDevice devicesWithMediaType:mediaType(s.choice.kind)])if(text(device.uniqueID)==s.choice.id){require(!s.device,"Capture device identity is ambiguous");s.device=device;}
        require(s.device&&s.device.connected&&!s.device.suspended,"Selected capture device is no longer available");
        NSError* error=nil;s.input=[AVCaptureDeviceInput deviceInputWithDevice:s.device error:&error];if(!s.input)throw std::runtime_error(text(error.localizedDescription));
        s.session=[AVCaptureSession new];s.sink=[ExtraPaneResourceSink new];s.sink->shared=s.shared;s.sink->camera=s.choice.kind==Kind::Camera;s.sink->previousAudioEnd=kCMTimeInvalid;
        s.sink->captureSession=s.session;
        [s.session beginConfiguration];s.configuring=true;require([s.session canAddInput:s.input],"Cannot add selected capture input");[s.session addInput:s.input];
        if(s.choice.kind==Kind::Camera){
            const bool requested=s.choice.width||s.choice.height||s.choice.fps;const auto mode=modeFor(s.device,requested?&s.choice:nullptr);require(mode.format,"Selected camera mode is unavailable");
            s.choice.width=mode.width;s.choice.height=mode.height;s.choice.fps=mode.fps;
            s.sink->width=mode.width;s.sink->height=mode.height;
            require([s.device lockForConfiguration:&error],"Cannot configure selected camera");
            // macOS has no InputPriority preset. Retaining the configuration
            // lock prevents session startup/topology changes overriding this
            // exact mode. Release after stopping, including failed construction.
            s.deviceLocked=true;
            s.device.activeFormat=mode.format;s.device.activeVideoMinFrameDuration=CMTimeMake(1,mode.fps);s.device.activeVideoMaxFrameDuration=CMTimeMake(1,mode.fps);
            AVCaptureVideoDataOutput* video=[AVCaptureVideoDataOutput new];video.alwaysDiscardsLateVideoFrames=YES;
            video.videoSettings=@{(NSString*)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)};
            [video setSampleBufferDelegate:s.sink queue:s.queue];s.output=video;
            require(VTCompressionSessionCreate(kCFAllocatorDefault,mode.width,mode.height,kCMVideoCodecType_H264,nullptr,nullptr,nullptr,encoded,s.shared.get(),&s.encoder)==noErr,"H.264 encoder is unavailable");
            auto property=[&](CFStringRef key,CFTypeRef value){require(VTSessionSetProperty(s.encoder,key,value)==noErr,"H.264 encoder rejected low-latency settings");};
            property(kVTCompressionPropertyKey_RealTime,kCFBooleanTrue);property(kVTCompressionPropertyKey_AllowFrameReordering,kCFBooleanFalse);property(kVTCompressionPropertyKey_ProfileLevel,kVTProfileLevel_H264_Main_AutoLevel);
            NSNumber* fps=@(mode.fps);NSNumber* bitrate=@8000000;property(kVTCompressionPropertyKey_ExpectedFrameRate,(__bridge CFNumberRef)fps);property(kVTCompressionPropertyKey_MaxKeyFrameInterval,(__bridge CFNumberRef)fps);property(kVTCompressionPropertyKey_AverageBitRate,(__bridge CFNumberRef)bitrate);
            require(VTCompressionSessionPrepareToEncodeFrames(s.encoder)==noErr,"H.264 encoder could not prepare");s.sink->encoder=s.encoder;
        }else{
            AVCaptureAudioDataOutput* audio=[AVCaptureAudioDataOutput new];audio.audioSettings=@{AVFormatIDKey:@(kAudioFormatLinearPCM),AVSampleRateKey:@48000,AVNumberOfChannelsKey:@1,AVLinearPCMBitDepthKey:@16,AVLinearPCMIsFloatKey:@NO,AVLinearPCMIsBigEndianKey:@NO,AVLinearPCMIsNonInterleaved:@NO};
            [audio setSampleBufferDelegate:s.sink queue:s.queue];s.output=audio;
        }
        require([s.session canAddOutput:s.output],"Selected capture output is unavailable");[s.session addOutput:s.output];[s.session commitConfiguration];s.configuring=false;
        if(s.choice.kind==Kind::Camera){
            const auto actual=CMVideoFormatDescriptionGetDimensions(s.device.activeFormat.formatDescription);
            require(actual.width==static_cast<int>(s.choice.width)&&actual.height==static_cast<int>(s.choice.height)&&CMTimeCompare(s.device.activeVideoMinFrameDuration,CMTimeMake(1,s.choice.fps))==0&&CMTimeCompare(s.device.activeVideoMaxFrameDuration,CMTimeMake(1,s.choice.fps))==0,"Camera session changed the negotiated format or frame rate");
        }
        s.connection=[s.output connectionWithMediaType:mediaType(s.choice.kind)];
        if(s.choice.kind==Kind::Camera&&s.connection.isVideoMirroringSupported){s.connection.automaticallyAdjustsVideoMirroring=NO;s.connection.videoMirrored=NO;}
        const auto shared=s.shared;
        s.errorObserver=[NSNotificationCenter.defaultCenter addObserverForName:AVCaptureSessionRuntimeErrorNotification object:s.session queue:nil usingBlock:^(NSNotification* event){NSError* error=event.userInfo[AVCaptureSessionErrorKey];shared->fail(text(error.localizedDescription));}];
        s.interruptionObserver=[NSNotificationCenter.defaultCenter addObserverForName:AVCaptureSessionWasInterruptedNotification object:s.session queue:nil usingBlock:^(NSNotification* event){(void)event;shared->fail("Capture was interrupted or permission was withdrawn");}];
        [s.session startRunning];require(s.session.running,"Capture could not start");
    } @catch(NSException* exception){throw std::runtime_error(text(exception.reason));} }
}
Capture::~Capture()=default;
const Choice& Capture::choice()const{return state_->choice;}
Packet Capture::nextPacket(const std::function<bool()>& cancelled){
    auto& s=*state_->shared;std::unique_lock lock(s.mutex);const auto until=std::chrono::steady_clock::now()+10s;
    while(s.packets.empty()&&s.error.empty()&&!s.stopping&&!cancelled()&&std::chrono::steady_clock::now()<until)s.changed.wait_for(lock,50ms);
    require(!cancelled()&&!s.stopping,"Capture stopped");if(!s.error.empty())throw std::runtime_error(s.error);require(!s.packets.empty(),"Capture produced no media; check device and permissions");
    auto packet=std::move(s.packets.front());s.packets.pop_front();s.bytes-=packet.bytes.size();return packet;
}
void Capture::setMirrored(bool enabled){
    auto& s=*state_;require(s.choice.kind==Kind::Camera,"Only camera capture can be mirrored");
    require(s.connection.isVideoMirroringSupported||!enabled,"This camera connection does not support mirroring");
    if(!s.connection.isVideoMirroringSupported)return;
    AVCaptureConnection* connection=s.connection;
    dispatch_sync(s.queue,^{connection.automaticallyAdjustsVideoMirroring=NO;connection.videoMirrored=enabled;});
}
}
