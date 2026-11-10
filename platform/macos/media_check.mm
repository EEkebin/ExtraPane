#include "media.hpp"
#import <VideoToolbox/VideoToolbox.h>
#include <stdexcept>
#include <cstring>
// Native compressed-sample/decode check; does not open windows or connect to a host.
struct Encoded {
    std::vector<uint8_t> packet;
    OSStatus error = 0;
};
static void encoded(void* context, void*, OSStatus status, VTEncodeInfoFlags, CMSampleBufferRef sample) {
    auto& result = *static_cast<Encoded*>(context);
    result.error = status;
    if (status || !sample) return;
    auto format = CMSampleBufferGetFormatDescription(sample);
    size_t count = 0;
    int length = 0;
    status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, nullptr, nullptr, &count, &length);
    if (status || length != 4) {
        result.error = status ? status : -1;
        return;
    }
    auto append = [&](const uint8_t* data, size_t size) {
        const uint8_t prefix[] = {0, 0, 0, 1};
        result.packet.insert(result.packet.end(), prefix, prefix + 4);
        result.packet.insert(result.packet.end(), data, data + size);
    };
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* bytes = nullptr;
        size_t size = 0;
        status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, i, &bytes, &size, nullptr, nullptr);
        if (status) {
            result.error = status;
            return;
        }
        append(bytes, size);
    }
    auto block = CMSampleBufferGetDataBuffer(sample);
    std::vector<uint8_t> bytes(CMBlockBufferGetDataLength(block));
    status = CMBlockBufferCopyDataBytes(block, 0, bytes.size(), bytes.data());
    if (status) {
        result.error = status;
        return;
    }
    for (size_t i = 0; i < bytes.size();) {
        if (i + 4 > bytes.size()) {
            result.error = -1;
            return;
        }
        uint32_t n = 0;
        for (int j = 0; j < 4; ++j) n = (n << 8) | bytes[i++];
        if (!n || n > bytes.size() - i) {
            result.error = -1;
            return;
        }
        append(bytes.data() + i, n);
        i += n;
    }
}
struct Decoded {
    bool valid = false;
};
static void decoded(void* context, void*, OSStatus error, VTDecodeInfoFlags, CVImageBufferRef image, CMTime, CMTime) {
    if (error || !image || CVPixelBufferGetWidth(image) != 64 || CVPixelBufferGetHeight(image) != 64 ||
        CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_32BGRA)
        return;
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    auto p = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(image));
    static_cast<Decoded*>(context)->valid = p && p[0] > 200 && p[1] < 40 && p[2] > 200;
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
}
void mediaCheck() {
    auto check = [](bool condition) {
        if (!condition) throw std::runtime_error("Native H.264 round-trip check failed");
    };
    Encoded compressed;
    VTCompressionSessionRef encoder = nullptr;
    check(!VTCompressionSessionCreate(kCFAllocatorDefault, 64, 64, kCMVideoCodecType_H264, nullptr, nullptr, nullptr,
                                      encoded, &compressed, &encoder));
    VTSessionSetProperty(encoder, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(encoder, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
    CVPixelBufferRef pixels = nullptr;
    check(!CVPixelBufferCreate(kCFAllocatorDefault, 64, 64, kCVPixelFormatType_32BGRA, nullptr, &pixels));
    CVPixelBufferLockBaseAddress(pixels, 0);
    auto data = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixels));
    for (size_t y = 0; y < 64; ++y)
        for (size_t x = 0; x < 64; ++x) {
            auto p = data + y * CVPixelBufferGetBytesPerRow(pixels) + x * 4;
            p[0] = 255;
            p[1] = 0;
            p[2] = 255;
            p[3] = 255;
        }
    CVPixelBufferUnlockBaseAddress(pixels, 0);
    auto error = VTCompressionSessionEncodeFrame(encoder, pixels, CMTimeMake(0, 60), CMTimeMake(1, 60), nullptr,
                                                 nullptr, nullptr);
    if (!error) error = VTCompressionSessionCompleteFrames(encoder, kCMTimeInvalid);
    CVPixelBufferRelease(pixels);
    VTCompressionSessionInvalidate(encoder);
    CFRelease(encoder);
    check(!error && !compressed.error && !compressed.packet.empty());
    dm::mac::Media media;
    auto sample = media.sample(compressed.packet, 0, 64, 64);
    check(sample != nullptr);
    Decoded output;
    VTDecompressionOutputCallbackRecord callback{decoded, &output};
    VTDecompressionSessionRef decoder = nullptr;
    NSDictionary* attrs = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    error = VTDecompressionSessionCreate(kCFAllocatorDefault, CMSampleBufferGetFormatDescription(sample), nullptr,
                                         (__bridge CFDictionaryRef)attrs, &callback, &decoder);
    if (!error) {
        error = VTDecompressionSessionDecodeFrame(decoder, sample, 0, nullptr, nullptr);
        VTDecompressionSessionWaitForAsynchronousFrames(decoder);
        VTDecompressionSessionInvalidate(decoder);
        CFRelease(decoder);
    }
    CFRelease(sample);
    check(!error && output.valid);
    bool rejected = false;
    try {
        auto wrong = media.sample(compressed.packet, 0, 128, 64);
        if (wrong) CFRelease(wrong);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected);
}
