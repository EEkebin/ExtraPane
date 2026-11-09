#pragma once
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace dm::microphone {
struct FanoutFrame {
    std::uint64_t position=0;
    bool muted=false,discontinuity=false;
    std::array<std::uint8_t,960> pcm{};
};
// Each recipient owns its own queue. Capture never waits for network I/O; a
// slow recipient loses oldest audio with an explicit discontinuity indication.
class FanoutQueue {
public:
    static constexpr std::size_t capacity=6; // Six 10 ms PCM packets.
    void push(FanoutFrame frame){
        std::lock_guard lock(mutex_);if(closed_)return;
        if(queue_.size()==capacity){queue_.pop_front();gap_=true;}
        if(muted_){frame.muted=true;frame.pcm.fill(0);}
        queue_.push_back(std::move(frame));changed_.notify_one();
    }
    std::optional<FanoutFrame> pop(std::chrono::milliseconds timeout){
        std::unique_lock lock(mutex_);changed_.wait_for(lock,timeout,[&]{return closed_||!queue_.empty();});
        if(queue_.empty())return {};auto frame=std::move(queue_.front());queue_.pop_front();frame.discontinuity|=gap_;gap_=false;return frame;
    }
    void mute(bool value){std::lock_guard lock(mutex_);if(muted_!=value){muted_=value;queue_.clear();gap_=true;}}
    void close(){std::lock_guard lock(mutex_);closed_=true;queue_.clear();changed_.notify_all();}
private:
    std::mutex mutex_;std::condition_variable changed_;std::deque<FanoutFrame> queue_;bool muted_=false,gap_=false,closed_=false;
};
}
