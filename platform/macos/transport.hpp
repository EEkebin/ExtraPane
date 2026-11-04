#pragma once
#import <Network/Network.h>
#include <span>
#include <vector>
#include <memory>
#include <atomic>
#include <dm/access.hpp>
namespace dm::mac {
class Transport {
    std::unique_ptr<access::Stream> protected_;std::string code_;int seconds_=10;
    std::vector<uint8_t> readRaw(size_t,int);void writeRaw(std::span<const uint8_t>);
    nw_connection_t connection_;
    dispatch_queue_t queue_;
public:
    Transport(const char* peer,const char* local,unsigned port=43824,std::string code={});
    ~Transport();
    void connect();
    void cancel();
    std::vector<uint8_t> read(size_t count,int seconds=15);
    void write(std::span<const uint8_t> bytes);
};
bool ipv4(const char* value);
}
