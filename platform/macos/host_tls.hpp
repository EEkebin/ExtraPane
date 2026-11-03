#pragma once
#include <memory>
#include <functional>
#include <span>
#include <vector>
namespace dm::mac {
class HostTls {
    struct State;std::unique_ptr<State> state_;
public:
    HostTls(int socket,std::function<bool()> cancelled);
    ~HostTls();
    std::vector<uint8_t> read(size_t count,int seconds=2);
    void write(std::span<const uint8_t> data,int seconds=2);
};
}
