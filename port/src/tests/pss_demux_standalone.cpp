#include "../video/pss.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

static void be16(std::vector<std::uint8_t> &v, std::size_t n) { v.push_back(n >> 8); v.push_back(n); }
static void pes(std::vector<std::uint8_t> &v, std::uint8_t id, const std::vector<std::uint8_t> &es, std::int64_t pts) {
    v.insert(v.end(), {0,0,1,id}); be16(v, 8 + es.size());
    v.insert(v.end(), {0x80,0x80,5});
    // PTS prefix 0010 with valid marker bits; synthetic small value 90000.
    const std::uint64_t p = (std::uint64_t)pts;
    v.push_back((2 << 4) | (((p >> 30) & 7) << 1) | 1);
    v.push_back(p >> 22); v.push_back((((p >> 15) & 0x7f) << 1) | 1);
    v.push_back(p >> 7); v.push_back(((p & 0x7f) << 1) | 1);
    v.insert(v.end(), es.begin(), es.end());
}

int main() {
    std::vector<std::uint8_t> stream = {0,0,1,0xba,0x44,0,0,0,0,0,0,0,0,0}; // MPEG-2 pack
    // 720x480 sequence header, 29.97 fps and sequence extension with progressive_sequence=1.
    std::vector<std::uint8_t> v = {0,0,1,0xb3,0x2d,0x01,0xe0,0x04,
                                   0,0,1,0xb5,0x18,0x80,0,0};
    pes(stream, 0xe0, v, 90000);
    pes(stream, 0xbd, {0x80,1,2,3}, 180000);
    stream.insert(stream.end(), {0,0,1,0xb9});
    video::PssDemuxer d; std::vector<video::PssChunk> chunks; std::size_t used = 0;
    assert(d.Feed(stream.data(), stream.size(), used, chunks));
    assert(used == stream.size() && chunks.size() == 2);
    assert(chunks[0].stream == video::PssStream::Video && chunks[0].pts == 90000 && chunks[0].bytes == v);
    assert(chunks[1].stream == video::PssStream::Private1 && chunks[1].pts == 180000);
    assert(d.Info().width == 720 && d.Info().height == 480 && d.Info().frame_rate_code == 4);
    assert(d.Info().has_sequence_extension && d.Info().progressive_sequence);
    // Incomplete trailing PES remains available to the caller for the next read.
    auto partial = stream; partial.resize(partial.size() - 2);
    video::PssDemuxer d2; chunks.clear();
    assert(d2.Feed(partial.data(), partial.size(), used, chunks));
    assert(used < partial.size());
    // A declared packet beyond this span stays unconsumed for the next read.
    const std::uint8_t incomplete[] = {0,0,1,0xe0,0xff,0xff};
    chunks.clear();
    assert(d2.Feed(incomplete, sizeof incomplete, used, chunks));
    assert(used == 0 && chunks.empty());
    // A complete packet with an optional header beyond its payload fails closed.
    const std::uint8_t bad[] = {0,0,1,0xe0,0,3,0x80,0x80,5};
    assert(!d2.Feed(bad, sizeof bad, used, chunks));
    assert(!d2.Feed(nullptr, 1, used, chunks));
}
