#include "receiver.hpp"
#include "tls.hpp"
#include <gst/app/gstappsrc.h>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace dm::linux_client {
namespace {
using Clock=std::chrono::steady_clock;
auto deadline(int seconds=15){return Clock::now()+std::chrono::seconds(seconds);}
struct GstRelease{void operator()(GstElement* p)const{if(p)gst_object_unref(p);}};
using Source=std::unique_ptr<GstElement,GstRelease>;
}
Receiver::Receiver(Options options,InputQueue& input,Media& media,QObject* parent)
    :QThread(parent),options_(std::move(options)),input_(input),media_(media){}
void Receiver::run(){
    namespace wire=dm::video::wire;
    const auto cancelled=[this]{return isInterruptionRequested();};
    try{
        emit state("Connecting...");
        Tls stream(options_.server.toStdString(),options_.local.toStdString(),static_cast<uint16_t>(options_.port),cancelled,options_.interfaceIndex,options_.code.toStdString());
        options_.code.fill(QChar(0));options_.code.clear();
        auto request=wire::request;
        request[9]=static_cast<std::uint8_t>(options_.scale);
        const unsigned requested=options_.input?dm::input::all_caps:0;
        request[7]=requested?1:0;request[8]=static_cast<std::uint8_t>(requested);
        wire::set_client_display(request,options_.display);stream.write(request,deadline());
        wire::OfferBytes bytes{};stream.read(bytes,deadline(45));
        auto offer=wire::decode_offer(bytes);
        if(offer.codec!=wire::Codec::h264)throw std::runtime_error("The Linux client requires an H.264 server stream");
        if(offer.input_caps&~requested)throw std::runtime_error("Invalid server input grant");
        unsigned serial=0;
        Source source;
        auto prepare=[&]{
            input_.grant(0);source.reset();
            emit configure(offer.width,offer.height,offer.fps,++serial);
            source.reset(media_.waitSource(serial,cancelled));
            input_.grant(offer.input_caps);
            stream.write(wire::ready,deadline());
            emit state("Connected");
        };
        prepare();
        std::uint64_t sequence=0,previousTime=0,intervalFrames=0,intervalBytes=0;
        auto statsStart=Clock::now();
        while(!offer.frames || sequence<offer.frames){
            if(cancelled())break;
            if(input_.overflowed())throw std::runtime_error("Input queue overflowed; disconnected to release held input");
            wire::HeaderBytes headerBytes{};stream.read(headerBytes,deadline());
            const auto header=wire::decode_header(headerBytes);
            if(header.sequence!=static_cast<std::uint32_t>(sequence) || header.micros<previousTime)
                throw std::runtime_error("Out-of-order video frame");
            if(header.micros>=GST_CLOCK_TIME_NONE/GST_USECOND)throw std::runtime_error("Video timestamp exceeds supported range");
            previousTime=header.micros;
            if(!header.bytes){
                // Hold the TCP session while Windows changes the virtual display.
                input_.grant(0);stream.read(bytes,deadline(30));const auto replacement=wire::decode_offer(bytes);
                if(replacement.codec!=offer.codec || replacement.frames!=offer.frames || replacement.input_caps!=offer.input_caps ||
                   (offer.input_caps && replacement.input_epoch==offer.input_epoch))throw std::runtime_error("Invalid display reconfiguration");
                gst_app_src_end_of_stream(GST_APP_SRC(source.get()));media_.waitEos(cancelled,2);
                offer=replacement;prepare();continue;
            }
            std::vector<std::uint8_t> payload(header.bytes);stream.read(payload,deadline());
            // Windows waits for one input packet per compressed frame, even when empty.
            // Send it before decoder work to preserve the Windows receiver's ordering.
            if(offer.input_caps){
                const auto events=input_.take();
                stream.write(dm::input::encode_header({offer.input_epoch,static_cast<std::uint32_t>(sequence),static_cast<std::uint32_t>(events.size())}),deadline());
                stream.write(dm::input::encode_events(events),deadline());
            }
            // Backpressure compressed data rather than dropping arbitrary H.264 reference frames.
            // Do not let a stalled decoder hold the host's input watchdog indefinitely.
            const auto queueDeadline=Clock::now()+std::chrono::milliseconds(500);
            for(;;){guint64 count=0;g_object_get(source.get(),"current-level-buffers",&count,nullptr);
                if(count<2)break;
                if(cancelled())throw std::runtime_error("Disconnected");
                if(Clock::now()>=queueDeadline)throw std::runtime_error("Decoder stalled; disconnected to avoid queued latency");
                QThread::msleep(2);
            }
            auto* buffer=gst_buffer_new_allocate(nullptr,payload.size(),nullptr);
            if(!buffer)throw std::runtime_error("Cannot allocate compressed video packet");
            gst_buffer_fill(buffer,0,payload.data(),payload.size());
            GST_BUFFER_PTS(buffer)=header.micros*GST_USECOND;
            GST_BUFFER_DURATION(buffer)=GST_SECOND/offer.fps;
            const auto flow=gst_app_src_push_buffer(GST_APP_SRC(source.get()),buffer); // takes ownership
            if(flow!=GST_FLOW_OK)throw std::runtime_error("Video decoder stopped accepting frames");
            ++sequence;++intervalFrames;intervalBytes+=payload.size();
            const double elapsed=std::chrono::duration<double>(Clock::now()-statsStart).count();
            if(elapsed>=1){
                emit statistics(QString("%1 x %2 | Receive %3 FPS | %4 Mbps | Target %5 FPS")
                    .arg(offer.width).arg(offer.height).arg(intervalFrames/elapsed,0,'f',1)
                    .arg(intervalBytes*8.0/elapsed/1000000,0,'f',1).arg(offer.fps));
                intervalFrames=intervalBytes=0;statsStart=Clock::now();
            }
        }
        if(!cancelled() && offer.frames){
            gst_app_src_end_of_stream(GST_APP_SRC(source.get()));media_.waitEos(cancelled);
            if(media_.decodedFrames()!=sequence)throw std::runtime_error("Decoder did not produce every negotiated frame");
            stream.write(std::array<std::uint8_t,4>{'D','O','N','E'},deadline());
            emit state("Server diagnostic session completed");
        }else emit state("Disconnected");
    }catch(const std::exception& error){if(cancelled())emit state("Disconnected");else emit failed(QString::fromUtf8(error.what()));}
    // Closing TLS lets the Windows session release its keys/contacts and monitor.
    input_.grant(0);
}
}
