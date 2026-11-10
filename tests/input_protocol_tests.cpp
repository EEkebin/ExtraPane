#include <dm/input_protocol.hpp>
#include <dm/video_protocol.hpp>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok) {
    if (!ok) throw std::runtime_error("Input protocol assertion failed");
}
template <class F>
void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed input accepted");
}
}
int main() {
    try {
        using namespace dm::input;
        auto header = encode_header({0xfedcba9876543210, 0x98765432, 128});
        auto h = decode_header(header);
        require(h.epoch == 0xfedcba9876543210 && h.sequence == 0x98765432 && h.count == 128);
        rejects([] { encode_header({0, 0, 0}); });
        rejects([] { encode_header({1, 0, 129}); });
        header[24] = 1;
        rejects([&] { decode_header(header); });
        Event pen;
        pen.kind = Kind::pen;
        pen.action = Action::down;
        pen.flags = 3;
        pen.id = 123;
        pen.x = 65535;
        pen.y = 34567;
        pen.mask = 15;
        pen.code = 7;
        pen.pressure = 937;
        pen.tilt_x = -75;
        pen.tilt_y = 80;
        pen.rotation = 359;
        pen.micros = 0x123456789abcdef0;
        auto bytes = encode_events(std::span(&pen, 1));
        const auto restored = decode_events(bytes, 1).at(0);
        require(restored.tilt_x == -75 && restored.tilt_y == 80 && restored.pressure == 937 && restored.code == 7 &&
                restored.mask == 15 && restored.micros == pen.micros && restored.x == 65535);
        bytes[52] = 1;
        rejects([&] { decode_events(bytes, 1); });
        rejects([&] { decode_events(bytes, 2); });
        auto bad = pen;
        bad.pressure = 1025;
        rejects([&] { encode_events(std::span(&bad, 1)); });
        bad = pen;
        bad.tilt_x = -91;
        rejects([&] { validate(bad); });
        bad = pen;
        bad.flags = 2;
        rejects([&] { validate(bad); });
        bad = pen;
        bad.action = Action::up;
        rejects([&] { validate(bad); });
        Event key;
        key.kind = Kind::key;
        key.id = 65;
        key.code = 30;
        key.action = Action::down;
        key.value = 1;
        auto data = encode_events(std::span(&key, 1));
        require(decode_events(data, 1)[0].code == 30);
        key.code = 0;
        rejects([&] { validate(key); });
        Event touch = pen;
        touch.kind = Kind::touch;
        touch.mask = 7;
        touch.code = 0;
        touch.tilt_x = touch.tilt_y = 0;
        touch.contact_x = 1000;
        touch.contact_y = 2000;
        validate(touch);
        touch.mask = 8;
        rejects([&] { validate(touch); });
        Event wheel;
        wheel.kind = Kind::mouse;
        wheel.action = Action::wheel;
        wheel.value = -120;
        data = encode_events(std::span(&wheel, 1));
        require(decode_events(data, 1)[0].value == -120);
        wheel.code = 2;
        rejects([&] { validate(wheel); });
        auto offer = dm::video::wire::encode_offer({1280, 720, 30, 300, dm::video::wire::Codec::h264, 15, 1234});
        require(dm::video::wire::decode_offer(offer).input_epoch == 1234);
        offer[7] = 16;
        rejects([&] { dm::video::wire::decode_offer(offer); });
        std::cout << "Input event bounds, signed axes, masks, epoch and permission checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
