#include "media.hpp"
#include <stdexcept>
namespace dm::mac {
std::vector<std::vector<uint8_t>> annexb(std::span<const uint8_t> b){
    auto prefix=[&](size_t i)->size_t{if(i+3<=b.size() && b[i]==0 && b[i+1]==0){if(b[i+2]==1)return 3;if(i+4<=b.size() && b[i+2]==0 && b[i+3]==1)return 4;}return 0;};
    std::vector<std::vector<uint8_t>> result;size_t i=0;
    while(i<b.size() && !prefix(i)){if(b[i++]!=0)throw std::runtime_error("Expected Annex B H.264");}
    while(i<b.size()){size_t start=i+prefix(i);i=start;while(i<b.size() && !prefix(i))++i;size_t end=i;while(end>start && b[end-1]==0)--end;
        if(end==start)throw std::runtime_error("Empty H.264 NAL unit");result.emplace_back(b.begin()+start,b.begin()+end);
        if(result.size()>1024)throw std::runtime_error("Too many H.264 units");
    }if(result.empty())throw std::runtime_error("Missing H.264 units");return result;
}
void Media::reset(){if(format_)CFRelease(format_);format_=nullptr;sps_.clear();pps_.clear();}
CMSampleBufferRef Media::sample(std::span<const uint8_t> packet,uint64_t micros){
    auto units=annexb(packet);bool changed=false;std::vector<uint8_t> payload;
    for(const auto& unit:units){auto type=unit[0]&31;
        if(type==7){changed|=sps_!=unit;sps_=unit;continue;}if(type==8){changed|=pps_!=unit;pps_=unit;continue;}
        uint32_t n=uint32_t(unit.size());for(int shift=24;shift>=0;shift-=8)payload.push_back(uint8_t(n>>shift));payload.insert(payload.end(),unit.begin(),unit.end());
    }
    if(changed || !format_){if(sps_.empty() || pps_.empty())throw std::runtime_error("H.264 parameter sets missing");
        const uint8_t* sets[]={sps_.data(),pps_.data()};size_t sizes[]={sps_.size(),pps_.size()};CMVideoFormatDescriptionRef next=nullptr;
        if(CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,2,sets,sizes,4,&next))throw std::runtime_error("Unsupported H.264 format");
        if(format_)CFRelease(format_);format_=next;
    }
    if(payload.empty())return nullptr;
    CMBlockBufferRef block=nullptr;CMSampleBufferRef sample=nullptr;
    if(CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,nullptr,payload.size(),kCFAllocatorDefault,nullptr,0,payload.size(),0,&block))throw std::runtime_error("Cannot allocate compressed sample");
    OSStatus error=CMBlockBufferReplaceDataBytes(payload.data(),block,0,payload.size());
    CMSampleTimingInfo timing{kCMTimeInvalid,CMTimeMake(int64_t(micros),1000000),kCMTimeInvalid};size_t size=payload.size();
    if(!error)error=CMSampleBufferCreateReady(kCFAllocatorDefault,block,format_,1,1,&timing,1,&size,&sample);CFRelease(block);
    if(error)throw std::runtime_error("Cannot create H.264 sample");
    auto attachments=CMSampleBufferGetSampleAttachmentsArray(sample,true);
    CFDictionarySetValue((CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments,0),kCMSampleAttachmentKey_DisplayImmediately,kCFBooleanTrue);
    return sample;
}
}
