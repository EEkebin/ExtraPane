#include <windows.h>
#include <dm/camera_resource.hpp>
#include <stdexcept>
namespace dm::camera {
SourceResource::SourceResource(std::wstring device,Format format,Sink sink,std::function<void()> cleanup)
    :device_(std::move(device)),format_(format),sink_(std::move(sink)),worker_cleanup_(std::move(cleanup)){
    validate_format(format_);if(device_.empty()||!sink_)throw std::invalid_argument("Camera source needs a local device and native sink");
}
SourceResource::~SourceResource(){stop();}
extrapane::Availability SourceResource::prepare(){
    if(capture_)return {false,"Camera source already prepared"};
    try{capture_=std::make_unique<Capture>(device_,format_);failed_=false;stopping_=false;return {true,{}};}
    catch(...){return {false,"Camera could not open the selected mode; check permission, device availability and format support"};}
}
extrapane::Availability SourceResource::start(){
    if(!capture_||worker_.joinable())return {false,"Camera source is not ready to start"};
    try{worker_=std::thread([this]{
        const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(initialized)){failed_=true;capture_->stop();return;}
        try{
            while(!stopping_){auto sample=capture_->read(std::chrono::seconds(2));if(!stopping_&&sample.native)sink_(std::move(sample));}
        }catch(...){if(!stopping_)failed_=true;}
        capture_->stop();
        try{if(worker_cleanup_)worker_cleanup_();}catch(...){failed_=true;}
        CoUninitialize();
    });return {true,{}};}
    catch(...){return {false,"Camera worker could not start"};}
}
void SourceResource::stop()noexcept{
    stopping_=true;if(capture_)capture_->stop();
    if(worker_.joinable())worker_.join();
    capture_.reset();
}
}
