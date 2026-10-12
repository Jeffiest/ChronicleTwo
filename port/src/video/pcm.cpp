#include "video/pcm.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace video {
namespace {

constexpr size_t kTagBytes = 4;
constexpr size_t kPrefixBytes = kTagBytes + 4;
constexpr size_t kMinHeaderSize = 24;
constexpr size_t kBodyPrefixBytes = kTagBytes + 4;
constexpr uint32_t kSigned16LePcm = 1;

uint32_t ReadU32(const uint8_t *p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

bool HasTag(const uint8_t *p, const char *tag) { return std::memcmp(p, tag, kTagBytes) == 0; }

void EmitGroup(const uint8_t *group, int channels, size_t stride, size_t frames, std::vector<int16_t> &out) {
    for (size_t frame = 0; frame < frames; frame++) {
        for (int c = 0; c < channels; c++) {
            const uint8_t *s = group + static_cast<size_t>(c) * stride + 2 * frame;
            out.push_back(static_cast<int16_t>(s[0] | (s[1] << 8)));
        }
    }
}

}  // namespace

void PssPcmDecoder::Push(std::span<const uint8_t> payload) {
    if (failed_ || finished_) {
        return;
    }
    if (!header_seen_) {
        pending_.insert(pending_.end(), payload.begin(), payload.end());
        if (!ParseHeader()) {
            return;
        }
        pending_.resize(std::min<size_t>(pending_.size(), format_.body_bytes));
        body_taken_ = pending_.size();
    } else {
        const size_t take = std::min(payload.size(), format_.body_bytes - body_taken_);
        pending_.insert(pending_.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(take));
        body_taken_ += take;
    }
    DecodeWholeGroups();
}

void PssPcmDecoder::Finish() {
    if (finished_) {
        return;
    }
    finished_ = true;
    if (header_seen_ && !pending_.empty()) {
        // A trailing partial group has no recorded layout; an even split keeps the channels aligned.
        const size_t per_channel = pending_.size() / static_cast<size_t>(format_.channels);
        EmitGroup(pending_.data(), format_.channels, per_channel, per_channel / 2, samples_);
    }
    pending_.clear();
}

bool PssPcmDecoder::Failed() const { return failed_; }

bool PssPcmDecoder::HeaderSeen() const { return header_seen_; }

const PssPcmFormat &PssPcmDecoder::Format() const { return format_; }

const std::vector<int16_t> &PssPcmDecoder::Samples() const { return samples_; }

bool PssPcmDecoder::ParseHeader() {
    const uint8_t *p = pending_.data();
    if (pending_.size() >= kTagBytes && !HasTag(p, "SShd")) {
        return Fail();
    }
    if (pending_.size() < kPrefixBytes) {
        return false;
    }
    const uint32_t header_size = ReadU32(p + kTagBytes);
    if (header_size < kMinHeaderSize) {
        return Fail();
    }
    const size_t body_tag = kPrefixBytes + header_size;
    if (pending_.size() < body_tag + kBodyPrefixBytes) {
        return false;
    }
    if (!HasTag(p + body_tag, "SSbd")) {
        return Fail();
    }

    PssPcmFormat format;
    format.type = ReadU32(p + 8);
    format.rate = static_cast<int>(ReadU32(p + 12));
    format.channels = static_cast<int>(ReadU32(p + 16));
    format.interleave = ReadU32(p + 20);
    format.body_bytes = ReadU32(p + body_tag + 4);
    if (format.type != kSigned16LePcm || format.rate <= 0 || (format.channels != 1 && format.channels != 2) ||
        format.interleave == 0 || format.interleave % 2 != 0) {
        return Fail();
    }

    format_ = format;
    header_seen_ = true;
    pending_.erase(pending_.begin(),
                   pending_.begin() + static_cast<std::ptrdiff_t>(body_tag + kBodyPrefixBytes));
    return true;
}

void PssPcmDecoder::DecodeWholeGroups() {
    const size_t group = static_cast<size_t>(format_.channels) * format_.interleave;
    size_t at = 0;
    while (pending_.size() - at >= group) {
        EmitGroup(pending_.data() + at, format_.channels, format_.interleave, format_.interleave / 2, samples_);
        at += group;
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(at));
}

bool PssPcmDecoder::Fail() {
    failed_ = true;
    pending_.clear();
    return false;
}

}  // namespace video
