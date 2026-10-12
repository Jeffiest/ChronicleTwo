#include "color_capture.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>
#include "gfx/context.hpp"

namespace port_capture {
namespace {
// Completed-frame reads must not append their staging copy to the unfinished tick.
struct RecordingPause {
    std::shared_ptr<gfx::DisplayList> list = std::move(gfx::detail::g.list);
    ~RecordingPause() { gfx::detail::g.list = std::move(list); }
};
}

size_t ImageBytes(int width, int height, int bpp) {
    if (width <= 0 || height <= 0 || (bpp != 16 && bpp != 24 && bpp != 32)) return 0;
    if (static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / (bpp / 8)) return 0;
    size_t row = static_cast<size_t>(width) * (bpp / 8);
    if (row > std::numeric_limits<size_t>::max() / static_cast<size_t>(height)) return 0;
    return row * height;
}

bool PackColor(const uint8_t *rgba, size_t rgba_bytes, int width, int height, int bpp,
               void *destination, size_t capacity) {
    size_t bytes = ImageBytes(width, height, bpp);
    size_t source_bytes = ImageBytes(width, height, 32);
    if (!rgba || !destination || !bytes || !source_bytes || capacity < bytes || rgba_bytes < source_bytes)
        return false;
    auto *dst = static_cast<uint8_t *>(destination);
    for (size_t i = 0; i < source_bytes / 4; ++i) {
        const uint8_t *p = rgba + i * 4;
        if (bpp == 16) {
            // GS PSMCT16 / PSMCT16S: little-endian ABGR1555, alpha from the high bit.
            unsigned gs_alpha = (unsigned(p[3]) * 128 + 127) / 255;
            uint16_t pixel = (p[0] >> 3) | ((p[1] >> 3) << 5) | ((p[2] >> 3) << 10) |
                             ((gs_alpha & 0x80) ? 0x8000 : 0);
            dst[i * 2] = static_cast<uint8_t>(pixel);
            dst[i * 2 + 1] = static_cast<uint8_t>(pixel >> 8);
        } else {
            size_t at = i * (bpp / 8);
            std::memcpy(dst + at, p, 3);
            if (bpp == 32) dst[at + 3] = static_cast<uint8_t>((unsigned(p[3]) * 128 + 127) / 255);
        }
    }
    return true;
}

bool HalfColor(const uint8_t *rgba, size_t rgba_bytes, int width, int height,
               uint8_t *destination, size_t capacity, int padded_width, int padded_height) {
    size_t source_bytes = ImageBytes(width, height, 32);
    size_t bytes = ImageBytes(padded_width, padded_height, 32);
    if (!rgba || !destination || width < 2 || height < 2 || !source_bytes || rgba_bytes < source_bytes ||
        !bytes || capacity < bytes || padded_width < width / 2 || padded_height < height / 2) return false;
    std::memset(destination, 0, bytes);
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            const uint8_t *a = rgba + (static_cast<size_t>(y * 2) * width + x * 2) * 4;
            const uint8_t *b = a + static_cast<size_t>(width) * 4;
            uint8_t *dst = destination + (static_cast<size_t>(y) * padded_width + x) * 4;
            for (int c = 0; c < 3; ++c) dst[c] = (unsigned(a[c]) + a[c + 4] + b[c] + b[c + 4]) >> 2;
            dst[3] = 0x80;
        }
    }
    return true;
}

bool ReadColor(gfx::TextureHandle source, gfx::Rect rect, int width, int height, int bpp,
               void *destination, size_t capacity) {
    size_t bytes = ImageBytes(width, height, bpp);
    if (!bytes || !destination || capacity < bytes || rect.w <= 0 || rect.h <= 0 ||
        gfx::detail::g.in_frame || (gfx::Recording() && source != gfx::kPreviousFrame)) return false;
    auto info = gfx::GetTextureInfo(source);
    if (!info || info->format != gfx::TextureFormat::Rgba8 || rect.x < 0 || rect.y < 0 ||
        static_cast<uint64_t>(rect.x) + rect.w > info->width ||
        static_cast<uint64_t>(rect.y) + rect.h > info->height) return false;
    RecordingPause pause;
    gfx::TextureDesc desc;
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    gfx::TextureHandle staging = gfx::CreateTexture(desc);
    if (staging == gfx::kNullTexture) return false;
    std::vector<uint8_t> rgba;
    uint32_t read_width = 0, read_height = 0;
    bool ok = gfx::BlitTexture(source, rect, staging, {0, 0, width, height}, gfx::Filter::Nearest) &&
              gfx::ReadbackTexture(staging, rgba, read_width, read_height) &&
              read_width == static_cast<uint32_t>(width) && read_height == static_cast<uint32_t>(height) &&
              PackColor(rgba.data(), rgba.size(), width, height, bpp, destination, capacity);
    gfx::DestroyTexture(staging);
    return ok;
}
}
