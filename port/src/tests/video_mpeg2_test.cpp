#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

#include "platform/paths.hpp"
#include "video/mpeg2_decoder.hpp"
#include "video/pss.hpp"

namespace fs = std::filesystem;
using video::Mpeg2Decoder;
using video::Picture;
using video::PssChunk;
using video::PssDemuxer;
using video::PssStream;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr double kL5LogoFirstLuma = 16.0;
constexpr int64_t kL5LogoFirstPts = 4758;
constexpr size_t kPush = 64 * 1024;

// Writes fields most significant bit first, as the MPEG headers lay them out.
class BitWriter {
public:
    void Put(uint64_t value, int bits) {
        for (int i = bits - 1; i >= 0; i--, bit_++) {
            if (bit_ % 8 == 0) {
                out_.push_back(0);
            }
            if ((value >> i) & 1) {
                out_.back() |= static_cast<uint8_t>(0x80 >> (bit_ % 8));
            }
        }
    }

    void Start(uint8_t code) {
        out_.insert(out_.end(), {0x00, 0x00, 0x01, code});
        bit_ = 0;
    }

    const Bytes &Data() const { return out_; }

private:
    Bytes out_;
    int bit_ = 0;
};

Bytes Mpeg1Stream() {
    BitWriter es;
    es.Start(0xB3);
    es.Put(512, 12);
    es.Put(416, 12);
    es.Put(1, 4);
    es.Put(3, 4);
    es.Put(0x3FFFF, 18);
    es.Put(1, 1);
    es.Put(0x1E0, 10);
    es.Put(0, 1);
    es.Put(0, 1);
    es.Put(0, 1);

    es.Start(0xB8);
    es.Put(0, 25);
    es.Put(0, 2);

    es.Start(0x00);
    es.Put(0, 10);
    es.Put(1, 3);
    es.Put(0xFFFF, 16);
    return es.Data();
}

Bytes ReadFile(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ctest runs from the build directory, so search upward from the test executable for data/movie/.
std::optional<fs::path> MovieFile(const char *name) {
    fs::path dir = PathsExecutable().parent_path();
    for (int up = 0; up < 6 && !dir.empty(); up++, dir = dir.parent_path()) {
        std::error_code error;
        fs::path path = dir / "data" / "movie" / name;
        if (fs::is_regular_file(path, error)) {
            return path;
        }
    }
    return std::nullopt;
}

} // namespace

TEST(VideoMpeg2, RejectsMpeg1Stream) {
    const Bytes stream = Mpeg1Stream();
    Mpeg2Decoder decoder;

    EXPECT_FALSE(decoder.Push(stream, std::nullopt));
    EXPECT_TRUE(decoder.Failed());
    EXPECT_FALSE(decoder.Receive().has_value());
}

TEST(VideoMpeg2, RejectsStreamWithoutSequenceHeader) {
    const Bytes zeros(300 * 1024, 0x00);
    Mpeg2Decoder decoder;

    decoder.Push(zeros, 0);
    EXPECT_TRUE(decoder.Failed());
    EXPECT_FALSE(decoder.Receive().has_value());
}

TEST(VideoMpeg2, EmptyInputYieldsNothing) {
    Mpeg2Decoder decoder;
    decoder.Finish();

    EXPECT_FALSE(decoder.Receive().has_value());
    EXPECT_FALSE(decoder.Failed());
}

TEST(VideoMpeg2, L5LogoFirstPictureIsAnIPictureOfTheSequenceSize) {
    const std::optional<fs::path> path = MovieFile("l5logo.pss");
    if (!path) {
        GTEST_SKIP() << "no extracted data/movie/l5logo.pss within 6 levels above the test executable";
    }

    const Bytes file = ReadFile(*path);
    PssDemuxer demux;
    demux.Push(file);
    demux.Finish();

    Mpeg2Decoder decoder;
    std::optional<Picture> first;
    while (!first) {
        const std::optional<PssChunk> chunk = demux.Next();
        if (!chunk) {
            break;
        }
        if (chunk->stream != PssStream::Video) {
            continue;
        }
        decoder.Push(chunk->payload, chunk->pts);
        first = decoder.Receive();
    }
    if (!first) {
        decoder.Finish();
        first = decoder.Receive();
    }

    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(decoder.Failed());
    EXPECT_EQ(first->width, 512);
    EXPECT_EQ(first->height, 416);
    EXPECT_TRUE(first->key);
    EXPECT_EQ(first->pts, kL5LogoFirstPts);
    ASSERT_EQ(first->y.size(), static_cast<size_t>(512 * 416));
    ASSERT_EQ(first->cb.size(), static_cast<size_t>(256 * 208));

    double sum = 0.0;
    for (const uint8_t v : first->y) {
        sum += v;
    }
    const double average = sum / static_cast<double>(first->y.size());
    std::printf("L5LOGO first picture average luma %.4f\n", average);
    std::fflush(stdout);
    EXPECT_NEAR(average, kL5LogoFirstLuma, 0.01);
}

TEST(VideoMpeg2, L5LogoDecodesEveryPicture) {
    const std::optional<fs::path> path = MovieFile("l5logo.pss");
    if (!path) {
        GTEST_SKIP() << "no extracted data/movie/l5logo.pss within 6 levels above the test executable";
    }

    const Bytes file = ReadFile(*path);
    PssDemuxer demux;
    Mpeg2Decoder decoder;
    size_t pictures = 0;
    std::optional<int64_t> last_pts;
    bool strictly_increasing = true;

    const auto drain = [&] {
        while (std::optional<Picture> pic = decoder.Receive()) {
            pictures++;
            if (last_pts && pic->pts <= *last_pts) {
                strictly_increasing = false;
            }
            last_pts = pic->pts;
        }
    };
    const auto pump = [&] {
        while (std::optional<PssChunk> chunk = demux.Next()) {
            if (chunk->stream == PssStream::Video) {
                decoder.Push(chunk->payload, chunk->pts);
                drain();
            }
        }
    };

    for (size_t at = 0; at < file.size(); at += kPush) {
        demux.Push(std::span<const uint8_t>(file).subspan(at, std::min(kPush, file.size() - at)));
        pump();
    }
    demux.Finish();
    pump();
    decoder.Finish();
    drain();

    std::printf("L5LOGO decoded pictures=%zu\n", pictures);
    std::fflush(stdout);
    EXPECT_FALSE(decoder.Failed());
    EXPECT_EQ(pictures, 150u);
    EXPECT_TRUE(strictly_increasing);
}
