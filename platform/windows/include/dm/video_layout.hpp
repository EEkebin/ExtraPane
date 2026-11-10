#pragma once
#include <algorithm>
#include <cstdint>
namespace dm::video {
struct ContentRect {
    unsigned x, y, width, height;
};
inline ContentRect content_rect(unsigned sw, unsigned sh, unsigned tw, unsigned th) {
    if (!sw || !sh || !tw || !th) return {};
    unsigned w = tw, h = th;
    if (static_cast<std::uint64_t>(tw) * sh <= static_cast<std::uint64_t>(th) * sw)
        h = std::max(1u, static_cast<unsigned>(static_cast<std::uint64_t>(tw) * sh / sw));
    else
        w = std::max(1u, static_cast<unsigned>(static_cast<std::uint64_t>(th) * sw / sh));
    return {(tw - w) / 2, (th - h) / 2, w, h};
}
}
