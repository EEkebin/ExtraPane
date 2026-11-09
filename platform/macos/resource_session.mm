#include "resource_session.hpp"
#include "resource_capture.hpp"
#include "transport.hpp"
#include "resource_microphone.hpp"
#include <extrapane/camera_protocol.hpp>
#include <extrapane/negotiation.hpp>
#include <Security/Security.h>
#include <CoreMedia/CoreMedia.h>
#include <algorithm>
#include <stdexcept>
namespace dm::mac {
using namespace extrapane;
namespace {void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}}
ResourceSession::ResourceSession(ResourceOptions options,ResourceCallbacks callbacks):options_(std::move(options)),callbacks_(std::move(callbacks)){}
ResourceSession::~ResourceSession(){cancel();if(worker_.joinable())worker_.join();std::fill(options_.code.begin(),options_.code.end(),0);}
void ResourceSession::start(){if(worker_.joinable())throw std::runtime_error("Resource session already started");worker_=std::thread([this]{run();});}
void ResourceSession::cancel(){cancelled_=true;std::shared_ptr<Transport> stream;{std::lock_guard lock(mutex_);stream=stream_;}if(stream)stream->cancel();}
void ResourceSession::setMuted(bool value){mute_.set(value,CMTimeConvertScale(CMClockGetTime(CMClockGetHostTimeClock()),10000000,kCMTimeRoundingMethod_Default).value);}
void ResourceSession::run(){@autoreleasepool{
    std::string ending;
    try{
        require(options_.kind==ResourceKind::camera||options_.kind==ResourceKind::microphone,"Unsupported Mac resource source");
        const auto kind=options_.kind==ResourceKind::camera?resource::Kind::Camera:resource::Kind::Microphone;
        auto choices=resource::enumerate(kind);auto selected=std::find_if(choices.begin(),choices.end(),[&](const auto& c){return c.id==options_.sourceId;});
        require(selected!=choices.end(),"Selected source is no longer connected");require(selected->available,"Selected source is unavailable");
        auto choice=*selected;
        if(kind==resource::Kind::Camera&&options_.width){require(choice.width==options_.width&&choice.height==options_.height&&choice.fps==options_.fps,"Camera mode changed; select its source again");}
        if(callbacks_.status)callbacks_.status("Connecting...");
        std::shared_ptr<AdmissionLease> admission;std::string credential;
        if(options_.nativeTicket){auto ticket=dm::access::channelCredential(options_.code);require(ticket&&ticket->kind==static_cast<unsigned>(options_.kind)&&options_.target.address=="127.0.0.1","Invalid scoped native resource ticket");credential=options_.code;}else{admission=acquireAdmission(options_.target,options_.code,static_cast<unsigned>(options_.kind),[this]{return cancelled_.load();});credential=admission->credential;}
        struct WipeCredential{std::string& value;~WipeCredential(){std::fill(value.begin(),value.end(),0);}}wipeCredential{credential};
        std::fill(options_.code.begin(),options_.code.end(),0);options_.code.clear();
        auto cancelled=[this,&admission]{return cancelled_.load()||(admission&&!admission->alive());};
        auto stream=std::make_shared<Transport>(options_.target.address.c_str(),options_.target.local.c_str(),options_.target.port,credential);
        {std::lock_guard lock(mutex_);stream_=stream;}require(!cancelled(),"Connection cancelled");stream->connect();
        auto read=[&](std::span<uint8_t> output){require(!cancelled(),"Server admission ended");auto bytes=stream->read(output.size(),45);std::copy(bytes.begin(),bytes.end(),output.begin());};
        auto write=[&](std::span<const uint8_t> bytes){require(!cancelled(),"Server admission ended");stream->write(bytes);};
        media::Binding binding{};do{require(SecRandomCopyBytes(kSecRandomDefault,sizeof(binding),reinterpret_cast<uint8_t*>(&binding))==errSecSuccess,"Cannot create resource session identity");}while(!binding.resource||!binding.generation);
        if(kind==resource::Kind::Camera)write(camera_wire::encode({choice.width,choice.height,choice.fps,binding}));else write(microphone_wire::encode_offer(binding));
        wire::ControlChannel control(read,write);auto local=unavailable_capabilities();local[static_cast<unsigned>(options_.kind)].source={true,{}};
        auto payload=wire::encode_capabilities(local);control.send({{wire::Operation::capabilities,ResourceKind::display,0,static_cast<uint32_t>(payload.size())},std::move(payload)});
        auto reply=control.receive();require(reply.header.operation==wire::Operation::capabilities&&reply.header.kind==ResourceKind::display&&!reply.header.resource_id,"Invalid server capability response");
        auto available=can_connect(options_.kind,local,wire::decode_capabilities(reply.payload));if(!available.available)throw std::runtime_error(available.reason);
        payload=wire::encode_request({binding.resource,options_.kind,kind==resource::Kind::Camera?"camera":"microphone"});
        control.send({{wire::Operation::request,options_.kind,binding.resource,static_cast<uint32_t>(payload.size())},std::move(payload)});
        reply=control.receive();require(reply.header.operation==wire::Operation::grant&&reply.header.kind==options_.kind&&reply.header.resource_id==binding.resource&&reply.payload.empty(),"Server rejected this resource; check its receiving component");
        require(!cancelled(),"Connection cancelled");
        resource::Capture capture(choice,[this](std::int64_t capturedAt){return mute_.sampleMuted(capturedAt);});media::Channel camera(binding,read,write);microphone_wire::Channel microphone(binding,read,write);
        bool announced=false;auto announce=[&]{if(!announced){announced=true;if(callbacks_.connected)callbacks_.connected(choice.width,choice.height,choice.fps);}};
        MicrophoneDelivery pcm(binding,[this]{return mute_.current();},[&](const auto& frame){microphone.send(frame);announce();});uint64_t sequence=0;
        while(!cancelled()){
            auto packet=capture.nextPacket(cancelled);if(packet.bytes.empty())continue;
            if(kind==resource::Kind::Camera){camera.send({binding,sequence++,packet.timestamp100ns,packet.bytes});announce();}
            else pcm.push(packet.bytes,packet.muted,packet.discontinuity);
        }
        if(!cancelled_)ending="Server admission ended";
    }catch(const std::exception& error){if(!cancelled_)ending=error.what();}
    cancel();{std::lock_guard lock(mutex_);stream_.reset();}
    std::fill(options_.code.begin(),options_.code.end(),0);options_.code.clear();
    if(callbacks_.ended)callbacks_.ended(ending);
}}
}
