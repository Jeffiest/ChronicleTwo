#include "video/pss.hpp"

#include <cstddef>
#include <limits>

namespace video {
namespace {

constexpr uint8_t kSequenceHeader = 0xB3;
constexpr uint8_t kUserData = 0xB2;
constexpr uint8_t kExtensionStart = 0xB5;
constexpr uint8_t kSequenceExtensionId = 1;
constexpr uint8_t kPictureCodingExtensionId = 8;
constexpr uint8_t kEndCode = 0xB9;
constexpr uint8_t kPackStart = 0xBA;
constexpr uint8_t kSystemHeader = 0xBB;
constexpr uint8_t kFirstLengthCoded = 0xBC;
constexpr uint8_t kPrivateStream1 = 0xBD;
constexpr uint8_t kVideoStream0 = 0xE0;
constexpr uint8_t kPcmChannel0 = 0xA0;
constexpr uint8_t kStuffing = 0xFF;
constexpr size_t kPcmSubHeaderSize = 4;
constexpr size_t kNotFound = std::numeric_limits<size_t>::max();

enum class PesResult { Chunk, Skip, Bad };

bool IsStartCodeAt(const uint8_t *p) { return p[0] == 0 && p[1] == 0 && p[2] == 1; }

size_t NextStartCode(std::span<const uint8_t> bytes, size_t from) {
    for (size_t i = from; i + 4 <= bytes.size(); i++) {
        if (IsStartCodeAt(&bytes[i])) {
            return i;
        }
    }
    return kNotFound;
}

size_t FindCode(std::span<const uint8_t> bytes, size_t from, uint8_t id) {
    for (size_t at = NextStartCode(bytes, from); at != kNotFound; at = NextStartCode(bytes, at + 4)) {
        if (bytes[at + 3] == id) {
            return at;
        }
    }
    return kNotFound;
}

size_t FindPackStart(std::span<const uint8_t> bytes, size_t from) {
    for (size_t at = NextStartCode(bytes, from); at != kNotFound; at = NextStartCode(bytes, at + 1)) {
        if (bytes[at + 3] == kPackStart) {
            return at;
        }
    }
    return kNotFound;
}

std::optional<int64_t> DecodeTimestamp(const uint8_t *b, uint8_t prefix) {
    if ((b[0] >> 4) != prefix || !(b[0] & 1) || !(b[2] & 1) || !(b[4] & 1)) {
        return std::nullopt;
    }
    return (static_cast<int64_t>((b[0] >> 1) & 7) << 30) | (static_cast<int64_t>(b[1]) << 22) |
           (static_cast<int64_t>((b[2] >> 1) & 0x7F) << 15) | (static_cast<int64_t>(b[3]) << 7) |
           static_cast<int64_t>((b[4] >> 1) & 0x7F);
}

// packet holds one whole PES packet, start code included.
PesResult ParsePes(std::span<const uint8_t> packet, PssChunk &out) {
    if (packet.size() < 9 || (packet[6] & 0xC0) != 0x80) {
        return PesResult::Bad;
    }

    const unsigned pts_dts_flags = packet[7] >> 6;
    const size_t header_data = packet[8];
    const size_t payload_at = 9 + header_data;
    if (pts_dts_flags == 1 || payload_at > packet.size()) {
        return PesResult::Bad;
    }

    const size_t needed = pts_dts_flags == 3 ? 10 : pts_dts_flags == 2 ? 5 : 0;
    if (header_data < needed) {
        return PesResult::Bad;
    }

    out.pts.reset();
    out.dts.reset();
    if (pts_dts_flags >= 2) {
        out.pts = DecodeTimestamp(&packet[9], pts_dts_flags == 3 ? 3 : 2);
        if (!out.pts) {
            return PesResult::Bad;
        }
    }
    if (pts_dts_flags == 3) {
        out.dts = DecodeTimestamp(&packet[14], 1);
        if (!out.dts) {
            return PesResult::Bad;
        }
    }

    const std::span<const uint8_t> payload = packet.subspan(payload_at);
    if (packet[3] == kVideoStream0) {
        out.stream = PssStream::Video;
        out.substream = 0;
        out.payload.assign(payload.begin(), payload.end());
        return PesResult::Chunk;
    }

    // The movies write 0xFF stuffing ahead of the substream id, inside the 4-byte sub-header that
    // retail strips from the payload start (FF A0 00 00, then the SShd header), so the id is found
    // past the stuffing and the strip stays fixed at 4 bytes.
    if (payload.size() < kPcmSubHeaderSize) {
        return PesResult::Skip;
    }
    size_t id = 0;
    while (id < kPcmSubHeaderSize && payload[id] == kStuffing) {
        id++;
    }
    if (id == kPcmSubHeaderSize || payload[id] != kPcmChannel0) {
        return PesResult::Skip;
    }

    out.stream = PssStream::Pcm;
    out.substream = kPcmChannel0;
    out.payload.assign(payload.begin() + kPcmSubHeaderSize, payload.end());
    return PesResult::Chunk;
}

// Reads MSB-first bit fields. A read past the end yields zero and sets the overrun flag, so a
// parser checks Overrun() once after its last field.
class BitCursor {
public:
    explicit BitCursor(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    uint32_t Take(unsigned count) {
        if (bit_ + count > bytes_.size() * 8) {
            overrun_ = true;
            bit_ += count;
            return 0;
        }
        uint32_t value = 0;
        for (unsigned i = 0; i < count; i++, bit_++) {
            value = (value << 1) | ((bytes_[bit_ >> 3] >> (7 - (bit_ & 7))) & 1u);
        }
        return value;
    }

    bool Overrun() const { return overrun_; }

private:
    std::span<const uint8_t> bytes_;
    size_t bit_ = 0;
    bool overrun_ = false;
};

} // namespace

void PssDemuxer::Push(std::span<const uint8_t> bytes) {
    if (pos_ > buffer_.size() / 2) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(pos_));
        pos_ = 0;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

void PssDemuxer::Finish() { finished_ = true; }

std::optional<PssChunk> PssDemuxer::Next() {
    const auto advance = [this](size_t count) {
        pos_ += count;
        consumed_ += count;
    };
    // Without more input a packet can never complete, so once finished the tail is dropped.
    const auto stall = [this, &advance]() -> std::optional<PssChunk> {
        if (finished_) {
            advance(buffer_.size() - pos_);
        }
        return std::nullopt;
    };

    while (error_ == PssError::None && !ended_) {
        const size_t avail = buffer_.size() - pos_;
        if (avail < 4) {
            return stall();
        }

        const std::span<const uint8_t> rest(buffer_.data() + pos_, avail);
        const uint8_t *p = rest.data();
        const uint8_t code = p[3];
        const bool at_packet = IsStartCodeAt(p) && (code == kPackStart || code == kEndCode ||
                                                    code == kSystemHeader || code >= kFirstLengthCoded);
        if (!at_packet) {
            // A start code split across pushes needs its last three bytes kept for the next search.
            const size_t next = FindPackStart(rest, 1);
            if (next != kNotFound) {
                advance(next);
                continue;
            }
            advance(finished_ ? avail : avail - 3);
            return std::nullopt;
        }

        if (code == kEndCode) {
            advance(4);
            ended_ = true;
            return std::nullopt;
        }

        if (code == kPackStart) {
            if (avail < 5) {
                return stall();
            }
            size_t size = 0;
            if ((p[4] & 0xC0) == 0x40) {
                if (avail < 14) {
                    return stall();
                }
                size = 14 + (p[13] & 7);
            } else if ((p[4] & 0xF0) == 0x20) {
                size = 12;
            } else {
                error_ = PssError::BadPack;
                return std::nullopt;
            }
            if (avail < size) {
                return stall();
            }
            advance(size);
            continue;
        }

        if (avail < 6) {
            return stall();
        }
        const size_t length = (static_cast<size_t>(p[4]) << 8) | p[5];

        if (code == kPrivateStream1 || code == kVideoStream0) {
            if (length == 0) {
                error_ = PssError::BadPes;
                return std::nullopt;
            }
            const size_t size = 6 + length;
            if (avail < size) {
                return stall();
            }
            PssChunk chunk;
            const PesResult result = ParsePes(rest.first(size), chunk);
            if (result == PesResult::Bad) {
                error_ = PssError::BadPes;
                return std::nullopt;
            }
            advance(size);
            if (result == PesResult::Chunk) {
                return chunk;
            }
            continue;
        }

        const size_t size = 6 + length;
        if (avail < size) {
            return stall();
        }
        advance(size);
    }

    return std::nullopt;
}

bool PssDemuxer::Done() const { return ended_ || (finished_ && pos_ == buffer_.size()); }

PssError PssDemuxer::Error() const { return error_; }

uint64_t PssDemuxer::Consumed() const { return consumed_; }

std::optional<Mpeg2SequenceInfo> ProbeSequence(std::span<const uint8_t> elementary_stream) {
    const size_t header = FindCode(elementary_stream, 0, kSequenceHeader);
    if (header == kNotFound) {
        return std::nullopt;
    }

    Mpeg2SequenceInfo info;
    BitCursor seq(elementary_stream.subspan(header + 4));
    const uint32_t width = seq.Take(12);
    const uint32_t height = seq.Take(12);
    info.aspect_ratio_code = static_cast<uint8_t>(seq.Take(4));
    info.frame_rate_code = static_cast<uint8_t>(seq.Take(4));
    info.bit_rate = seq.Take(18);
    seq.Take(1);
    if (seq.Overrun()) {
        return std::nullopt;
    }
    info.width = static_cast<uint16_t>(width);
    info.height = static_cast<uint16_t>(height);

    size_t ext = NextStartCode(elementary_stream, header + 4);
    while (ext != kNotFound && elementary_stream[ext + 3] == kUserData) {
        ext = NextStartCode(elementary_stream, ext + 4);
    }
    if (ext != kNotFound && elementary_stream[ext + 3] == kExtensionStart) {
        BitCursor seq_ext(elementary_stream.subspan(ext + 4));
        if (seq_ext.Take(4) == kSequenceExtensionId) {
            info.mpeg2 = true;
            info.profile_and_level = static_cast<uint8_t>(seq_ext.Take(8));
            info.progressive_sequence = seq_ext.Take(1) != 0;
            info.chroma_format = static_cast<uint8_t>(seq_ext.Take(2));
            const uint32_t width_ext = seq_ext.Take(2);
            const uint32_t height_ext = seq_ext.Take(2);
            const uint32_t bit_rate_ext = seq_ext.Take(12);
            seq_ext.Take(1);
            seq_ext.Take(8);
            info.low_delay = seq_ext.Take(1) != 0;
            if (seq_ext.Overrun()) {
                return std::nullopt;
            }
            info.width = static_cast<uint16_t>(width | (width_ext << 12));
            info.height = static_cast<uint16_t>(height | (height_ext << 12));
            info.bit_rate = info.bit_rate | (bit_rate_ext << 18);
        }
    }

    for (size_t at = NextStartCode(elementary_stream, header + 4); at != kNotFound;
         at = NextStartCode(elementary_stream, at + 4)) {
        if (elementary_stream[at + 3] != kExtensionStart || at + 4 >= elementary_stream.size() ||
            (elementary_stream[at + 4] >> 4) != kPictureCodingExtensionId) {
            continue;
        }
        BitCursor picture(elementary_stream.subspan(at + 4));
        picture.Take(4);
        picture.Take(16);
        picture.Take(2);
        const uint32_t structure = picture.Take(2);
        picture.Take(8);
        const bool progressive_frame = picture.Take(1) != 0;
        if (!picture.Overrun()) {
            info.first_picture_structure = static_cast<uint8_t>(structure);
            info.first_picture_progressive_frame = progressive_frame;
        }
        break;
    }

    return info;
}

} // namespace video
