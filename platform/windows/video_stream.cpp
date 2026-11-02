#include <windows.h>
#include <dm/video_wait.hpp>
#include <dm/video.hpp>
#include <dm/video_protocol.hpp>
#include <dm/tls.hpp>
#include <dm/h264.hpp>
#include <dm/video_gpu.hpp>
#include <dm/display.hpp>
#include <dm/input.hpp>
#include <bcrypt.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>

namespace dm::video {
namespace {
using Clock = std::chrono::steady_clock;
auto deadline() { return Clock::now() + std::chrono::seconds(5); }
}
void host_video(const net::Options& options, const std::string& pair, unsigned frames, unsigned fps, wire::Codec codec, const std::wstring& source, bool extend,bool allow_input,unsigned width,unsigned height,bool keep_layout) {
    if(allow_input && !extend && source.empty())throw std::invalid_argument("Input requires an explicit --display target or --extend");
    if((extend || !source.empty()) && codec!=wire::Codec::h264) throw std::invalid_argument("Selected/extended display requires H264");
    if(extend && !source.empty()) throw std::invalid_argument("Choose --extend or --display, not both");
    (void)wire::encode_offer({width,height,fps,frames,codec});
    // Authenticate before opening the capture API. Explicit CLI consent is checked by the caller.
    auto key = security::load_pairing(pair);
    SecureZeroMemory(key.data(), key.size());
    auto socket = net::accept_stream(options, [](const std::string& local) { std::cout << "LISTENING " << local << " TLS-video-test" << std::endl; });
    security::TlsStream stream(std::move(socket), true, pair);
    stream.handshake(deadline());
    std::array<std::uint8_t, wire::request.size()> request{};
    stream.read(request, deadline());
    if(request[7]>1)throw std::runtime_error("Unknown input permission request");
    const bool control=request[7]==1;request[7]=0;
    const unsigned requested_caps=request[8];request[8]=0;
    if(requested_caps>input::all_caps || control!=(requested_caps!=0))throw std::runtime_error("Invalid receiver input capabilities");
    if (request != wire::request) throw std::runtime_error("Unsupported video/input request");
    if(control && !allow_input)throw std::runtime_error("Host did not grant --allow-input permission");
    std::unique_ptr<display::VirtualMonitor> virtual_monitor;
    auto selected_source=source;
    if(extend) {
        const auto baseline=display::active_monitors();
        virtual_monitor=std::make_unique<display::VirtualMonitor>();
        selected_source=virtual_monitor->wait_for_source();
        if(!keep_layout || !display::restore_mode(selected_source,width,height))
            display::set_virtual_mode(selected_source,width,height,(width==1280 && height==720 && fps<=30)?30:60,baseline);
    }
    if(!selected_source.empty()) std::wcout<<L"CAPTURE_SOURCE "<<selected_source<<L" extended="<<extend<<std::endl;
    const unsigned profile_width=width,profile_height=height,target_fps=fps;
    std::optional<display::Mode> mode;
    std::wstring identity;
    if(!selected_source.empty()) {
        for(const auto& monitor:display::active_monitors())if(monitor.source==selected_source)identity=monitor.device_path;
        mode=display::current_mode(selected_source);
        if(extend){width=mode->width;height=mode->height;}
    }
    auto capture=std::make_unique<DesktopCapture>(selected_source,width,height);
    auto frame = codec==wire::Codec::h264 ? capture->next_gpu() : capture->next();
    std::unique_ptr<H264Encoder> encoder;
    if (codec==wire::Codec::h264) {
        encoder=std::make_unique<H264Encoder>(frame.width,frame.height,fps,frame.gpu->device.Get());
        std::cout << "ENCODER " << encoder->description() << std::endl;
    }
    std::unique_ptr<input::Broker> broker;
    std::uint64_t epoch=0;
    unsigned granted_caps=0;
    if(control) {
        broker=std::make_unique<input::Broker>(selected_source);
        granted_caps=broker->capabilities()&requested_caps;
        if(!granted_caps)throw std::runtime_error("No mutually supported input families");
        if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&epoch),sizeof(epoch),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0 || !epoch)throw std::runtime_error("Cannot create input session epoch");
        std::cout<<"INPUT_GRANTED caps="<<granted_caps<<" touch_contacts="<<((granted_caps&input::touch_cap)?10:0)<<" pen_devices="<<((granted_caps&input::pen_cap)?1:0)<<" watchdog_ms=1500"<<std::endl;
    }
    const auto codec_name=codec==wire::Codec::h264 ? "H264" : "JPEG-test";
    stream.write(wire::encode_offer({frame.width, frame.height, fps, frames,codec,static_cast<std::uint8_t>(granted_caps),epoch}), deadline());
    std::array<std::uint8_t,wire::ready.size()> ready{};
    stream.read(ready,deadline());
    if(ready!=wire::ready) throw std::runtime_error("Receiver did not confirm decoder readiness");
    std::cout << "VIDEO_STARTED " << frame.width << 'x' << frame.height << " codec=" << codec_name << " fps=" << fps << ' ' << stream.description() << std::endl;
    const auto start = Clock::now();
    std::uint64_t total = 0;
    double capture_ms=0,encode_ms=0,send_ms=0;
    std::uint64_t last_generation=0;std::uint64_t fresh_frames=0;
    std::uint64_t input_events=0;std::uint64_t last_input_time=0;
    auto next_frame=Clock::now();
    auto reconfigure=[&](std::uint64_t sequence) {
        broker.reset(); // Release held contacts before the geometry can be reused.
        const auto micros=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count());
        stream.write(wire::encode_header({static_cast<std::uint32_t>(sequence),0,micros}),deadline());
        std::cout<<"DISPLAY_RECONFIGURING"<<std::endl;
        encoder.reset();capture.reset();frame={};
        const auto until=Clock::now()+std::chrono::seconds(20);
        std::exception_ptr last;
        for(;;) {
            if(Clock::now()>=until)throw std::runtime_error("Display did not stabilize within recovery interval");
            try {
                bool found=false;
                for(const auto& monitor:display::active_monitors())if(monitor.device_path==identity && (!extend || monitor.dm_virtual)){selected_source=monitor.source;found=true;break;}
                if(!found)throw std::runtime_error("Waiting for selected display to return");
                auto current=display::current_mode(selected_source);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                if(current!=display::current_mode(selected_source))continue;
                fps=std::min(target_fps,std::clamp(current.hz,1u,60u));
                (void)wire::encode_offer({current.width,current.height,fps,frames,codec});
                capture=std::make_unique<DesktopCapture>(selected_source,current.width,current.height);
                frame=capture->next_gpu();
                encoder=std::make_unique<H264Encoder>(frame.width,frame.height,fps,frame.gpu->device.Get());
                mode=current;
                if(control) {
                    broker=std::make_unique<input::Broker>(selected_source);
                    if((broker->capabilities()&granted_caps)!=granted_caps)throw std::runtime_error("Input capabilities changed during display recovery");
                    if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&epoch),sizeof(epoch),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0 || !epoch)throw std::runtime_error("Cannot renew input epoch");
                }
                break;
            }catch(...){last=std::current_exception();broker.reset();encoder.reset();capture.reset();if(Clock::now()>=until)std::rethrow_exception(last);std::this_thread::sleep_for(std::chrono::milliseconds(100));}
            if(Clock::now()>=until)throw std::runtime_error("Display did not stabilize");
        }
        stream.write(wire::encode_offer({frame.width,frame.height,fps,frames,codec,static_cast<std::uint8_t>(granted_caps),epoch}),deadline());
        stream.read(ready,deadline());if(ready!=wire::ready)throw std::runtime_error("Receiver did not acknowledge changed geometry");
        if(keep_layout && extend)display::remember_mode(selected_source,profile_width,profile_height);
        last_input_time=0;next_frame=Clock::now();
        std::cout<<"DISPLAY_RECONFIGURED "<<frame.width<<'x'<<frame.height<<" position="<<mode->x<<','<<mode->y<<" rotation="<<mode->rotation<<std::endl;
    };
    if(keep_layout && extend)display::remember_mode(selected_source,profile_width,profile_height);
    for (std::uint64_t i = 0; !frames || i < frames; ++i) {
        const auto begin_capture=Clock::now();
        if(mode) {
            bool changed=false;
            try{changed=display::current_mode(selected_source)!=*mode;}catch(...){changed=true;}
            if(changed)reconfigure(i);
        }
        try{frame = encoder ? capture->next_gpu() : capture->next();}
        catch(...){if(!mode || codec!=wire::Codec::h264)throw;reconfigure(i);}
        if(frame.capture_generation!=last_generation){++fresh_frames;last_generation=frame.capture_generation;}
        const auto sample = Clock::now();
        auto payload = encoder ? encoder->encode(frame,i) : encode_jpeg(frame);
        const auto encoded=Clock::now();
        const auto micros = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(sample - start).count());
        stream.write(wire::encode_header({static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(payload.size()), micros}), deadline());
        stream.write(payload, deadline());
        if(broker) {
            input::HeaderBytes bytes{};stream.read(bytes,Clock::now()+std::chrono::milliseconds(1000));const auto header=input::decode_header(bytes);
            if(header.epoch!=epoch || header.sequence!=static_cast<std::uint32_t>(i))throw std::runtime_error("Input epoch/frame sequence mismatch");
            std::vector<std::uint8_t> events_bytes(header.count*input::event_size);if(!events_bytes.empty())stream.read(events_bytes,Clock::now()+std::chrono::milliseconds(1000));
            auto events=input::decode_events(events_bytes,header.count);
            for(const auto& event:events) {
                if(input::capability(event.kind)&~granted_caps)throw std::runtime_error("Input family was not granted");
                if(event.kind!=input::Kind::reset && event.micros<last_input_time)throw std::runtime_error("Input samples are out of order");
                if(event.kind!=input::Kind::reset)last_input_time=event.micros;
            }
            broker->send(events);input_events+=header.count;
        }
        const auto sent=Clock::now();
        capture_ms+=std::chrono::duration<double,std::milli>(sample-begin_capture).count();
        encode_ms+=std::chrono::duration<double,std::milli>(encoded-sample).count();
        send_ms+=std::chrono::duration<double,std::milli>(sent-encoded).count();
        total += payload.size();
        next_frame=std::max(next_frame+std::chrono::microseconds(1000000/fps),Clock::now());
        precise_wait(next_frame-Clock::now());
    }
    // Release input before waiting for the final acknowledgement or display removal.
    broker.reset();
    std::cout<<"INPUT_SESSION events="<<input_events<<" granted="<<control<<std::endl;
    // Receiver acknowledges every frame was decoded, not just received at TCP.
    std::array<std::uint8_t, 4> ack{};
    stream.read(ack, deadline());
    if (ack != std::array<std::uint8_t,4>{'D','O','N','E'}) throw std::runtime_error("Video receiver did not confirm completion");
    stream.close(deadline());
    const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::cout << "VIDEO_SENT frames=" << frames << " codec=" << codec_name << " encoded_bytes=" << total << " seconds=" << seconds << " achieved_fps=" << frames/seconds << " fresh_capture_frames=" << fresh_frames << " capture_ms_avg=" << capture_ms/frames << " encode_ms_avg=" << encode_ms/frames << " send_ms_avg=" << send_ms/frames << " raw_pixel_readbacks=" << (encoder ? 0 : frames) << " permissions=" << (control?"video+input":"video-only") << std::endl;
}
void receive_video(const net::Options& options, const std::string& pair, const std::function<void(Frame)>& sink,std::optional<unsigned> decoder_adapter,const std::function<std::vector<input::Event>()>& input_source,const std::function<void(unsigned)>& input_grant) {
    auto socket = net::connect_stream(options);
    security::TlsStream stream(std::move(socket), false, pair);
    stream.handshake(deadline());
    auto request=wire::request;if(input_source){request[7]=1;request[8]=static_cast<std::uint8_t>(input::receiver_capabilities());}stream.write(request, deadline());
    wire::OfferBytes offer_bytes{};
    // Authenticated host may need to enumerate/start its session-owned driver.
    stream.read(offer_bytes, Clock::now()+std::chrono::seconds(45));
    auto offer = wire::decode_offer(offer_bytes);
    if((offer.input_caps!=0)!=static_cast<bool>(input_source))throw std::runtime_error("Unexpected input permission grant");
    if(offer.input_caps&~request[8])throw std::runtime_error("Host granted unrequested input families");
    if(input_grant)input_grant(offer.input_caps);
    if (decoder_adapter && offer.codec!=wire::Codec::h264) throw std::runtime_error("Decoder adapter selection requires an H264 stream");
    std::unique_ptr<H264Decoder> decoder;
    if (offer.codec==wire::Codec::h264) {
        decoder=std::make_unique<H264Decoder>(offer.width,offer.height,offer.fps,decoder_adapter,true);
        std::cout << "DECODER " << decoder->description() << std::endl;
    }
    const auto codec_name=decoder ? "H264" : "JPEG-test";
    std::cout << "VIDEO_RECEIVING " << offer.width << 'x' << offer.height << " codec=" << codec_name << ' ' << stream.description() << std::endl;
    stream.write(wire::ready,deadline());
    std::uint64_t total = 0, previous_time = 0;
    const auto start = Clock::now();
    std::uint64_t decoded_frames=0,retired_gpu_frames=0;
    double decode_ms=0;
    auto deliver=[&](Frame frame) {
        if (++decoded_frames>offer.frames && offer.frames) throw std::runtime_error("Decoded frame count exceeds negotiated limit");
        sink(std::move(frame));
    };
    for (std::uint64_t i = 0; !offer.frames || i < offer.frames; ++i) {
        wire::HeaderBytes header_bytes{};
        stream.read(header_bytes, deadline());
        auto header = wire::decode_header(header_bytes);
        while(header.bytes==0) {
            if(header.sequence!=static_cast<std::uint32_t>(i) || header.micros<previous_time)throw std::runtime_error("Out-of-order reconfiguration");
            previous_time=header.micros;
            if(input_grant)input_grant(0);
            if(decoder)for(auto& frame:decoder->finish())deliver(std::move(frame));
            if(decoder)retired_gpu_frames+=decoder->gpu_frames();
            decoder.reset();
            stream.read(offer_bytes,Clock::now()+std::chrono::seconds(30));
            const auto replacement=wire::decode_offer(offer_bytes);
            if(replacement.codec!=offer.codec || replacement.frames!=offer.frames || replacement.input_caps!=offer.input_caps || (offer.input_caps && replacement.input_epoch==offer.input_epoch))throw std::runtime_error("Invalid reconfiguration offer");
            offer=replacement;
            if(offer.codec!=wire::Codec::h264)throw std::runtime_error("Reconfiguration requires H264");
            decoder=std::make_unique<H264Decoder>(offer.width,offer.height,offer.fps,decoder_adapter,true);
            if(input_source)(void)input_source();
            if(input_grant)input_grant(offer.input_caps);
            stream.write(wire::ready,deadline());
            std::cout<<"DISPLAY_RECONFIGURED "<<offer.width<<'x'<<offer.height<<std::endl;
            stream.read(header_bytes,deadline());header=wire::decode_header(header_bytes);
        }
        if (header.sequence != static_cast<std::uint32_t>(i) || header.micros < previous_time) throw std::runtime_error("Out-of-order video frame");
        previous_time = header.micros;
        std::vector<std::uint8_t> payload(header.bytes);
        stream.read(payload, deadline());
        // Return input before decoding so host encode can overlap receiver GPU work.
        if(offer.input_caps) {
            auto events=input_source();
            stream.write(input::encode_header({offer.input_epoch,static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(events.size())}),deadline());
            const auto bytes=input::encode_events(events);if(!bytes.empty())stream.write(bytes,deadline());
        }
        const auto decode_start=Clock::now();
        if (decoder) {
            for (auto& frame:decoder->decode(payload,i)) deliver(std::move(frame));
        } else deliver(decode_jpeg(payload,offer.width,offer.height));
        decode_ms+=std::chrono::duration<double,std::milli>(Clock::now()-decode_start).count();
        total += payload.size();
    }
    if (decoder) for (auto& frame:decoder->finish()) deliver(std::move(frame));
    if (decoded_frames!=offer.frames) throw std::runtime_error("Decoder did not produce every negotiated frame");
    stream.write(std::array<std::uint8_t,4>{'D','O','N','E'}, deadline());
    stream.close(deadline());
    const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::cout << "VIDEO_RECEIVED decoded_frames=" << decoded_frames << " codec=" << codec_name << " encoded_bytes=" << total << " seconds=" << seconds << " decode_ms_avg=" << decode_ms/decoded_frames << " gpu_surface_frames=" << (decoder ? retired_gpu_frames+decoder->gpu_frames() : 0) << " raw_pixel_readbacks=0 permissions=" << (offer.input_caps?"video+input":"video-only") << std::endl;
}
}
