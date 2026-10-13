#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace video {

enum class PssStream : uint8_t { Video, Pcm };

struct PssChunk {
    PssStream stream = PssStream::Video;
    uint8_t substream = 0;            // private_stream_1 substream id for Pcm, 0 for Video
    std::optional<int64_t> pts;       // 90 kHz, 33-bit
    std::optional<int64_t> dts;
    std::vector<uint8_t> payload;     // Video: elementary-stream bytes. Pcm: the bytes after the 4-byte sub-header.
};

enum class PssError : uint8_t { None, BadPack, BadPes };

// Incremental: the player pushes file bytes as it reads them and pulls PES payloads of video
// stream 0 and PCM channel 0; everything else (padding, system header, other streams) is skipped.
class PssDemuxer {
public:
    void Push(std::span<const uint8_t> bytes);
    void Finish();
    std::optional<PssChunk> Next();   // nullopt: needs more input, or Done()
    bool Done() const;                // Finish() called and every byte consumed, or the end code seen
    PssError Error() const;
    uint64_t Consumed() const;        // bytes of the stream parsed so far
private:
    std::vector<uint8_t> buffer_;
    size_t pos_ = 0;
    uint64_t consumed_ = 0;
    bool finished_ = false;
    bool ended_ = false;
    PssError error_ = PssError::None;
};

struct Mpeg2SequenceInfo {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t aspect_ratio_code = 0;
    uint8_t frame_rate_code = 0;
    uint32_t bit_rate = 0;            // units of 400 bit/s, 18 bits + extension bits
    bool mpeg2 = false;               // a sequence extension follows the sequence header
    uint8_t profile_and_level = 0;
    bool progressive_sequence = false;
    uint8_t chroma_format = 0;        // 1 = 4:2:0
    bool low_delay = false;
    std::optional<bool> first_picture_progressive_frame;  // from the first picture coding extension
    std::optional<uint8_t> first_picture_structure;       // 3 = frame
};

// Parses the first sequence header (00 00 01 B3), its sequence extension (00 00 01 B5, id 1) and the
// first picture coding extension (id 8) of an MPEG video elementary stream. Header parsing only.
std::optional<Mpeg2SequenceInfo> ProbeSequence(std::span<const uint8_t> elementary_stream);

}  // namespace video
