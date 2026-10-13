#include "video/mpeg2_decoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "video/pss.hpp"

namespace video {
namespace {

// Probing stops here: a sequence header plus its extension always fits well inside this, and an
// unbounded wait would hold the whole movie in memory for a stream that is not MPEG-2.
constexpr size_t kProbeCap = 256 * 1024;
constexpr size_t kNpos = std::numeric_limits<size_t>::max();
constexpr uint8_t kSequenceHeader = 0xB3;
constexpr uint8_t kUserData = 0xB2;
constexpr uint8_t kExtensionStart = 0xB5;
constexpr int64_t kClockRate = 90000;
constexpr int64_t kDefaultFrameDuration = 3600;

struct FrameRate {
    int64_t num;
    int64_t den;
};

std::optional<FrameRate> RateFor(uint8_t code) {
    switch (code) {
    case 1: return FrameRate{24000, 1001};
    case 2: return FrameRate{24, 1};
    case 3: return FrameRate{25, 1};
    case 4: return FrameRate{30000, 1001};
    case 5: return FrameRate{30, 1};
    case 6: return FrameRate{50, 1};
    case 7: return FrameRate{60000, 1001};
    case 8: return FrameRate{60, 1};
    default: return std::nullopt;
    }
}

int64_t DurationFor(uint8_t code) {
    const std::optional<FrameRate> rate = RateFor(code);
    if (!rate) {
        return kDefaultFrameDuration;
    }
    return (2 * kClockRate * rate->den + rate->num) / (2 * rate->num);
}

bool StartCodeAt(std::span<const uint8_t> bytes, size_t at) {
    return at + 4 <= bytes.size() && bytes[at] == 0 && bytes[at + 1] == 0 && bytes[at + 2] == 1;
}

size_t NextStartCode(std::span<const uint8_t> bytes, size_t from) {
    for (size_t at = from; at + 4 <= bytes.size(); at++) {
        if (StartCodeAt(bytes, at)) {
            return at;
        }
    }
    return kNpos;
}

size_t FindCode(std::span<const uint8_t> bytes, uint8_t code) {
    for (size_t at = NextStartCode(bytes, 0); at != kNpos; at = NextStartCode(bytes, at + 1)) {
        if (bytes[at + 3] == code) {
            return at;
        }
    }
    return kNpos;
}

// A sequence extension, when present, is the first start code after the sequence header that is not
// user data. Any other code there means no extension will come, so the MPEG-1 verdict is final.
bool SequenceExtensionSettled(std::span<const uint8_t> bytes) {
    const size_t header = FindCode(bytes, kSequenceHeader);
    if (header == kNpos) {
        return false;
    }
    for (size_t at = NextStartCode(bytes, header + 4); at != kNpos; at = NextStartCode(bytes, at + 4)) {
        const uint8_t code = bytes[at + 3];
        if (code != kUserData && code != kExtensionStart) {
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> CopyPlane(const uint8_t *src, int stride, int width, int height) {
    std::vector<uint8_t> dst(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (int row = 0; row < height; row++) {
        std::memcpy(dst.data() + static_cast<size_t>(row) * static_cast<size_t>(width),
                    src + static_cast<std::ptrdiff_t>(row) * stride, static_cast<size_t>(width));
    }
    return dst;
}

} // namespace

struct Mpeg2Decoder::Impl {
    enum class Verdict { Undecided, Reject, Mpeg2 };

    struct PendingPush {
        size_t offset;
        std::optional<int64_t> pts;
    };

    ~Impl() {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&context);
        av_parser_close(parser);
    }

    bool Fail() {
        failed = true;
        return false;
    }

    bool Open() {
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
        parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
        context = codec ? avcodec_alloc_context3(codec) : nullptr;
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!codec || !parser || !context || !packet || !frame) {
            return false;
        }
        // Single-threaded by requirement: the game runs no worker threads.
        context->thread_count = 1;
        context->thread_type = 0;
        return avcodec_open2(context, codec, nullptr) == 0;
    }

    Verdict Judge(std::span<const uint8_t> bytes, bool exhausted) {
        const std::optional<Mpeg2SequenceInfo> probed = ProbeSequence(bytes);
        if (probed && probed->mpeg2) {
            frame_duration = DurationFor(probed->frame_rate_code);
            return Verdict::Mpeg2;
        }
        if (exhausted || (probed && SequenceExtensionSettled(bytes))) {
            return Verdict::Reject;
        }
        return Verdict::Undecided;
    }

    // Replays the probe window push by push so each PES timestamp lands on the picture that starts in it.
    bool Start(std::span<const uint8_t> rest) {
        decoding = true;
        for (size_t i = 0; i < probe_pushes.size(); i++) {
            const size_t end = i + 1 < probe_pushes.size() ? probe_pushes[i + 1].offset : probe.size();
            const std::span<const uint8_t> segment = std::span<const uint8_t>(probe).subspan(
                probe_pushes[i].offset, end - probe_pushes[i].offset);
            if (!Parse(segment, probe_pushes[i].pts)) {
                return Fail();
            }
        }
        probe.clear();
        probe.shrink_to_fit();
        probe_pushes.clear();
        return Parse(rest, std::nullopt) || Fail();
    }

    bool Push(std::span<const uint8_t> bytes, std::optional<int64_t> pts) {
        if (failed || bytes.empty()) {
            return !failed;
        }
        if (decoding) {
            return Parse(bytes, pts) || Fail();
        }

        if (!context && !Open()) {
            return Fail();
        }
        const size_t take = std::min(bytes.size(), kProbeCap - probe.size());
        probe_pushes.push_back({probe.size(), pts});
        probe.insert(probe.end(), bytes.data(), bytes.data() + take);

        switch (Judge(probe, probe.size() >= kProbeCap)) {
        case Verdict::Reject:
            return Fail();
        case Verdict::Undecided:
            return true;
        case Verdict::Mpeg2:
            return Start(bytes.subspan(take));
        }
        return Fail();
    }

    void Finish() {
        if (failed) {
            return;
        }
        if (!decoding) {
            if (probe.empty()) {
                return;
            }
            if (Judge(probe, true) != Verdict::Mpeg2 || !Start({})) {
                Fail();
                return;
            }
        }
        if (!FlushParser()) {
            Fail();
            return;
        }
        const int ret = avcodec_send_packet(context, nullptr);
        if ((ret < 0 && ret != AVERROR_EOF) || !Drain()) {
            Fail();
        }
    }

    // Returns false on a stall or a hard error. Only the first call of a Push carries its pts.
    bool Parse(std::span<const uint8_t> bytes, std::optional<int64_t> pts) {
        const uint8_t *data = bytes.data();
        size_t left = bytes.size();
        int64_t in_pts = pts.value_or(AV_NOPTS_VALUE);
        while (left > 0) {
            uint8_t *out = nullptr;
            int out_size = 0;
            const int used = av_parser_parse2(parser, context, &out, &out_size, data, static_cast<int>(left),
                                              in_pts, AV_NOPTS_VALUE, 0);
            in_pts = AV_NOPTS_VALUE;
            if (used < 0 || (used == 0 && out_size == 0)) {
                return false;
            }
            data += used;
            left -= static_cast<size_t>(used);
            if (out_size > 0 && !Submit(out, out_size, parser->pts)) {
                return false;
            }
        }
        return true;
    }

    bool FlushParser() {
        for (;;) {
            uint8_t *out = nullptr;
            int out_size = 0;
            const int used = av_parser_parse2(parser, context, &out, &out_size, nullptr, 0, AV_NOPTS_VALUE,
                                              AV_NOPTS_VALUE, 0);
            if (used < 0) {
                return false;
            }
            if (out_size == 0) {
                return true;
            }
            if (!Submit(out, out_size, parser->pts)) {
                return false;
            }
        }
    }

    bool Submit(uint8_t *data, int size, int64_t pts) {
        packet->data = data;
        packet->size = size;
        packet->pts = pts;
        int ret = avcodec_send_packet(context, packet);
        if (ret == AVERROR(EAGAIN)) {
            if (!Drain()) {
                av_packet_unref(packet);
                return false;
            }
            ret = avcodec_send_packet(context, packet);
        }
        av_packet_unref(packet);
        if (ret < 0 && ret != AVERROR_INVALIDDATA) {
            return false;
        }
        return Drain();
    }

    bool Drain() {
        for (;;) {
            const int ret = avcodec_receive_frame(context, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret == AVERROR_INVALIDDATA) {
                return true;
            }
            if (ret < 0) {
                return false;
            }
            const bool emitted = Emit(*frame);
            av_frame_unref(frame);
            if (!emitted) {
                return false;
            }
        }
    }

    int64_t PictureTimestamp(const AVFrame &f) const {
        int64_t pts = 0;
        if (f.pts != AV_NOPTS_VALUE) {
            pts = f.pts;
        } else if (f.best_effort_timestamp != AV_NOPTS_VALUE) {
            pts = f.best_effort_timestamp;
        } else if (last_pts) {
            pts = *last_pts + frame_duration;
        }
        if (last_pts && pts <= *last_pts) {
            pts = *last_pts + frame_duration;
        }
        return pts;
    }

    bool Emit(const AVFrame &f) {
        if (f.format != AV_PIX_FMT_YUV420P || !f.data[0] || !f.data[1] || !f.data[2]) {
            return false;
        }
        // Chroma rounds up for odd dimensions, matching what the decoder writes into the frame.
        const int chroma_width = (f.width + 1) / 2;
        const int chroma_height = (f.height + 1) / 2;

        Picture pic;
        pic.width = f.width;
        pic.height = f.height;
        pic.y = CopyPlane(f.data[0], f.linesize[0], f.width, f.height);
        pic.cb = CopyPlane(f.data[1], f.linesize[1], chroma_width, chroma_height);
        pic.cr = CopyPlane(f.data[2], f.linesize[2], chroma_width, chroma_height);
        pic.key = f.pict_type == AV_PICTURE_TYPE_I;
        pic.pts = PictureTimestamp(f);

        last_pts = pic.pts;
        output.push_back(std::move(pic));
        return true;
    }

    bool failed = false;
    bool decoding = false;
    int64_t frame_duration = kDefaultFrameDuration;
    std::vector<uint8_t> probe;
    std::vector<PendingPush> probe_pushes;
    AVCodecParserContext *parser = nullptr;
    AVCodecContext *context = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    std::deque<Picture> output;
    std::optional<int64_t> last_pts;
};

Mpeg2Decoder::Mpeg2Decoder() : impl_(std::make_unique<Impl>()) {}

Mpeg2Decoder::~Mpeg2Decoder() = default;

bool Mpeg2Decoder::Push(std::span<const uint8_t> elementary_stream, std::optional<int64_t> pts) {
    return impl_->Push(elementary_stream, pts);
}

void Mpeg2Decoder::Finish() { impl_->Finish(); }

std::optional<Picture> Mpeg2Decoder::Receive() {
    if (impl_->output.empty()) {
        return std::nullopt;
    }
    Picture pic = std::move(impl_->output.front());
    impl_->output.pop_front();
    return pic;
}

bool Mpeg2Decoder::Failed() const { return impl_->failed; }

int64_t Mpeg2Decoder::FrameDuration() const { return impl_->frame_duration; }

} // namespace video
