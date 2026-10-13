#include "video/player.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

#include "video/pcm.hpp"
#include "video/yuv.hpp"

namespace video {
namespace {

namespace fs = std::filesystem;

constexpr size_t kVideoBlockBytes = 64 * 1024;
constexpr size_t kPrescanBlockBytes = 1024 * 1024;
constexpr int64_t kClockHz = 90000;

void Record(std::optional<int64_t> &slot, const PssChunk &chunk) {
    if (!slot && chunk.pts) {
        slot = chunk.pts;
    }
}

// The shared mixer plays only complete tracks, so the soundtrack is decoded whole before the first picture.
std::shared_ptr<const audio::PcmTrack> PrescanAudio(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return nullptr;
    }

    PssDemuxer demux;
    PssPcmDecoder pcm;
    std::optional<int64_t> first_video;
    std::optional<int64_t> first_pcm;
    std::vector<uint8_t> block(kPrescanBlockBytes);

    const auto drain = [&] {
        while (std::optional<PssChunk> chunk = demux.Next()) {
            if (chunk->stream == PssStream::Pcm) {
                pcm.Push(chunk->payload);
                Record(first_pcm, *chunk);
            } else {
                Record(first_video, *chunk);
            }
        }
    };

    for (;;) {
        in.read(reinterpret_cast<char *>(block.data()), static_cast<std::streamsize>(block.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) {
            break;
        }
        demux.Push(std::span<const uint8_t>(block.data(), static_cast<size_t>(got)));
        drain();
    }
    demux.Finish();
    drain();
    pcm.Finish();

    if (!pcm.HeaderSeen() || pcm.Failed() || pcm.Samples().empty()) {
        return nullptr;
    }
    const PssPcmFormat &format = pcm.Format();
    if (format.channels != 1 && format.channels != 2) {
        return nullptr;
    }

    auto track = std::make_shared<audio::PcmTrack>();
    track->rate = format.rate;
    track->loop = false;
    const std::vector<int16_t> &source = pcm.Samples();
    if (format.channels == 2) {
        track->samples = source;
    } else {
        track->samples.reserve(source.size() * 2);
        for (int16_t sample : source) {
            track->samples.push_back(sample);
            track->samples.push_back(sample);
        }
    }

    if (first_video && first_pcm) {
        const int64_t offset = (*first_pcm - *first_video) * format.rate / kClockHz;
        if (offset > 0) {
            track->samples.insert(track->samples.begin(), static_cast<size_t>(offset) * 2, int16_t{0});
        } else if (offset < 0) {
            const int64_t drop = std::min(-offset, track->Frames());
            track->samples.erase(track->samples.begin(),
                                 track->samples.begin() + static_cast<std::ptrdiff_t>(drop * 2));
        }
    }
    return track;
}

}  // namespace

MoviePlayer::MoviePlayer() = default;

MoviePlayer::~MoviePlayer() = default;

bool MoviePlayer::Open(const fs::path &path, bool with_audio) {
    Close();
    video_in_.open(path, std::ios::binary);
    if (!video_in_.is_open()) {
        return false;
    }
    block_.resize(kVideoBlockBytes);
    decoder_ = std::make_unique<Mpeg2Decoder>();
    if (with_audio) {
        audio_ = PrescanAudio(path);
    }

    Pump();
    if (video_demux_.Error() != PssError::None || decoder_->Failed() || queue_.empty() || !Advance(0)) {
        Close();
        return false;
    }
    return true;
}

void MoviePlayer::Close() {
    video_in_.close();
    block_.clear();
    video_demux_ = PssDemuxer{};
    video_eof_ = false;
    decoder_.reset();
    queue_.clear();
    input_exhausted_ = false;
    first_pts_.reset();
    shown_pts_ = 0;
    elapsed_last_ = 0;
    width_ = 0;
    height_ = 0;
    rgba_.clear();
    pictures_shown_ = 0;
    audio_.reset();
}

bool MoviePlayer::IsOpen() const { return decoder_ != nullptr; }

bool MoviePlayer::Advance(int64_t elapsed) {
    if (!IsOpen()) {
        return false;
    }
    elapsed_last_ = elapsed;

    // Pictures behind the clock are dropped unconverted: after a stall only the newest due picture is worth
    // the colour conversion.
    std::optional<Picture> candidate;
    for (;;) {
        if (queue_.empty()) {
            if (input_exhausted_) {
                break;
            }
            Pump();
            continue;
        }
        if (queue_.front().pts - *first_pts_ > elapsed) {
            break;
        }
        candidate = std::move(queue_.front());
        queue_.pop_front();
    }
    if (!candidate) {
        return false;
    }

    const Picture &picture = *candidate;
    width_ = picture.width;
    height_ = picture.height;
    rgba_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
    // Fields are woven as the IPU output them: every disc movie is progressive content, L5LOGO's progressive_frame = 0 included.
    Yuv420ToRgba(picture.y.data(), picture.width, picture.cb.data(), picture.cr.data(), (picture.width + 1) / 2,
                 picture.width, picture.height, rgba_.data());
    shown_pts_ = picture.pts;
    pictures_shown_++;
    return true;
}

bool MoviePlayer::HasPicture() const { return !rgba_.empty(); }

int MoviePlayer::Width() const { return width_; }

int MoviePlayer::Height() const { return height_; }

const std::vector<uint8_t> &MoviePlayer::Rgba() const { return rgba_; }

bool MoviePlayer::Ended() const {
    return input_exhausted_ && queue_.empty() && HasPicture() &&
           elapsed_last_ >= shown_pts_ - *first_pts_ + decoder_->FrameDuration();
}

std::shared_ptr<const audio::PcmTrack> MoviePlayer::Audio() const { return audio_; }

int64_t MoviePlayer::PicturesShown() const { return pictures_shown_; }

void MoviePlayer::Pump() {
    while (queue_.empty() && !input_exhausted_) {
        if (decoder_->Failed() || video_demux_.Error() != PssError::None || video_demux_.Done()) {
            EndInput();
            return;
        }
        if (std::optional<PssChunk> chunk = video_demux_.Next()) {
            if (chunk->stream == PssStream::Video) {
                decoder_->Push(chunk->payload, chunk->pts);
            }
            CollectPictures();
            continue;
        }
        if (video_eof_) {
            EndInput();
            return;
        }
        ReadBlock();
    }
}

void MoviePlayer::ReadBlock() {
    video_in_.read(reinterpret_cast<char *>(block_.data()), static_cast<std::streamsize>(block_.size()));
    const std::streamsize got = video_in_.gcount();
    if (got > 0) {
        video_demux_.Push(std::span<const uint8_t>(block_.data(), static_cast<size_t>(got)));
    } else {
        video_eof_ = true;
        video_demux_.Finish();
    }
}

// A decoder that failed mid-stream counts as exhausted: pictures already queued still play out.
void MoviePlayer::EndInput() {
    input_exhausted_ = true;
    if (!decoder_->Failed()) {
        decoder_->Finish();
    }
    CollectPictures();
}

void MoviePlayer::CollectPictures() {
    while (std::optional<Picture> picture = decoder_->Receive()) {
        if (!first_pts_) {
            first_pts_ = picture->pts;
        }
        queue_.push_back(std::move(*picture));
    }
}

}  // namespace video
