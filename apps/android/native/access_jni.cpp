#include <jni.h>
#include <dm/access.hpp>
#include <algorithm>
#include <stdexcept>
namespace {
struct Native {
    JNIEnv* env; jobject input=nullptr,output=nullptr;jmethodID readID=nullptr,writeID=nullptr;
    std::unique_ptr<dm::access::Stream> stream;
    Native(JNIEnv* e,jobject in,jobject out):env(e){
        input=e->NewGlobalRef(in);output=e->NewGlobalRef(out);
        auto inClass=e->GetObjectClass(in),outClass=e->GetObjectClass(out);
        readID=e->GetMethodID(inClass,"readFully","([B)V");writeID=e->GetMethodID(outClass,"write","([B)V");e->DeleteLocalRef(inClass);e->DeleteLocalRef(outClass);
        if(!input||!output||!readID||!writeID){if(input)e->DeleteGlobalRef(input);if(output)e->DeleteGlobalRef(output);input=output=nullptr;throw std::runtime_error("Cannot create access transport");}
        stream=std::make_unique<dm::access::Stream>([this](std::span<uint8_t> bytes){
            auto a=env->NewByteArray(static_cast<jsize>(bytes.size()));if(!a)throw std::runtime_error("Cannot allocate input record");
            env->CallVoidMethod(input,readID,a);if(!env->ExceptionCheck())env->GetByteArrayRegion(a,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));env->DeleteLocalRef(a);
            if(env->ExceptionCheck())throw std::runtime_error("Access transport read failed");
        },[this](std::span<const uint8_t> bytes){
            auto a=env->NewByteArray(static_cast<jsize>(bytes.size()));if(!a)throw std::runtime_error("Cannot allocate output record");
            env->SetByteArrayRegion(a,0,static_cast<jsize>(bytes.size()),reinterpret_cast<const jbyte*>(bytes.data()));if(!env->ExceptionCheck())env->CallVoidMethod(output,writeID,a);env->DeleteLocalRef(a);
            if(env->ExceptionCheck())throw std::runtime_error("Access transport write failed");
        });
    }
    ~Native(){stream.reset();if(input)env->DeleteGlobalRef(input);if(output)env->DeleteGlobalRef(output);}
};
void fail(JNIEnv* e,const std::exception& x){if(!e->ExceptionCheck())e->ThrowNew(e->FindClass("java/io/IOException"),x.what());}
Native& state(jlong h){if(!h)throw std::runtime_error("Access channel closed");return *reinterpret_cast<Native*>(h);}
}
extern "C" JNIEXPORT jlong JNICALL Java_io_github_eekebin_extrapane_AccessChannel_open(JNIEnv* e,jclass,jobject in,jobject out,jstring text){
    try{if(!in||!out||!text)throw std::runtime_error("Missing access parameters");const char* chars=e->GetStringUTFChars(text,nullptr);if(!chars)throw std::runtime_error("Missing access code");std::string code(chars);e->ReleaseStringUTFChars(text,chars);
        struct Wipe{std::string& text;~Wipe(){volatile char* p=text.data();for(size_t i=0;i<text.size();++i)p[i]=0;}}wipe{code};auto native=std::make_unique<Native>(e,in,out);native->stream->client(code);std::fill(code.begin(),code.end(),0);return reinterpret_cast<jlong>(native.release());
    }catch(const std::exception& x){fail(e,x);return 0;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_AccessChannel_receive(JNIEnv* e,jclass,jlong h,jint count){
    try{if(count<0||count>65536)throw std::runtime_error("Access input exceeds bounds");dm::access::Bytes bytes(count);state(h).stream->read(bytes);auto a=e->NewByteArray(count);if(a)e->SetByteArrayRegion(a,0,count,reinterpret_cast<const jbyte*>(bytes.data()));return a;}catch(const std::exception& x){fail(e,x);return nullptr;}
}
extern "C" JNIEXPORT void JNICALL Java_io_github_eekebin_extrapane_AccessChannel_send(JNIEnv* e,jclass,jlong h,jbyteArray a){
    try{if(!a||e->GetArrayLength(a)>65536)throw std::runtime_error("Access output exceeds bounds");dm::access::Bytes bytes(e->GetArrayLength(a));e->GetByteArrayRegion(a,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));if(e->ExceptionCheck())return;state(h).stream->write(bytes);}catch(const std::exception& x){fail(e,x);}
}
extern "C" JNIEXPORT void JNICALL Java_io_github_eekebin_extrapane_AccessChannel_destroy(JNIEnv*,jclass,jlong h){delete reinterpret_cast<Native*>(h);}
