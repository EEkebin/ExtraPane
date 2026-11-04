#pragma once
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <cstdint>
namespace dm::access {
using Bytes=std::vector<uint8_t>;
using Read=std::function<void(std::span<uint8_t>)>;
using Write=std::function<void(std::span<const uint8_t>)>;
using Clock=std::chrono::steady_clock;
bool validCode(const std::string& code);
class Gate {
    std::mutex mutex_;std::string code_;Clock::time_point expires_{};uint64_t generation_=0;unsigned attempts_=0;bool used_=true;
public:
    struct Ticket {std::string code;uint64_t generation;};
    ~Gate();
    std::string generate();
    Ticket begin();
    void consume(uint64_t generation);
    void revoke();
};
// Shared across TLS backends. Read/write callbacks must enforce deadlines.
class Stream {
    struct State;std::unique_ptr<State> state_;
public:
    Stream(Read read,Write write);
    ~Stream();
    Stream(const Stream&)=delete;Stream& operator=(const Stream&)=delete;
    void client(const std::string& code);
    void server(Gate& gate);
    void read(std::span<uint8_t> bytes);
    void write(std::span<const uint8_t> bytes);
};
}
