#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace dm::protocol {
// Bounded lookahead above an already admitted byte stream. Callbacks must fill
// the entire requested span or throw. One connection executor owns this object.
class PrefixBuffer final {
public:
    static constexpr std::size_t capacity = 8;
    template <class Read>
    void peek(std::span<std::uint8_t> bytes, Read&& source) {
        require_live();
        if (bytes.size() > capacity) throw std::invalid_argument("Session prefix exceeds lookahead limit");
        auto available = end_ - begin_;
        if (bytes.size() > available) {
            std::move(buffer_.begin() + begin_, buffer_.begin() + end_, buffer_.begin());
            begin_ = 0;
            end_ = available;
            try {
                source(std::span(buffer_).subspan(end_, bytes.size() - available));
            } catch (...) {
                failed_ = true;
                throw;
            }
            end_ = bytes.size();
        }
        std::copy_n(buffer_.begin() + begin_, bytes.size(), bytes.begin());
    }
    template <class Read>
    void read(std::span<std::uint8_t> bytes, Read&& source) {
        require_live();
        const auto count = std::min(bytes.size(), end_ - begin_);
        std::copy_n(buffer_.begin() + begin_, count, bytes.begin());
        begin_ += count;
        if (begin_ == end_) begin_ = end_ = 0;
        bytes = bytes.subspan(count);
        if (!bytes.empty()) try {
                source(bytes);
            } catch (...) {
                failed_ = true;
                throw;
            }
    }

private:
    void require_live() const {
        if (failed_) throw std::runtime_error("Session prefix stream already failed");
    }
    std::array<std::uint8_t, capacity> buffer_{};
    std::size_t begin_ = 0, end_ = 0;
    bool failed_ = false;
};
}
