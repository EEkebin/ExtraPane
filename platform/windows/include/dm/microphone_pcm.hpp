#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dm::microphone {
struct PcmFormat {
    unsigned rate = 48000, channels = 1, bits = 16;
    bool floating = false;
};
// Pure bounded conversion. Output is 48 kHz mono signed PCM16. Stereo is
// averaged, not represented as independent microphones. No resampler exists.
void validate_pcm(PcmFormat format);
std::vector<std::int16_t> mono_pcm16(std::span<const std::uint8_t> bytes, std::size_t frames, PcmFormat format,
                                     bool silence = false);
}
