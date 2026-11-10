#include <extrapane/usb_descriptors.hpp>
#include <iostream>
#include <functional>
using namespace extrapane::usb;
using Bytes = std::vector<std::uint8_t>;
void need(bool b) {
    if (!b) throw std::runtime_error("Descriptor test failed");
}
void rejected(const std::function<void()>& f) {
    bool threw = false;
    try {
        f();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    need(threw);
}
Bytes dev(unsigned configs = 1) {
    return {18, 1, 0, 2, 0, 0, 0, 64, 0x51, 0x31, 0x20, 0x30, 0, 1, 0, 0, 0, std::uint8_t(configs)};
}
Bytes composite() {
    return {9, 2, 41, 0,  2, 1, 0, 0x80, 50, 9, 4, 0, 0, 1, 3, 1,    2, 0, 7, 5, 0x81,
            3, 8, 0,  10, 9, 4, 1, 0,    1,  3, 0, 0, 0, 7, 5, 0x82, 3, 8, 0, 10};
}
int main() {
    auto d = dev();
    auto c = composite();
    auto result = validate_device_descriptors(d, {c});
    need(result.hid && result.composite && !result.audio);
    for (unsigned cls : {8u, 9u, 0xe0u, 0xffu, 2u}) {
        auto bad = c;
        bad[30] = cls;
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    {
        auto bad = c;
        bad[4] = 3;
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    {
        auto bad = c;
        bad[0] = 8;
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    for (std::size_t n = 0; n < c.size(); ++n) {
        Bytes bad(c.begin(), c.begin() + n);
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    {
        auto bad = c;
        bad[27] = 0;
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    {
        auto bad = c;
        bad[28] = 1;
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    {
        auto bad = c;
        bad[29] = 2;
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    {
        auto bad = c;
        bad[25] = 0;
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    {
        auto two = dev(2);
        rejected([&] { validate_device_descriptors(two, {c}); });
    }
    {
        auto two = dev(2);
        auto other = c;
        other[5] = 2;
        other[30] = 8;
        rejected([&] { validate_device_descriptors(two, {c, other}); });
    }
    {
        auto bad = c;
        bad.insert(bad.end(), {9, 4, 1, 1, 0, 8, 0, 0, 0});
        bad[2] = std::uint8_t(bad.size());
        rejected([&] { validate_device_descriptors(d, {bad}); });
    }
    {
        auto audio = c;
        audio[14] = 1;
        audio[30] = 1;
        auto a = validate_device_descriptors(d, {audio});
        need(a.audio && !a.hid);
    }
    {
        auto b = d;
        b[4] = 0xe0;
        rejected([&] { validate_device_descriptors(b, {c}); });
    }
    std::cout << "USB_DESCRIPTOR_CHECK_OK\n";
}
