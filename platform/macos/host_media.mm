#include "host_media.hpp"
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <VideoToolbox/VideoToolbox.h>
#include <dm/quality.hpp>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <stdexcept>
using namespace std::chrono_literals;
struct CaptureState {
    std::mutex mutex;std::condition_variable changed;CVPixelBufferRef latest=nullptr;std::string error;std::deque<std::vector<uint8_t>> encoded;
    ~CaptureState(){if(latest)CVPixelBufferRelease(latest);}
};
API_AVAILABLE(macos(13.0))
@interface CaptureSink:NSObject<SCStreamOutput,SCStreamDelegate> { @public std::shared_ptr<CaptureState> shared; }
@end
@implementation CaptureSink
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type {
    (void)stream;if(type!=SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample))return;
    auto attachments=CMSampleBufferGetSampleAttachmentsArray(sample,false);if(!attachments || !CFArrayGetCount(attachments))return;
    NSDictionary* info=(__bridge NSDictionary*)CFArrayGetValueAtIndex(attachments,0);NSNumber* status=info[SCStreamFrameInfoStatus];if(!status || status.integerValue!=SCFrameStatusComplete)return;
    auto pixels=CMSampleBufferGetImageBuffer(sample);if(!pixels)return;std::lock_guard lock(shared->mutex);CVPixelBufferRetain(pixels);if(shared->latest)CVPixelBufferRelease(shared->latest);shared->latest=pixels;shared->changed.notify_all();
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {(void)stream;std::lock_guard lock(shared->mutex);shared->error=error.localizedDescription.UTF8String;shared->changed.notify_all();}
@end
namespace dm::mac {
static void encodeOutput(void* ref,void*,OSStatus error,VTEncodeInfoFlags flags,CMSampleBufferRef sample){
    auto& s=*static_cast<CaptureState*>(ref);std::lock_guard lock(s.mutex);
    try{if(error || !sample || flags&kVTEncodeInfo_FrameDropped)throw std::runtime_error("H.264 encoder failed or dropped a required frame");
        std::vector<uint8_t> packet;auto append=[&](const uint8_t* bytes,size_t n){const uint8_t prefix[]={0,0,0,1};packet.insert(packet.end(),prefix,prefix+4);packet.insert(packet.end(),bytes,bytes+n);};
        auto format=CMSampleBufferGetFormatDescription(sample);size_t count=0;int length=0;
        if(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,0,nullptr,nullptr,&count,&length) || length!=4)throw std::runtime_error("Unexpected H.264 format");
        for(size_t i=0;i<count;++i){const uint8_t* p=nullptr;size_t n=0;if(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,i,&p,&n,nullptr,nullptr))throw std::runtime_error("H.264 parameter set missing");append(p,n);}
        auto block=CMSampleBufferGetDataBuffer(sample);size_t n=CMBlockBufferGetDataLength(block);if(!n || n>2*1024*1024)throw std::runtime_error("Encoded frame exceeds protocol limit");std::vector<uint8_t> bytes(n);
        if(CMBlockBufferCopyDataBytes(block,0,n,bytes.data()))throw std::runtime_error("Cannot read H.264 packet");
        for(size_t i=0;i<bytes.size();){if(i+4>bytes.size())throw std::runtime_error("Invalid H.264 length");uint32_t size=0;for(int j=0;j<4;++j)size=(size<<8)|bytes[i++];if(!size || size>bytes.size()-i)throw std::runtime_error("Invalid H.264 unit");append(bytes.data()+i,size);i+=size;}
        if(packet.size()>2*1024*1024 || s.encoded.size()>=2)throw std::runtime_error("Encoder queue exceeded latency bound");s.encoded.push_back(std::move(packet));
    }catch(const std::exception& e){s.error=e.what();}catch(...){s.error="Encoder callback failed";}s.changed.notify_all();
}
struct HostMedia::State {
    std::shared_ptr<CaptureState> shared=std::make_shared<CaptureState>();CaptureSink* sink=nil;SCStream* stream=nil;VTCompressionSessionRef encoder=nullptr;dispatch_queue_t queue=dispatch_queue_create("io.github.eekebin.extrapane.capture",DISPATCH_QUEUE_SERIAL);
    ~State(){if(stream){dispatch_semaphore_t done=dispatch_semaphore_create(0);[stream stopCaptureWithCompletionHandler:^(NSError*){dispatch_semaphore_signal(done);}];dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC));stream=nil;}
        if(encoder){VTCompressionSessionCompleteFrames(encoder,kCMTimeInvalid);VTCompressionSessionInvalidate(encoder);CFRelease(encoder);}sink=nil;
    }
};
HostMedia::HostMedia(CGDirectDisplayID display,unsigned width,unsigned height,unsigned fps):state_(std::make_unique<State>()){
    auto& s=*state_;s.sink=[CaptureSink new];s.sink->shared=s.shared;
    dispatch_semaphore_t done=dispatch_semaphore_create(0);__block SCShareableContent* content=nil;__block NSError* failure=nil;
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:NO completionHandler:^(SCShareableContent* value,NSError* error){content=value;failure=error;dispatch_semaphore_signal(done);}];
    if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC)))throw std::runtime_error("Screen capture permission/content request timed out");
    if(failure)throw std::runtime_error(failure.localizedDescription.UTF8String);SCDisplay* selected=nil;for(SCDisplay* d in content.displays)if(d.displayID==display){selected=d;break;}if(!selected)throw std::runtime_error("The selected screen is not available for capture");
    SCContentFilter* filter=[[SCContentFilter alloc]initWithDisplay:selected excludingWindows:@[]];SCStreamConfiguration* config=[SCStreamConfiguration new];config.width=width;config.height=height;config.minimumFrameInterval=CMTimeMake(1,fps);config.queueDepth=3;config.pixelFormat=kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;config.showsCursor=YES;config.capturesAudio=NO;
    s.stream=[[SCStream alloc]initWithFilter:filter configuration:config delegate:s.sink];NSError* addError=nil;if(![s.stream addStreamOutput:s.sink type:SCStreamOutputTypeScreen sampleHandlerQueue:s.queue error:&addError])throw std::runtime_error(addError.localizedDescription.UTF8String);
    NSDictionary* options=@{(id)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder:@YES};
    auto error=VTCompressionSessionCreate(kCFAllocatorDefault,width,height,kCMVideoCodecType_H264,(__bridge CFDictionaryRef)options,nullptr,nullptr,encodeOutput,s.shared.get(),&s.encoder);if(error)throw std::runtime_error("Cannot create H.264 encoder");
    VTSessionSetProperty(s.encoder,kVTCompressionPropertyKey_RealTime,kCFBooleanTrue);VTSessionSetProperty(s.encoder,kVTCompressionPropertyKey_AllowFrameReordering,kCFBooleanFalse);VTSessionSetProperty(s.encoder,kVTCompressionPropertyKey_ProfileLevel,kVTProfileLevel_H264_Main_AutoLevel);
    NSNumber* rate=@(video::QualityController(video::Quality::native).bitrate(width,height,fps));VTSessionSetProperty(s.encoder,kVTCompressionPropertyKey_AverageBitRate,(__bridge CFNumberRef)rate);NSNumber* hz=@(fps);VTSessionSetProperty(s.encoder,kVTCompressionPropertyKey_ExpectedFrameRate,(__bridge CFNumberRef)hz);VTSessionSetProperty(s.encoder,kVTCompressionPropertyKey_MaxKeyFrameInterval,(__bridge CFNumberRef)hz);
    if(VTCompressionSessionPrepareToEncodeFrames(s.encoder))throw std::runtime_error("Encoder preparation failed");
    failure=nil;[s.stream startCaptureWithCompletionHandler:^(NSError* error){failure=error;dispatch_semaphore_signal(done);}];if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC)))throw std::runtime_error("Capture startup timed out");if(failure)throw std::runtime_error(failure.localizedDescription.UTF8String);
}
HostMedia::~HostMedia()=default;
std::vector<uint8_t> HostMedia::frame(uint64_t micros,const std::function<bool()>& cancelled){
    auto& s=*state_;CVPixelBufferRef pixels=nullptr;auto deadline=std::chrono::steady_clock::now()+5s;
    {std::unique_lock lock(s.shared->mutex);while(!s.shared->latest && s.shared->error.empty() && !cancelled() && std::chrono::steady_clock::now()<deadline)s.shared->changed.wait_for(lock,50ms);
        if(cancelled())throw std::runtime_error("Server stopped");if(!s.shared->error.empty())throw std::runtime_error(s.shared->error);if(!s.shared->latest)throw std::runtime_error("Virtual monitor produced no frames");pixels=CVPixelBufferRetain(s.shared->latest);}
    auto error=VTCompressionSessionEncodeFrame(s.encoder,pixels,CMTimeMake(micros,1000000),kCMTimeInvalid,nullptr,nullptr,nullptr);CVPixelBufferRelease(pixels);if(error)throw std::runtime_error("H.264 frame submission failed");
    std::unique_lock lock(s.shared->mutex);deadline=std::chrono::steady_clock::now()+1s;
    while(s.shared->encoded.empty() && s.shared->error.empty() && !cancelled() && std::chrono::steady_clock::now()<deadline)s.shared->changed.wait_for(lock,20ms);
    if(cancelled())throw std::runtime_error("Server stopped");if(!s.shared->error.empty())throw std::runtime_error(s.shared->error);if(s.shared->encoded.empty())throw std::runtime_error("H.264 encoder stalled");auto packet=std::move(s.shared->encoded.front());s.shared->encoded.pop_front();return packet;
}
}
