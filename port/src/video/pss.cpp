#include "pss.hpp"

#include <algorithm>

namespace video {
namespace {
std::int64_t ReadPts(const std::uint8_t *p) {
    return (std::int64_t)((p[0] >> 1) & 7) << 30 | (std::int64_t)p[1] << 22 |
           (std::int64_t)((p[2] >> 1) & 0x7f) << 15 | (std::int64_t)p[3] << 7 | (p[4] >> 1);
}
bool Start(const std::uint8_t *p) { return p[0] == 0 && p[1] == 0 && p[2] == 1; }
}

bool PssDemuxer::Feed(const std::uint8_t *data, std::size_t size, std::size_t &consumed,
                      std::vector<PssChunk> &chunks) {
    consumed = 0;
    if (data == nullptr && size != 0) return false;
    std::size_t pos = 0;
    while (pos < size) {
        if (size - pos < 4) break;
        if (!Start(data + pos)) return false;
        const std::uint8_t code = data[pos + 3];
        if (code == 0xb9) { pos += 4; consumed = pos; continue; }
        if (code == 0xba) {
            if (size - pos < 5) break;
            std::size_t n;
            if ((data[pos + 4] & 0xc0) == 0x40) { // MPEG-2 pack header
                if (size - pos < 14) break;
                n = 14 + (data[pos + 13] & 7);
            } else if ((data[pos + 4] & 0xf0) == 0x20) n = 12; // MPEG-1 pack
            else return false;
            if (n > size - pos) break;
            pos += n; consumed = pos; continue;
        }
        // System header and other length-prefixed packets.
        if (size - pos < 6) break;
        const std::size_t packet_size = 6u + (std::size_t(data[pos + 4]) << 8 | data[pos + 5]);
        if (packet_size > MaxPacketBytes) return false;
        if (packet_size > size - pos) break;
        const std::uint8_t *payload = data + pos + 6;
        const std::size_t plen = packet_size - 6;
        if (code >= 0xe0 && code <= 0xef && plen >= 4) {
            int width = (payload[0] << 4) | (payload[1] >> 4);
            int height = ((payload[1] & 15) << 8) | payload[2];
            // Scan video ES for sequence headers and extensions, independently of PES boundaries.
            for (std::size_t i = 0; i + 8 <= plen; ++i) {
                if (!Start(payload + i)) continue;
                if (payload[i + 3] == 0xb3) {
                    width = (payload[i + 4] << 4) | (payload[i + 5] >> 4);
                    height = ((payload[i + 5] & 15) << 8) | payload[i + 6];
                    info_.frame_rate_code = payload[i + 7] & 15;
                    if (width && height) { info_.width = width; info_.height = height; }
                } else if (payload[i + 3] == 0xb5 && i + 5 <= plen && (payload[i + 4] >> 4) == 1) {
                    info_.has_sequence_extension = true;
                    info_.progressive_sequence = (payload[i + 5] & 0x80) != 0;
                }
            }
        }
        PssStream stream;
        bool selected = false;
        if (code >= 0xe0 && code <= 0xef) { stream = PssStream::Video; selected = true; }
        else if (code == 0xbd) { stream = PssStream::Private1; selected = true; }
        if (selected) {
            std::size_t es = 0;
            std::int64_t pts = -1, dts = -1;
            // MPEG-2 PES optional header.
            if (plen >= 3 && (payload[0] & 0xc0) == 0x80) {
                const std::size_t hlen = payload[2];
                const unsigned flags = (payload[1] >> 6) & 3;
                if (3 + hlen > plen || ((flags == 2 || flags == 3) && hlen < 5) || (flags == 3 && hlen < 10)) return false;
                if (flags >= 2) pts = ReadPts(payload + 3);
                if (flags == 3) dts = ReadPts(payload + 8);
                es = 3 + hlen;
            } else if (plen >= 1) { // MPEG-1 PES optional fields
                std::size_t i = 0;
                while (i < plen && payload[i] == 0xff) ++i;
                if (i >= plen) return false;
                if ((payload[i] & 0xc0) == 0x40) i += 2;
                if (i >= plen) return false;
                if ((payload[i] & 0xf0) == 0x20) { if (plen - i < 5) return false; pts = ReadPts(payload + i); i += 5; }
                else if ((payload[i] & 0xf0) == 0x30) { if (plen - i < 10) return false; pts = ReadPts(payload + i); dts = ReadPts(payload + i + 5); i += 10; }
                else if (payload[i] == 0x0f) ++i;
                else return false;
                es = i;
            }
            PssChunk chunk;
            chunk.stream = stream; chunk.stream_id = code; chunk.pts = pts; chunk.dts = dts;
            chunk.bytes.assign(payload + es, payload + plen);
            chunks.push_back(std::move(chunk));
        }
        pos += packet_size;
        consumed = pos;
    }
    return true;
}
} // namespace video
