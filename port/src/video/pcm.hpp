#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace video {

struct PssPcmFormat {
    uint32_t type = 0;
    int rate = 0;
    int channels = 0;
    uint32_t interleave = 0;
    uint32_t body_bytes = 0;
};

// Consumes the PCM track of a PSS file, one payload at a time, and emits interleaved 16-bit samples.
class PssPcmDecoder {
public:
    void Push(std::span<const uint8_t> payload);
    void Finish();
    bool Failed() const;
    bool HeaderSeen() const;
    const PssPcmFormat &Format() const;
    const std::vector<int16_t> &Samples() const;

private:
    bool ParseHeader();
    void DecodeWholeGroups();
    bool Fail();

    std::vector<uint8_t> pending_;
    PssPcmFormat format_;
    size_t body_taken_ = 0;
    bool header_seen_ = false;
    bool failed_ = false;
    bool finished_ = false;
    std::vector<int16_t> samples_;
};

}  // namespace video
