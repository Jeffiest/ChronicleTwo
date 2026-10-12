#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace video {

enum class PssStream { Video, Private1 };

struct PssChunk {
    PssStream stream{};
    std::uint8_t stream_id = 0;
    std::int64_t pts = -1;
    std::int64_t dts = -1;
    std::vector<std::uint8_t> bytes; // PES elementary payload (private substream header retained).
};

struct PssInfo {
    int width = 0;
    int height = 0;
    int frame_rate_code = 0;
    bool has_sequence_extension = false;
    bool progressive_sequence = false;
};

// Parses complete program-stream packets from a bounded input span. A partial trailing packet is
// left unconsumed for the caller to prepend to its next read. Invalid packets return an error.
class PssDemuxer {
public:
    static constexpr std::size_t MaxPacketBytes = 1024 * 1024;
    bool Feed(const std::uint8_t *data, std::size_t size, std::size_t &consumed,
              std::vector<PssChunk> &chunks);
    const PssInfo &Info() const { return info_; }

private:
    PssInfo info_;
};

} // namespace video
