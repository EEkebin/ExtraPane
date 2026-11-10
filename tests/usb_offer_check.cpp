#include <extrapane/usb_offer.hpp>
#include <iostream>
#include <stdexcept>
using namespace extrapane::usb::wire;
void check(bool value) {
    if (!value) throw std::runtime_error("USB offer check failed");
}
template <class F>
void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected);
}
int main() {
    Offer source;
    source.bus = "1-12";
    source.vendor = 0xabcd;
    source.product = 0x1234;
    for (unsigned i = 0; i < source.nonce.size(); ++i) source.nonce[i] = static_cast<std::uint8_t>(i + 1);
    for (auto kind : {DeviceKind::storage, DeviceKind::generic}) {
        for (unsigned message : {0u, 2u}) {
            const auto hello = greeting(message, kind);
            check(decode_greeting(hello, message) == kind);
            auto bad = hello;
            bad[4] = 3;
            rejects([&] { decode_greeting(bad, message); });
            bad = hello;
            bad[6] = 0;
            rejects([&] { decode_greeting(bad, message); });
            bad = hello;
            bad[7] = 1;
            rejects([&] { decode_greeting(bad, message); });
            rejects([&] { decode_greeting(hello, message == 0 ? 2 : 0); });
        }
        source.kind = kind;
        const auto bytes = encode(source);
        const auto copy = decode(bytes);
        check(copy.kind == kind && copy.nonce == source.nonce && copy.vendor == source.vendor &&
              copy.product == source.product && copy.bus == source.bus);
        for (unsigned version : {0u, 1u, 2u, 3u, 5u}) {
            auto bad = bytes;
            bad[4] = version;
            rejects([&] { decode(bad); });
        }
        for (unsigned invalid : {0u, 3u, 255u}) {
            auto bad = bytes;
            bad[6] = invalid;
            rejects([&] { decode(bad); });
        }
        for (unsigned offset : {0u, 5u, 7u, 63u}) {
            auto bad = bytes;
            bad[offset] ^= 0x80;
            rejects([&] { decode(bad); });
        }
        for (unsigned size : {0u, 16u, 255u}) {
            auto bad = bytes;
            bad[28] = size;
            rejects([&] { decode(bad); });
        }
        auto bad = bytes;
        bad[29] = 'x';
        rejects([&] { decode(bad); });
        bad = bytes;
        bad[29] = '-';
        rejects([&] { decode(bad); });
        bad = bytes;
        bad[32] = '-';
        rejects([&] { decode(bad); });
        const auto proof = receipt(source);
        auto changed = source;
        changed.nonce[0] ^= 1;
        check(receipt(changed) != proof);
        changed = source;
        changed.vendor ^= 1;
        check(receipt(changed) != proof);
        changed = source;
        changed.product ^= 1;
        check(receipt(changed) != proof);
        changed = source;
        changed.kind = kind == DeviceKind::storage ? DeviceKind::generic : DeviceKind::storage;
        check(receipt(changed) != proof);
    }
    source.bus = "";
    rejects([&] { encode(source); });
    source.bus = std::string(16, '1');
    rejects([&] { encode(source); });
    source.bus = "1-2";
    source.kind = static_cast<DeviceKind>(255);
    rejects([&] { encode(source); });
    rejects([] { greeting(0, static_cast<DeviceKind>(255)); });
    rejects([] { greeting(1, DeviceKind::generic); });
    std::cout << "USB_OFFER_CHECK_OK\n";
}
