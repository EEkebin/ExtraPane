#pragma once
#include <dm/control.hpp>
#include <functional>
#include <span>
#include <chrono>
#include <mutex>
namespace dm::linux_client {
class Tls {
    dm::access::ControlOperation op_ = dm::access::ControlOperation::open;
    unsigned kind_ = 0;

public:
    inline static bool malformed = false;
    inline static std::mutex mutex;
    inline static std::vector<unsigned> kinds;
    Tls(const std::string&, const std::string&, uint16_t, std::function<bool()>, unsigned, const std::string& code) {
        if (code != "123456") throw std::runtime_error("Unexpected secret");
    }
    void write(std::span<const uint8_t> data, std::chrono::steady_clock::time_point) {
        dm::access::ControlRequest request{};
        if (data.size() != request.size()) throw std::runtime_error("Bad request size");
        std::copy(data.begin(), data.end(), request.begin());
        const auto parsed = dm::access::parse_control(request);
        op_ = parsed.first;
        kind_ = parsed.second;
        if (op_ == dm::access::ControlOperation::add) {
            std::lock_guard lock(mutex);
            kinds.push_back(kind_);
        }
    }
    void read(std::span<uint8_t> data, std::chrono::steady_clock::time_point) {
        auto reply = dm::access::control_reply(op_, kind_, true,
                                               op_ == dm::access::ControlOperation::add
                                                   ? "EPCT1:" + std::to_string(kind_) + ":" + std::string(32, 'a') +
                                                         ":" + std::string(64, 'b')
                                                   : std::string{});
        if (malformed) reply[0] = 0;
        if (data.size() != reply.size()) throw std::runtime_error("Bad reply size");
        std::copy(reply.begin(), reply.end(), data.begin());
    }
};
}
