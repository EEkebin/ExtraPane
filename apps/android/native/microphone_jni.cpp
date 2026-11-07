#include <jni.h>
#include <extrapane/microphone_protocol.hpp>
#include <extrapane/channel.hpp>
#include <stdexcept>
namespace {
using Bytes=extrapane::wire::Bytes;
void fail(JNIEnv* env,const std::exception& error){if(!env->ExceptionCheck())env->ThrowNew(env->FindClass("java/io/IOException"),error.what());}
Bytes bytes(JNIEnv* env,jbyteArray array,std::size_t maximum){
    if(!array)throw std::invalid_argument("Missing microphone message");auto size=env->GetArrayLength(array);
    if(static_cast<std::size_t>(size)>maximum)throw std::invalid_argument("Microphone message exceeds bounds");Bytes result(size);
    env->GetByteArrayRegion(array,0,size,reinterpret_cast<jbyte*>(result.data()));return result;
}
jbyteArray output(JNIEnv* env,std::span<const std::uint8_t> bytes){auto result=env->NewByteArray(static_cast<jsize>(bytes.size()));if(result)env->SetByteArrayRegion(result,0,static_cast<jsize>(bytes.size()),reinterpret_cast<const jbyte*>(bytes.data()));return result;}
Bytes message(extrapane::wire::Operation operation,extrapane::ResourceKind kind,std::uint64_t resource,Bytes payload){
    auto header=extrapane::wire::encode({operation,kind,resource,static_cast<std::uint32_t>(payload.size())});Bytes result(header.begin(),header.end());result.insert(result.end(),payload.begin(),payload.end());return result;
}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_MicrophoneProtocol_hello(JNIEnv* env,jclass,jlong resource,jlong generation){try{return output(env,extrapane::microphone_wire::encode_offer({static_cast<std::uint64_t>(resource),static_cast<std::uint64_t>(generation)}));}catch(const std::exception& e){fail(env,e);return nullptr;}}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_MicrophoneProtocol_capabilities(JNIEnv* env,jclass){try{
    auto caps=extrapane::unavailable_capabilities();caps[2].source={true,{}};return output(env,message(extrapane::wire::Operation::capabilities,extrapane::ResourceKind::display,0,extrapane::wire::encode_capabilities(caps)));
}catch(const std::exception& e){fail(env,e);return nullptr;}}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_MicrophoneProtocol_request(JNIEnv* env,jclass,jlong resource){try{
    auto id=static_cast<std::uint64_t>(resource);return output(env,message(extrapane::wire::Operation::request,extrapane::ResourceKind::microphone,id,extrapane::wire::encode_request({id,extrapane::ResourceKind::microphone,"microphone"})));
}catch(const std::exception& e){fail(env,e);return nullptr;}}
extern "C" JNIEXPORT jint JNICALL Java_io_github_eekebin_extrapane_MicrophoneProtocol_payloadLength(JNIEnv* env,jclass,jbyteArray header){try{return extrapane::wire::decode(bytes(env,header,24)).payload_bytes;}catch(const std::exception& e){fail(env,e);return 0;}}
extern "C" JNIEXPORT void JNICALL Java_io_github_eekebin_extrapane_MicrophoneProtocol_validateCapabilities(JNIEnv* env,jclass,jbyteArray header,jbyteArray payload){try{
    auto h=extrapane::wire::decode(bytes(env,header,24));auto body=bytes(env,payload,65536);
    if(h.operation!=extrapane::wire::Operation::capabilities||h.kind!=extrapane::ResourceKind::display||h.resource_id||h.payload_bytes!=body.size())throw std::invalid_argument("Invalid microphone capability response");
    if(!extrapane::wire::decode_capabilities(body)[2].destination.available)throw std::runtime_error("Server microphone endpoint is unavailable; install its microphone component first");
}catch(const std::exception& e){fail(env,e);}}
extern "C" JNIEXPORT void JNICALL Java_io_github_eekebin_extrapane_MicrophoneProtocol_validateGrant(JNIEnv* env,jclass,jbyteArray header,jbyteArray payload,jlong resource){try{
    auto h=extrapane::wire::decode(bytes(env,header,24));auto body=bytes(env,payload,65536);
    if(h.kind!=extrapane::ResourceKind::microphone||h.resource_id!=static_cast<std::uint64_t>(resource)||h.payload_bytes!=body.size())throw std::invalid_argument("Invalid microphone grant response");
    if(h.operation==extrapane::wire::Operation::reject)throw std::runtime_error("Server could not start its microphone endpoint; check the server status");
    if(h.operation!=extrapane::wire::Operation::grant||!body.empty())throw std::invalid_argument("Microphone request was not granted");
}catch(const std::exception& e){fail(env,e);}}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_MicrophoneProtocol_frameHeader(JNIEnv* env,jclass,jlong resource,jlong generation,jlong sequence,jlong position,jboolean muted,jboolean discontinuity){try{
    if(resource<=0||generation<=0||sequence<0||position<0)throw std::invalid_argument("Invalid microphone frame");
    extrapane::microphone_wire::Frame frame;frame.binding={static_cast<std::uint64_t>(resource),static_cast<std::uint64_t>(generation)};
    frame.sequence=static_cast<std::uint64_t>(sequence);frame.position=static_cast<std::uint64_t>(position);frame.muted=muted;frame.discontinuity=discontinuity;return output(env,extrapane::microphone_wire::encode_header(frame));
}catch(const std::exception& e){fail(env,e);return nullptr;}}