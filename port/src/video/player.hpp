#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

#include "audio/pcm.hpp"
#include "video/mpeg2_decoder.hpp"
#include "video/pss.hpp"

namespace video {

class MoviePlayer {
public:
    MoviePlayer();
    ~MoviePlayer();
    MoviePlayer(const MoviePlayer &) = delete;
    MoviePlayer &operator=(const MoviePlayer &) = delete;

    bool Open(const std::filesystem::path &path, bool with_audio);
    void Close();
    bool IsOpen() const;
    bool Advance(int64_t elapsed);   // elapsed movie time in 90 kHz units since the first picture
    bool HasPicture() const;
    int Width() const;
    int Height() const;
    const std::vector<uint8_t> &Rgba() const;  // Width()*Height()*4, the shown picture
    bool Ended() const;
    std::shared_ptr<const audio::PcmTrack> Audio() const;
    int64_t PicturesShown() const;   // count of distinct pictures that became the shown picture

private:
    void Pump();
    void ReadBlock();
    void EndInput();
    void CollectPictures();

    std::ifstream video_in_;
    std::vector<uint8_t> block_;
    PssDemuxer video_demux_;
    bool video_eof_ = false;
    std::unique_ptr<Mpeg2Decoder> decoder_;
    std::deque<Picture> queue_;
    bool input_exhausted_ = false;
    std::optional<int64_t> first_pts_;
    int64_t shown_pts_ = 0;
    int64_t elapsed_last_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> rgba_;
    int64_t pictures_shown_ = 0;
    std::shared_ptr<const audio::PcmTrack> audio_;
};

}  // namespace video
