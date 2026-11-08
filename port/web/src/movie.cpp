// The movie player on the web (video/player.hpp): the browser build has no FFmpeg, so no movie opens and the
// game passes over each one as it does over a movie file that is missing (video/movie.cpp).

#include "video/player.hpp"

namespace video {

bool DecodeRejections::Skip() {
    total++;
    return ++run <= kMaxInARow;
}

void DecodeRejections::Decoded() { run = 0; }

int64_t DecodeRejections::Total() const { return total; }

PictureFit FitPicture(int retail_width, int retail_height, int width, int height) {
    if (retail_width <= 0 || retail_height <= 0) {
        return {.width = width, .height = height, .scale = 1};
    }
    int scale = 4;
    while (scale > 1 && (width < retail_width * scale || height < retail_height * scale)) {
        scale /= 2;
    }
    return {.width = retail_width * scale, .height = retail_height * scale, .scale = scale};
}

void MoviePlayer::FrameDeleter::operator()(AVFrame *) const {}

struct MoviePlayer::Impl {};

MoviePlayer::MoviePlayer() = default;
MoviePlayer::~MoviePlayer() = default;

bool MoviePlayer::Open(const std::filesystem::path &, bool) {
    Close();
    return false;
}

void MoviePlayer::Close() {
    impl.reset();
    queue.clear();
    rgba.clear();
    audio_track.reset();
    input_exhausted = false;
    first_pts.reset();
    shown_pts = 0;
    elapsed_last = 0;
    source_width = 0;
    source_height = 0;
    picture_fit = {};
    pictures_shown = 0;
}

bool MoviePlayer::IsOpen() const { return false; }

bool MoviePlayer::Advance(int64_t) { return false; }

bool MoviePlayer::HasPicture() const { return false; }

int MoviePlayer::Width() const { return picture_fit.width; }

int MoviePlayer::Height() const { return picture_fit.height; }

const std::vector<uint8_t> &MoviePlayer::Rgba() const { return rgba; }

bool MoviePlayer::Ended() const { return true; }

std::shared_ptr<const audio::PcmTrack> MoviePlayer::Audio() const { return audio_track; }

int64_t MoviePlayer::PicturesShown() const { return pictures_shown; }

int64_t MoviePlayer::Length() const { return 0; }

int MoviePlayer::SourceWidth() const { return source_width; }

int MoviePlayer::SourceHeight() const { return source_height; }

int MoviePlayer::Scale() const { return picture_fit.scale; }

void MoviePlayer::SetRetailSize(int width, int height) {
    retail_width = width;
    retail_height = height;
}

void MoviePlayer::Pump() {}

} // namespace video
