#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace mgport {

struct ScreenExtent {
    int width;
    int height;
};

// Fill mode exposes a larger, centred logical rectangle. Keep the original GS dimensions for a
// 4:3/letterboxed view, and expand each projection/culling extent to cover the visible span.
inline int ScreenExtentForVisibleSpan(int gs_extent, float logical_extent, float visible_extent) {
    if (gs_extent <= 0 || !std::isfinite(logical_extent) || !std::isfinite(visible_extent) ||
        logical_extent <= 0.0f || visible_extent <= 0.0f) {
        return gs_extent;
    }

    const double scale = std::max(1.0, static_cast<double>(visible_extent) / logical_extent);
    const double even_extent = std::ceil(static_cast<double>(gs_extent) * scale * 0.5) * 2.0;
    if (!std::isfinite(even_extent) || even_extent > std::numeric_limits<int>::max()) {
        return gs_extent;
    }
    return std::max(gs_extent, static_cast<int>(even_extent));
}

inline ScreenExtent ScreenExtentForVisibleRect(int gs_width, int gs_height, float logical_width,
                                               float logical_height, float visible_width,
                                               float visible_height) {
    return {ScreenExtentForVisibleSpan(gs_width, logical_width, visible_width),
            ScreenExtentForVisibleSpan(gs_height, logical_height, visible_height)};
}

} // namespace mgport
