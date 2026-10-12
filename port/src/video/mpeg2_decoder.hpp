#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace video {

struct Picture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> y;   // width*height, tightly packed
    std::vector<uint8_t> cb;  // chroma-width*chroma-height, tightly packed
    std::vector<uint8_t> cr;
    int64_t pts = 0;          // 90 kHz
    bool key = false;         // I-picture
};

class Mpeg2Decoder {
public:
    Mpeg2Decoder();
    ~Mpeg2Decoder();
    Mpeg2Decoder(const Mpeg2Decoder &) = delete;
    Mpeg2Decoder &operator=(const Mpeg2Decoder &) = delete;

    bool Push(std::span<const uint8_t> elementary_stream, std::optional<int64_t> pts);
    void Finish();
    std::optional<Picture> Receive();
    bool Failed() const;
    int64_t FrameDuration() const; // 90 kHz ticks per picture from the sequence's frame rate, 3600 by default

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace video
