#pragma once
#import <Network/Network.h>
#include <span>
#include <vector>
#include <memory>
#include <atomic>
namespace dm::mac {
class Transport {
    nw_connection_t connection_;
    dispatch_queue_t queue_;
public:
    Transport(const char* peer,const char* local);
    ~Transport();
    void connect();
    void cancel();
    std::vector<uint8_t> read(size_t count,int seconds=15);
    void write(std::span<const uint8_t> bytes);
};
bool ipv4(const char* value);
}
