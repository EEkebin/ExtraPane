#include <jni.h>
#include <dm/control.hpp>
#include <algorithm>
#include <stdexcept>

namespace {
void fail(JNIEnv* env, const std::exception& error) {
    if (!env->ExceptionCheck()) env->ThrowNew(env->FindClass("java/io/IOException"), error.what());
}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_AdmissionBroker_request(JNIEnv* env, jclass,
                                                                                                 jint operation,
                                                                                                 jint kind) {
    try {
        if (operation < 1 || operation > 4 || kind < 0 || kind > 4)
            throw std::invalid_argument("Invalid admission request");
        auto bytes = dm::access::control_request(static_cast<dm::access::ControlOperation>(operation),
                                                 static_cast<unsigned>(kind));
        auto result = env->NewByteArray(static_cast<jsize>(bytes.size()));
        if (result)
            env->SetByteArrayRegion(result, 0, static_cast<jsize>(bytes.size()),
                                    reinterpret_cast<const jbyte*>(bytes.data()));
        return result;
    } catch (const std::exception& error) {
        fail(env, error);
        return nullptr;
    }
}
extern "C" JNIEXPORT jstring JNICALL Java_io_github_eekebin_extrapane_AdmissionBroker_credential(
    JNIEnv* env, jclass, jbyteArray input, jint operation, jint kind, jboolean protectedMode) {
    try {
        dm::access::ControlReply bytes{};
        if (!input || env->GetArrayLength(input) != static_cast<jsize>(bytes.size()) || operation < 1 ||
            operation > 4 || kind < 0 || kind > 4)
            throw std::invalid_argument("Invalid admission response");
        env->GetByteArrayRegion(input, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<jbyte*>(bytes.data()));
        if (env->ExceptionCheck()) return nullptr;
        auto ticket = dm::access::control_credential(bytes, static_cast<dm::access::ControlOperation>(operation),
                                                     static_cast<unsigned>(kind), protectedMode);
        auto result = env->NewStringUTF(ticket.c_str());
        volatile char* secret = ticket.data();
        for (std::size_t i = 0; i < ticket.size(); ++i) secret[i] = 0;
        volatile uint8_t* record = bytes.data();
        for (std::size_t i = 0; i < bytes.size(); ++i) record[i] = 0;
        return result;
    } catch (const std::exception& error) {
        fail(env, error);
        return nullptr;
    }
}
