#pragma once
#include <memory>
#include <functional>
#include <span>
#include <vector>
#include <dm/access.hpp>
namespace dm::mac {
class HostTls {
    std::unique_ptr<access::Stream> protected_;
    int seconds_ = 10;
    std::vector<uint8_t> readRaw(size_t, int);
    void writeRaw(std::span<const uint8_t>, int);
    struct State;
    std::unique_ptr<State> state_;
    access::Gate* gate_ = nullptr;
    uint64_t control_ = 0;

public:
    HostTls(int socket, std::function<bool()> cancelled, access::Gate* gate = nullptr);
    ~HostTls();
    std::vector<uint8_t> read(size_t count, int seconds = 2);
    void write(std::span<const uint8_t> data, int seconds = 2);
    bool openControl();
    std::vector<access::ChannelCredential> controlChannels(unsigned kind, std::optional<bool> input = {});
    std::optional<bool> authorizedDisplayInput() const;
    void requireResource(unsigned kind) const;
};
}
