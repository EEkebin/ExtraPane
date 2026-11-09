#include <climits>
#include <windows.h>
#include <dm/video_wait.hpp>
#include <dm/video.hpp>
#include <dm/video_protocol.hpp>
#include <dm/tls.hpp>
#include <dm/h264.hpp>
#include <dm/video_gpu.hpp>
#include <dm/display.hpp>
#include <dm/input.hpp>
#include <dm/text_focus.hpp>
#include <dm/text_focus_policy.hpp>
#include <bcrypt.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <syncstream>

namespace dm::video {
namespace {
std::mutex display_lifecycle;
using Clock = std::chrono::steady_clock;
auto deadline() { return Clock::now() + std::chrono::seconds(5); }
}
SharedScreen share_screen(std::uint32_t id,const std::wstring& source,bool allow_input) {
    if(!id||source.empty())throw std::invalid_argument("Choose an explicit screen to share");
    for(const auto& monitor:display::active_monitors())if(monitor.source==source&&!monitor.device_path.empty())return {id,monitor.device_path,allow_input};
    throw std::invalid_argument("Selected screen is unavailable");
}
void host_video(const net::Options& options, const std::string& pair, unsigned frames, unsigned fps, wire::Codec codec, const std::wstring& source, bool extend,bool allow_input,unsigned width,unsigned height,bool keep_layout,bool client_mode,const std::function<std::wstring(bool&)>& acquire_monitor,bool* listener_ready,net::TcpStream* accepted,unsigned monitor_slot,security::TlsStream* admitted,std::span<const SharedScreen> shared_screens,bool existing_only) {
    if(allow_input && !extend && source.empty())throw std::invalid_argument("Input requires an explicit --display target or --extend");
    if((extend || !source.empty()) && codec!=wire::Codec::h264) throw std::invalid_argument("Selected/extended display requires H264");
    if(extend && !source.empty()) throw std::invalid_argument("Choose --extend or --display, not both");
    (void)wire::encode_offer({width,height,fps,frames,codec});
    // Authenticate before opening the capture API. Explicit CLI consent is checked by the caller.
    if(!pair.empty()){auto key = security::load_pairing(pair);SecureZeroMemory(key.data(), key.size());}
    std::unique_ptr<security::TlsStream> owned_stream;
    if(!admitted){
        auto socket = accepted?std::move(*accepted):net::accept_stream(options, [&](const std::string& local) { if(listener_ready)*listener_ready=true;std::osyncstream(std::cout) << "LISTENING " << local << " TLS-video-test" << std::endl; });
        owned_stream=std::make_unique<security::TlsStream>(std::move(socket),true,pair);
        owned_stream->handshake(deadline());admitted=owned_stream.get();
    }
    auto& stream=*admitted;
    std::array<std::uint8_t, wire::request.size()> request{};
    stream.read(request, deadline());
    const auto preference=wire::take_client_display(request);
    const auto screen=wire::take_screen_request(request);
    const bool existing_screen=screen.mode==wire::DisplayMode::existing_screen;
    if(existing_only&&!existing_screen)throw std::runtime_error("This grant permits only the selected existing screen");
    if(request[9]>2)throw std::runtime_error("Unknown quality mode");
    QualityController quality(static_cast<Quality>(request[9]));request[9]=0;
    if(request[7]>1)throw std::runtime_error("Unknown input permission request");
    const bool control=request[7]==1;request[7]=0;
    const unsigned requested_caps=request[8];request[8]=0;
    if(request[10]>2)throw std::runtime_error("Unknown text-focus hint request");
    const bool host_keyboard_requested=request[10]==2;const bool text_focus_requested=request[10]!=0;request[10]=0;
    if(text_focus_requested&&(!control||!(requested_caps&input::touch_cap)))throw std::runtime_error("Text-focus hints require granted touch input");
    if(requested_caps>input::all_caps || control!=(requested_caps!=0))throw std::runtime_error("Invalid receiver input capabilities");
    if (request != wire::request) throw std::runtime_error("Unsupported video/input request");
    std::wstring permitted_source=source;
    std::wstring permitted_identity;
    if(existing_screen) {
        const SharedScreen* permission=nullptr;
        for(const auto& offered:shared_screens)if(offered.id==screen.screen_id){
            if(permission)throw std::runtime_error("Duplicate shared screen identity");permission=&offered;
        }
        if(!permission||permission->identity.empty())throw std::runtime_error("The selected screen is not offered for sharing");
        permitted_source.clear();
        for(const auto& monitor:display::active_monitors())if(monitor.device_path==permission->identity){permitted_source=monitor.source;break;}
        if(permitted_source.empty())throw std::runtime_error("The shared screen is disconnected");
        permitted_identity=permission->identity;allow_input=permission->allow_input;extend=false;client_mode=false;keep_layout=false;
        if(preference.exact_mode)throw std::runtime_error("Existing-screen sharing cannot configure a virtual monitor");
        const auto actual=display::current_mode(permitted_source);
        width=actual.width;height=actual.height;fps=std::min(preference.hz?preference.hz:fps,std::clamp(actual.hz,1u,360u));
        (void)wire::encode_offer({width,height,fps,frames,codec});
    }
    if(const auto scoped_input=stream.authorized_display_input();scoped_input.has_value())allow_input=allow_input&&*scoped_input;
    if(control && !allow_input)throw std::runtime_error("The selected display has not granted remote input");
    if(client_mode && !extend)throw std::runtime_error("Client display matching requires extension");
    if(client_mode && !preference.width)throw std::runtime_error("Client did not supply its display mode");
    const unsigned profile_width=client_mode?preference.width:width,profile_height=client_mode?preference.height:height;
    struct MonitorDelete {void operator()(display::VirtualMonitor* p) const {std::lock_guard lock(display_lifecycle);delete p;}};
    std::unique_ptr<display::VirtualMonitor,MonitorDelete> virtual_monitor;
    auto selected_source=permitted_source;
    display::Mode client_choice{};
    if(extend) {
        std::lock_guard lock(display_lifecycle);
        const auto baseline=display::active_monitors();
        bool existing_monitor=false;
        if(!acquire_monitor) {
            virtual_monitor.reset(new display::VirtualMonitor(monitor_slot,preference.exact_mode?display::Mode{preference.width,preference.height,preference.hz}:display::Mode{}));
            selected_source=virtual_monitor->wait_for_source();
        } else selected_source=acquire_monitor(existing_monitor);
        if(client_mode) {
            const auto chosen=display::closest_mode(selected_source,preference.width,preference.height,preference.hz);
            if(preference.exact_mode&&(chosen.width!=preference.width||chosen.height!=preference.height||chosen.hz!=preference.hz))throw std::runtime_error("The installed display driver does not support this custom mode; update the server driver or select a supported mode");
            client_choice=chosen;
            width=chosen.width;height=chosen.height;fps=std::min(preference.hz,chosen.hz);
            std::osyncstream(std::cout)<<"CLIENT_MODE requested="<<preference.width<<'x'<<preference.height<<'@'<<preference.hz
                     <<" selected="<<width<<'x'<<height<<'@'<<fps
                     <<" fallback="<<(width!=preference.width || height!=preference.height || fps!=preference.hz)<<std::endl;
        }
        const bool restored_layout=keep_layout&&display::restore_mode(selected_source,profile_width,profile_height,existing_monitor);
        if(preference.exact_mode&&restored_layout){
            auto exact=display::current_mode(selected_source);exact.width=width;exact.height=height;exact.hz=client_choice.hz;exact.rotation=0;display::configure_virtual(selected_source,exact);
        }else if(!restored_layout) {
            if(client_mode && client_choice.rotation) {
                display::set_virtual_mode(selected_source,height,width,client_choice.hz,baseline);
                auto rotated=display::current_mode(selected_source);rotated.width=width;rotated.height=height;rotated.rotation=client_choice.rotation;
                display::configure_virtual(selected_source,rotated);
            } else display::set_virtual_mode(selected_source,width,height,client_mode?client_choice.hz:((width==1280 && height==720 && fps<=30)?30:60),baseline);
        }
        display::restore_monitors(baseline);
    }
    if(!selected_source.empty()) std::wosyncstream(std::wcout)<<L"CAPTURE_SOURCE "<<selected_source<<L" extended="<<extend<<std::endl;
    const unsigned target_fps=fps;
    std::optional<display::Mode> mode;
    std::wstring identity=permitted_identity;
    if(!selected_source.empty()) {
        if(identity.empty())for(const auto& monitor:display::active_monitors())if(monitor.source==selected_source)identity=monitor.device_path;
        mode=display::current_mode(selected_source);
        if(extend){width=mode->width;height=mode->height;}
    }
    std::unique_ptr<DesktopCapture> capture;
    Frame frame;
    // Restoring a remembered mode returns before Windows/WGC finish resizing.
    // No offer has been sent yet: retry startup against the same monitor
    // identity rather than dropping the connection on the first stale frame.
    const auto startup_until=Clock::now()+std::chrono::seconds(20);
    bool reported_startup_wait=false;
    for(;;){
        try {
            if(mode){
                bool found=false;
                for(const auto& monitor:display::active_monitors())if(monitor.device_path==identity && (!extend || monitor.dm_virtual)){selected_source=monitor.source;found=true;break;}
                if(!found)throw std::runtime_error("Waiting for selected display during startup");
                const auto current=display::current_mode(selected_source);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if(current!=display::current_mode(selected_source))throw std::runtime_error("Display mode is still settling");
                mode=current;
                if(extend||existing_screen){width=current.width;height=current.height;fps=std::min(target_fps,std::clamp(current.hz,1u,360u));}
            }
            capture=std::make_unique<DesktopCapture>(selected_source,width,height);
            frame=codec==wire::Codec::h264?capture->next_gpu():capture->next();
            if(mode && *mode!=display::current_mode(selected_source))throw std::runtime_error("Display changed during capture startup");
            break;
        }catch(...){
            capture.reset();frame={};
            if(!mode || Clock::now()>=startup_until)throw;
            if(!reported_startup_wait){std::osyncstream(std::cout)<<"DISPLAY_STARTING waiting-for-stable-capture"<<std::endl;reported_startup_wait=true;}
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    auto scale_frame=[&]{if(codec==wire::Codec::h264){auto size=stream_size(frame.width,frame.height,quality.scale());frame.width=size.first;frame.height=size.second;}};
    scale_frame();
    std::unique_ptr<H264Encoder> encoder;
    if (codec==wire::Codec::h264) {
        encoder=std::make_unique<H264Encoder>(frame.width,frame.height,fps,frame.gpu->device.Get(),quality.bitrate(frame.width,frame.height,fps));
        std::osyncstream(std::cout) << "ENCODER " << encoder->description() << std::endl;
    }
    std::unique_ptr<input::Broker> broker;
    std::uint64_t epoch=0;
    std::unique_ptr<input::TextFocusProbe> text_focus;
    unsigned granted_caps=0;
    if(control) {
        broker=std::make_unique<input::Broker>(selected_source);
        granted_caps=broker->capabilities()&requested_caps;
        if(!granted_caps)throw std::runtime_error("No mutually supported input families");
        if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&epoch),sizeof(epoch),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0 || !epoch)throw std::runtime_error("Cannot create input session epoch");
        std::osyncstream(std::cout)<<"INPUT_GRANTED caps="<<granted_caps<<" touch_contacts="<<((granted_caps&input::touch_cap)?10:0)<<" pen_devices="<<((granted_caps&input::pen_cap)?1:0)<<" watchdog_ms=1500"<<std::endl;
    }
    const auto codec_name=codec==wire::Codec::h264 ? "H264" : "JPEG-test";
    if(text_focus_requested&&(granted_caps&input::touch_cap))text_focus=std::make_unique<input::TextFocusProbe>(selected_source,host_keyboard_requested);
    stream.write(wire::encode_offer({frame.width, frame.height, fps, frames,codec,static_cast<std::uint8_t>(granted_caps),epoch}), deadline());
    std::array<std::uint8_t,wire::ready.size()> ready{};
    stream.read(ready,deadline());
    if(ready!=wire::ready) throw std::runtime_error("Receiver did not confirm decoder readiness");
    std::osyncstream(std::cout) << "VIDEO_STARTED " << frame.width << 'x' << frame.height << " codec=" << codec_name << " fps=" << fps << ' ' << stream.description() << std::endl;
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
        std::osyncstream(std::cout)<<"DISPLAY_RECONFIGURING"<<std::endl;
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
                fps=std::min(target_fps,std::clamp(current.hz,1u,360u));
                (void)wire::encode_offer({current.width,current.height,fps,frames,codec});
                capture=std::make_unique<DesktopCapture>(selected_source,current.width,current.height);
                frame=capture->next_gpu();scale_frame();
                encoder=std::make_unique<H264Encoder>(frame.width,frame.height,fps,frame.gpu->device.Get(),quality.bitrate(frame.width,frame.height,fps));
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
        if(text_focus)text_focus=std::make_unique<input::TextFocusProbe>(selected_source,host_keyboard_requested);
        last_input_time=0;next_frame=Clock::now();
        std::osyncstream(std::cout)<<"DISPLAY_RECONFIGURED "<<frame.width<<'x'<<frame.height<<" position="<<mode->x<<','<<mode->y<<" rotation="<<mode->rotation<<std::endl;
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
        catch(...){if(!mode || codec!=wire::Codec::h264)throw;reconfigure(i);frame=capture->next_gpu();}
        scale_frame();
        if(frame.capture_generation!=last_generation){++fresh_frames;last_generation=frame.capture_generation;}
        const auto sample = Clock::now();
        auto payload = encoder ? encoder->encode(frame,i) : encode_jpeg(frame);
        const auto encoded=Clock::now();
        const auto micros = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(sample - start).count());
        stream.write(wire::encode_header({static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(payload.size()), micros,text_focus?text_focus->confirmed_touch():0}), deadline());
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
            broker->send(events);if(text_focus)text_focus->observe(events);input_events+=header.count;
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
    std::osyncstream(std::cout)<<"INPUT_SESSION events="<<input_events<<" granted="<<control<<std::endl;
    // Receiver acknowledges every frame was decoded, not just received at TCP.
    std::array<std::uint8_t, 4> ack{};
    stream.read(ack, deadline());
    if (ack != std::array<std::uint8_t,4>{'D','O','N','E'}) throw std::runtime_error("Video receiver did not confirm completion");
    stream.close(deadline());
    const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::osyncstream(std::cout) << "VIDEO_SENT frames=" << frames << " codec=" << codec_name << " encoded_bytes=" << total << " seconds=" << seconds << " achieved_fps=" << frames/seconds << " fresh_capture_frames=" << fresh_frames << " capture_ms_avg=" << capture_ms/frames << " encode_ms_avg=" << encode_ms/frames << " send_ms_avg=" << send_ms/frames << " raw_pixel_readbacks=" << (encoder ? 0 : frames) << " permissions=" << (control?"video+input":"video-only") << std::endl;
}
void serve_video(const net::Options& requested,const std::string& pair,bool allow_input,unsigned frames) {
    // WinRT capture factories outlive an individual session. Keep their MTA alive
    // while the driver service accepts subsequent connections.
    const auto com_result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com_result))throw std::runtime_error("Cannot initialize driver service COM apartment");
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    if(!pair.empty()){auto key=security::load_pairing(pair);SecureZeroMemory(key.data(),key.size());}
    auto options=requested;options.timeout=std::chrono::milliseconds(0);
    net::TcpListener listener(options);
    std::osyncstream(std::cout)<<"DRIVER_STARTED waiting-for-clients capacity=resource-dependent"<<std::endl;
    std::osyncstream(std::cout)<<"LISTENING "<<listener.endpoints()<<std::endl;
    struct Session {std::thread worker;std::atomic<bool> done{true};};
    std::vector<std::unique_ptr<Session>> sessions;std::atomic_uint pending_handshakes=0;
    // The launcher owns process lifetime. Each worker owns its own monitor,
    // capture, encoder, TLS stream and input watchdog; failure stays local.
    for(;;) {
        auto socket=listener.accept();
        if(pending_handshakes.load()>=32){std::osyncstream(std::cout)<<"CLIENT_REJECTED Server is busy admitting new connections; retry shortly"<<std::endl;continue;}
        auto free=std::find_if(sessions.begin(),sessions.end(),[](const auto& session){return session->done.load();});
        if(free==sessions.end()){try{if(sessions.size()>=UINT_MAX)throw std::runtime_error("Display endpoint identity space exhausted");sessions.push_back(std::make_unique<Session>());free=sessions.end()-1;}catch(const std::exception& error){std::osyncstream(std::cout)<<"CLIENT_REJECTED Cannot allocate display session: "<<error.what()<<std::endl;continue;}}
        if((*free)->worker.joinable())(*free)->worker.join();
        auto* session=free->get();session->done.store(false);++pending_handshakes;
        const auto slot=static_cast<unsigned>(free-sessions.begin())+1;
        try{session->worker=std::thread([session,slot,options,pair,allow_input,frames,&pending_handshakes,socket=std::move(socket)]() mutable {
            bool pending=true;struct Pending{std::atomic_uint& count;bool& pending;~Pending(){if(pending)--count;}}pending_guard{pending_handshakes,pending};
            const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            try {
                if(FAILED(com))throw std::runtime_error("Cannot initialize client COM apartment");
                const auto peer=socket.peer_endpoint();const auto ip=peer.substr(0,peer.find(':'));
                display::select_layout_profile(std::wstring(ip.begin(),ip.end()));
                std::osyncstream(std::cout)<<"CLIENT_CONNECTED slot="<<slot<<" peer="<<peer<<std::endl;
                security::TlsStream tls(std::move(socket),true,pair);tls.handshake(deadline());--pending_handshakes;pending=false;
                host_video(options,pair,frames,60,wire::Codec::h264,L"",true,allow_input,1280,720,true,true,{},nullptr,nullptr,slot,&tls);
            } catch(const std::exception& error){std::osyncstream(std::cout)<<"CLIENT_ERROR slot="<<slot<<" "<<error.what()<<std::endl;}
            catch(...){std::osyncstream(std::cout)<<"CLIENT_ERROR slot="<<slot<<" unexpected session failure"<<std::endl;}
            if(SUCCEEDED(com))CoUninitialize();
            std::osyncstream(std::cout)<<"CLIENT_DISCONNECTED slot="<<slot<<std::endl;
            session->done.store(true);
        });}catch(const std::exception& error){--pending_handshakes;session->done=true;std::osyncstream(std::cout)<<"CLIENT_REJECTED Cannot start display worker: "<<error.what()<<std::endl;}
    }
}
void receive_video(const net::Options& options, const std::string& pair, const std::function<void(Frame)>& sink,std::optional<unsigned> decoder_adapter,const std::function<std::vector<input::Event>()>& input_source,const std::function<void(unsigned)>& input_grant,wire::ClientDisplay client_display,Quality quality,const std::function<void(StreamStats)>& statistics,const std::function<void()>& editable_touch,wire::ScreenRequest screen) {
    auto socket = net::connect_stream(options);
    security::TlsStream stream(std::move(socket), false, pair);
    stream.handshake(deadline());
    auto request=wire::request;request[9]=static_cast<std::uint8_t>(quality);if(input_source){request[7]=1;request[8]=static_cast<std::uint8_t>(input::receiver_capabilities());}if(editable_touch&&input_source&&(request[8]&input::touch_cap))request[10]=2;wire::set_client_display(request,client_display);wire::set_screen_request(request,screen);stream.write(request, deadline());
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
        std::osyncstream(std::cout) << "DECODER " << decoder->description() << std::endl;
    }
    const auto codec_name=decoder ? "H264" : "JPEG-test";
    std::osyncstream(std::cout) << "VIDEO_RECEIVING " << offer.width << 'x' << offer.height << " codec=" << codec_name << ' ' << stream.description() << std::endl;
    stream.write(wire::ready,deadline());
    std::uint64_t total = 0, previous_time = 0;
    const auto start = Clock::now();
    std::uint64_t decoded_frames=0,retired_gpu_frames=0;
    double decode_ms=0,last_decode_ms=0,interval_decode_ms=0;
    auto stats_start=Clock::now();std::uint64_t interval_bytes=0,interval_frames=0;
    auto deliver=[&](Frame frame) {
        if (++decoded_frames>offer.frames && offer.frames) throw std::runtime_error("Decoded frame count exceeds negotiated limit");
        sink(std::move(frame));
    };
    input::TouchKeyboardGate text_focus_gate;
    for (std::uint64_t i = 0; !offer.frames || i < offer.frames; ++i) {
        wire::HeaderBytes header_bytes{};
        stream.read(header_bytes, deadline());
        auto header = wire::decode_header(header_bytes);
        while(header.bytes==0) {
            if(header.sequence!=static_cast<std::uint32_t>(i) || header.micros<previous_time)throw std::runtime_error("Out-of-order reconfiguration");
            previous_time=header.micros;
            text_focus_gate.clear();if(input_grant)input_grant(0);
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
            std::osyncstream(std::cout)<<"DISPLAY_RECONFIGURED "<<offer.width<<'x'<<offer.height<<std::endl;
            stream.read(header_bytes,deadline());header=wire::decode_header(header_bytes);
        }
        if (header.sequence != static_cast<std::uint32_t>(i) || header.micros < previous_time) throw std::runtime_error("Out-of-order video frame");
        previous_time = header.micros;
        if(header.editable_touch&&!request[10])throw std::runtime_error("Unrequested text-focus hint");
        if(editable_touch&&text_focus_gate.accept(header.editable_touch,GetTickCount64()))editable_touch();
        std::vector<std::uint8_t> payload(header.bytes);
        stream.read(payload, deadline());
        // Return input before decoding so host encode can overlap receiver GPU work.
        if(offer.input_caps) {
            auto events=input_source();
            for(const auto& event:events){if(event.kind==input::Kind::reset||event.kind==input::Kind::key||event.kind==input::Kind::pen||(event.kind==input::Kind::mouse&&event.action==input::Action::down))text_focus_gate.clear();else if(event.kind==input::Kind::touch&&event.action==input::Action::down)text_focus_gate.touch(input::touch_token(event.micros),GetTickCount64());}
            stream.write(input::encode_header({offer.input_epoch,static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(events.size())}),deadline());
            const auto bytes=input::encode_events(events);if(!bytes.empty())stream.write(bytes,deadline());
        }
        const auto decode_start=Clock::now();
        if (decoder) {
            for (auto& frame:decoder->decode(payload,i)) deliver(std::move(frame));
        } else deliver(decode_jpeg(payload,offer.width,offer.height));
        last_decode_ms=std::chrono::duration<double,std::milli>(Clock::now()-decode_start).count();decode_ms+=last_decode_ms;
        interval_decode_ms+=last_decode_ms;interval_bytes+=payload.size();++interval_frames;
        const double elapsed=std::chrono::duration<double>(Clock::now()-stats_start).count();
        if(elapsed>=1){if(statistics)statistics({interval_frames/elapsed,interval_bytes*8.0/elapsed/1000000,interval_decode_ms/interval_frames,offer.width,offer.height,offer.fps});stats_start=Clock::now();interval_bytes=interval_frames=0;interval_decode_ms=0;}
        total += payload.size();
    }
    if (decoder) for (auto& frame:decoder->finish()) deliver(std::move(frame));
    if (decoded_frames!=offer.frames) throw std::runtime_error("Decoder did not produce every negotiated frame");
    stream.write(std::array<std::uint8_t,4>{'D','O','N','E'}, deadline());
    stream.close(deadline());
    const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::osyncstream(std::cout) << "VIDEO_RECEIVED decoded_frames=" << decoded_frames << " codec=" << codec_name << " encoded_bytes=" << total << " seconds=" << seconds << " decode_ms_avg=" << decode_ms/decoded_frames << " gpu_surface_frames=" << (decoder ? retired_gpu_frames+decoder->gpu_frames() : 0) << " raw_pixel_readbacks=0 permissions=" << (offer.input_caps?"video+input":"video-only") << std::endl;
}
}
