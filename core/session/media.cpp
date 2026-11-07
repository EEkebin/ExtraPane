#include <extrapane/media.hpp>
#include <bit>
#include <stdexcept>

namespace extrapane::media {
namespace {
void valid_binding(Binding b) {
    if (!b.resource || !b.generation) throw std::invalid_argument("Missing media binding");
}
void put(Header& h, std::size_t offset, std::uint64_t value, std::size_t count) {
    for (std::size_t i=0; i<count; ++i) h[offset+count-1-i]=static_cast<std::uint8_t>(value>>(i*8));
}
std::uint64_t get(std::span<const std::uint8_t> h, std::size_t offset, std::size_t count) {
    std::uint64_t value=0;
    for (std::size_t i=0; i<count; ++i) value=(value<<8)|h[offset+i];
    return value;
}
}
Header encode_header(const Frame& f) {
    valid_binding(f.binding);
    if (f.payload.empty() || f.payload.size()>max_frame_bytes) throw std::invalid_argument("Invalid camera frame size");
    Header h{};
    h[0]='E'; h[1]='P'; h[2]='M'; h[3]='F'; h[5]=1; h[6]=1; // v1, H.264
    put(h,8,f.binding.resource,8); put(h,16,f.binding.generation,8);
    put(h,24,f.sequence,8); put(h,32,std::bit_cast<std::uint64_t>(f.timestamp),8);
    put(h,40,f.payload.size(),4);
    return h;
}
Frame decode_header(std::span<const std::uint8_t> h) {
    if (h.size()!=48 || h[0]!='E' || h[1]!='P' || h[2]!='M' || h[3]!='F' ||
        h[4]!=0 || h[5]!=1 || h[6]!=1 || h[7]!=0 || get(h,44,4)!=0)
        throw std::invalid_argument("Invalid camera frame header");
    Frame f{{get(h,8,8),get(h,16,8)},get(h,24,8),std::bit_cast<std::int64_t>(get(h,32,8)),{}};
    valid_binding(f.binding);
    const auto size=get(h,40,4);
    if (!size || size>max_frame_bytes) throw std::invalid_argument("Invalid camera frame length");
    f.payload.resize(static_cast<std::size_t>(size));
    return f;
}
Channel::Channel(Binding b, wire::ControlChannel::Read r, wire::ControlChannel::Write w)
    :binding_(b),read_(std::move(r)),write_(std::move(w)) {
    valid_binding(b);
    if (!read_ || !write_) throw std::invalid_argument("Missing encrypted media transport");
}
void Channel::validate_sequence(const Frame& f,const std::optional<std::uint64_t>& previous) const {
    if (f.binding!=binding_ || (previous && f.sequence<=*previous))
        throw std::invalid_argument("Stale or misrouted camera frame");
}
void Channel::send(const Frame& f) {
    if (failed_) throw std::runtime_error("Media channel closed");
    try {
        validate_sequence(f,sent_); auto h=encode_header(f);
        write_(h); write_(f.payload); sent_=f.sequence;
    } catch (...) { failed_=true; throw; }
}
Frame Channel::receive() {
    if (failed_) throw std::runtime_error("Media channel closed");
    try {
        Header h; read_(h); auto f=decode_header(h); validate_sequence(f,received_);
        read_(f.payload); received_=f.sequence; return f;
    } catch (...) { failed_=true; throw; }
}
FrameQueue::FrameQueue(Binding b,std::size_t capacity):binding_(b),capacity_(capacity) {
    valid_binding(b);
    if (!capacity || capacity>8) throw std::invalid_argument("Invalid camera queue capacity");
}
bool FrameQueue::push(Frame f) {
    std::lock_guard lock(mutex_);
    if (closed_) return false;
    if (f.binding!=binding_ || f.payload.empty() || f.payload.size()>max_frame_bytes ||
        (last_ && f.sequence<=*last_) || frames_.size()==capacity_) {
        closed_=true; frames_.clear(); ready_.notify_all(); return false;
    }
    last_=f.sequence; frames_.push_back(std::move(f)); ready_.notify_one(); return true;
}
std::optional<Frame> FrameQueue::pop_for(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    ready_.wait_for(lock,timeout,[&]{return closed_ || !frames_.empty();});
    if (closed_ || frames_.empty()) return std::nullopt;
    auto frame=std::move(frames_.front()); frames_.pop_front(); return frame;
}
void FrameQueue::close() noexcept {
    std::lock_guard lock(mutex_); closed_=true; frames_.clear(); ready_.notify_all();
}
bool FrameQueue::closed() const { std::lock_guard lock(mutex_); return closed_; }
}
