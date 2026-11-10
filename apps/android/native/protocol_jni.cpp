#include <jni.h>
#include <dm/video_protocol.hpp>
#include <dm/input_protocol.hpp>
#include <stdexcept>
#include <vector>
namespace {
void fail(JNIEnv* e, const std::exception& x) {
    e->ThrowNew(e->FindClass("java/lang/IllegalArgumentException"), x.what());
}
std::vector<uint8_t> bytes(JNIEnv* e, jbyteArray a) {
    if (!a) throw std::runtime_error("Missing protocol data");
    const auto n = e->GetArrayLength(a);
    if (n > 8192) throw std::runtime_error("Protocol structure too large");
    std::vector<uint8_t> v(n);
    e->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(v.data()));
    return v;
}
jbyteArray output(JNIEnv* e, std::span<const uint8_t> v) {
    auto a = e->NewByteArray(v.size());
    if (a) e->SetByteArrayRegion(a, 0, v.size(), reinterpret_cast<const jbyte*>(v.data()));
    return a;
}
jlongArray numbers(JNIEnv* e, std::initializer_list<jlong> v) {
    auto a = e->NewLongArray(v.size());
    if (a) e->SetLongArrayRegion(a, 0, v.size(), v.begin());
    return a;
}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_Protocol_request(JNIEnv* e, jclass, jint w,
                                                                                          jint h, jint hz, jint caps,
                                                                                          jint scale) {
    try {
        if (caps < 0 || caps > 15 || scale < 0 || scale > 2) throw std::runtime_error("Invalid client options");
        auto r = dm::video::wire::request;
        r[7] = caps ? 1 : 0;
        r[8] = caps;
        r[9] = scale;
        dm::video::wire::set_client_display(
            r, {static_cast<uint32_t>(w), static_cast<uint32_t>(h), static_cast<uint32_t>(hz)});
        return output(e, r);
    } catch (const std::exception& x) {
        fail(e, x);
        return nullptr;
    }
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_Protocol_customRequest(JNIEnv* e, jclass,
                                                                                                jint w, jint h, jint hz,
                                                                                                jint caps, jint scale) {
    try {
        if (caps < 0 || caps > 15 || scale < 0 || scale > 2) throw std::runtime_error("Invalid client options");
        auto r = dm::video::wire::request;
        r[7] = caps ? 1 : 0;
        r[8] = caps;
        r[9] = scale;
        dm::video::wire::set_client_display(
            r, {static_cast<uint32_t>(w), static_cast<uint32_t>(h), static_cast<uint32_t>(hz), true});
        return output(e, r);
    } catch (const std::exception& x) {
        fail(e, x);
        return nullptr;
    }
}
extern "C" JNIEXPORT jbyteArray JNICALL
Java_io_github_eekebin_extrapane_Protocol_existingScreenRequest(JNIEnv* e, jclass, jlong id, jint caps, jint scale) {
    try {
        if (id <= 0 || id > 0xffffffffLL || caps < 0 || caps > 15 || scale < 0 || scale > 2)
            throw std::runtime_error("Invalid existing-screen options");
        auto r = dm::video::wire::request;
        r[7] = caps ? 1 : 0;
        r[8] = caps;
        r[9] = scale;
        dm::video::wire::set_screen_request(r,
                                            {dm::video::wire::DisplayMode::existing_screen, static_cast<uint32_t>(id)});
        return output(e, r);
    } catch (const std::exception& x) {
        fail(e, x);
        return nullptr;
    }
}
extern "C" JNIEXPORT jlongArray JNICALL Java_io_github_eekebin_extrapane_Protocol_offer(JNIEnv* e, jclass,
                                                                                        jbyteArray a) {
    try {
        auto o = dm::video::wire::decode_offer(bytes(e, a));
        return numbers(e, {o.width, o.height, o.fps, o.frames, static_cast<jlong>(o.codec), o.input_caps,
                           static_cast<jlong>(o.input_epoch)});
    } catch (const std::exception& x) {
        fail(e, x);
        return nullptr;
    }
}
extern "C" JNIEXPORT jlongArray JNICALL Java_io_github_eekebin_extrapane_Protocol_frame(JNIEnv* e, jclass,
                                                                                        jbyteArray a) {
    try {
        auto h = dm::video::wire::decode_header(bytes(e, a));
        return numbers(e, {h.sequence, h.bytes, static_cast<jlong>(h.micros)});
    } catch (const std::exception& x) {
        fail(e, x);
        return nullptr;
    }
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_io_github_eekebin_extrapane_Protocol_input(JNIEnv* e, jclass, jlong epoch,
                                                                                        jlong sequence, jintArray a) {
    try {
        if (!a) throw std::runtime_error("Missing events");
        auto n = e->GetArrayLength(a);
        if (n % 15 || n > static_cast<jsize>(15 * dm::input::max_events))
            throw std::runtime_error("Input batch exceeds bounds");
        std::vector<jint> fields(n);
        e->GetIntArrayRegion(a, 0, n, fields.data());
        std::vector<dm::input::Event> events;
        for (int i = 0; i < n; i += 15) {
            const auto* p = fields.data() + i;
            dm::input::Event v;
            v.kind = static_cast<dm::input::Kind>(p[0]);
            v.action = static_cast<dm::input::Action>(p[1]);
            v.flags = p[2];
            v.id = p[3];
            v.x = p[4];
            v.y = p[5];
            v.code = p[6];
            v.value = p[7];
            v.pressure = p[8];
            v.tilt_x = p[9];
            v.tilt_y = p[10];
            v.rotation = p[11];
            v.mask = p[12];
            v.contact_x = p[13];
            v.contact_y = p[14];
            events.push_back(v);
        }
        auto payload = dm::input::encode_events(events);
        auto header = dm::input::encode_header(
            {static_cast<uint64_t>(epoch), static_cast<uint32_t>(sequence), static_cast<uint32_t>(events.size())});
        std::vector<uint8_t> result(header.begin(), header.end());
        result.insert(result.end(), payload.begin(), payload.end());
        return output(e, result);
    } catch (const std::exception& x) {
        fail(e, x);
        return nullptr;
    }
}
