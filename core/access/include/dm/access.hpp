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
#include <map>
#include <optional>
namespace dm::access {
using Bytes=std::vector<uint8_t>;
using Read=std::function<void(std::span<uint8_t>)>;
using Write=std::function<void(std::span<const uint8_t>)>;
using Clock=std::chrono::steady_clock;
bool validCode(const std::string& code);
struct ChannelCredential {unsigned kind=0;std::string id,secret;};
std::optional<ChannelCredential> channelCredential(const std::string& text);
bool validCredential(const std::string& text);
std::string formatCredential(const ChannelCredential& credential);
class Gate {
    std::function<Clock::time_point()> now_;
    std::mutex mutex_;std::string code_;Clock::time_point expires_{};uint64_t generation_=0;unsigned attempts_=0;bool used_=true;
    struct Channel {std::string secret;unsigned kind=0,attempts=0;Clock::time_point expires;uint64_t generation=0,control=0;std::optional<bool> display_input;};
    std::map<std::string,Channel> channels_;
    std::map<uint64_t,bool> controls_;uint64_t next_control_=0;
    std::vector<ChannelCredential> issue_locked(unsigned mask,uint64_t control,std::optional<bool> display_input={});
    void clear_channels();
public:
    explicit Gate(std::function<Clock::time_point()> now=Clock::now):now_(std::move(now)){}
    enum class Status { active,used,expired,exhausted };
    Status status();
    struct Ticket {std::string code;uint64_t generation;std::optional<bool> display_input;};
    ~Gate();
    std::string generate();
    Ticket begin();
    void consume(uint64_t generation);
    void revoke();
    std::vector<ChannelCredential> issue_channels(unsigned mask,uint64_t admitted_generation);
    uint64_t open_control(uint64_t admitted_generation);
    std::vector<ChannelCredential> control_channels(unsigned mask,uint64_t control,std::optional<bool> display_input={});
    void close_control(uint64_t control);
    Ticket begin_channel(const std::string& id,unsigned kind);
    void consume_channel(const std::string& id,unsigned kind,uint64_t generation);
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
    // Peer control uses its own protocol domain and authenticated transcript.
    // Secret is a six-digit pairing code or a 64-hex remembered random key.
    void peer_client(const std::string& secret,std::span<const uint8_t> transcript);
    void peer_server(const std::string& secret,std::span<const uint8_t> transcript);
    std::optional<unsigned> authorized_kind()const;
    std::optional<bool> authorized_display_input()const;
    std::vector<ChannelCredential> issue_channels(Gate& gate,unsigned mask);
    uint64_t open_control(Gate& gate);
    void read(std::span<uint8_t> bytes);
    void write(std::span<const uint8_t> bytes);
};
}
