#include <dm/camera_receiver.hpp>
#include <dm/h264.hpp>
#include <limits>
#include <stdexcept>
namespace dm::camera {
namespace {
class NativeDecoder final:public Decoder {
public:
    explicit NativeDecoder(Format format):decoder_(format.width,format.height,format.fps){}
    void decode(std::span<const std::uint8_t> bytes,std::int64_t timestamp,const Sink& sink)override{decoder_.decode_nv12(bytes,timestamp,sink);}
private:dm::video::H264Decoder decoder_;
};
}
std::unique_ptr<Decoder> native_camera_decoder(Format format){validate_format(format);return std::make_unique<NativeDecoder>(format);}
Receiver::Receiver(Format format,unsigned slot,extrapane::media::Binding binding,std::unique_ptr<CameraRegistration> registration,DecoderFactory factory)
    :format_(format),binding_(binding),endpoint_(format,slot,std::move(registration)),factory_(std::move(factory)){
    if(!binding.resource||!binding.generation||!factory_)throw std::invalid_argument("Camera receiver requires admission binding and decoder factory");
}
Receiver::~Receiver(){stop();}
extrapane::Availability Receiver::prepare(){
    if(stopped_||prepared_)return {false,"Camera receiver already prepared or stopped"};
    try{
        decoder_=factory_(format_);if(!decoder_)throw std::runtime_error("Camera decoder unavailable");
        auto result=endpoint_.prepare();if(!result.available){failed_=true;stop();return result;}
        prepared_=true;return {true,{}};
    }catch(...){failed_=true;stop();return {false,"Camera receiver could not prepare its decoder or endpoint"};}
}
extrapane::Availability Receiver::start(){
    if(!prepared_||running_||stopped_)return {false,"Camera receiver is not ready"};
    auto result=endpoint_.start();if(!result.available){failed_=true;stop();return result;}
    running_=true;return result;
}
void Receiver::stop()noexcept{if(stopped_)return;stopped_=true;running_=false;decoder_.reset();endpoint_.stop();}
void Receiver::receive_one(extrapane::media::Channel& channel){
    if(!running_)throw std::runtime_error("Camera receiver is not running");
    try{accept(channel.receive());}catch(...){failed_=true;stop();throw;}
}
void Receiver::accept(const extrapane::media::Frame& frame){
    if(!running_)throw std::runtime_error("Camera receiver is not running");
    try{
        if(frame.binding!=binding_||frame.payload.empty()||frame.payload.size()>extrapane::media::max_frame_bytes||
           (!sequence_&&frame.sequence!=0)||(sequence_&&(*sequence_==UINT64_MAX||frame.sequence!=*sequence_+1)))
            throw std::invalid_argument("Stale, missing or misrouted camera frame");
        if(last_capture_&&frame.timestamp<*last_capture_)throw std::invalid_argument("Camera clock moved backwards");
        if(!origin_)origin_=frame.timestamp;
        const auto delta=static_cast<std::uint64_t>(frame.timestamp)-static_cast<std::uint64_t>(*origin_);
        if(delta>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()-10000000/format_.fps))
            throw std::invalid_argument("Camera timestamp exceeds stream lifetime");
        unsigned produced=0;
        decoder_->decode(frame.payload,static_cast<std::int64_t>(delta),[&](auto pixels,std::int64_t timestamp){
            if(++produced>8||timestamp<0||timestamp>static_cast<std::int64_t>(delta)||
               (last_output_&&timestamp<*last_output_)||pixels.size()!=std::size_t(format_.width)*format_.height*3/2)
                throw std::runtime_error("Camera decoder changed negotiated format or timing");
            endpoint_.submit(pixels,timestamp);last_output_=timestamp;
        });
        sequence_=frame.sequence;last_capture_=frame.timestamp;
    }catch(...){failed_=true;stop();throw;}
}
}
