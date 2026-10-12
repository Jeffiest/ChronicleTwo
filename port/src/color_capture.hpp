#pragma once

#include <cstddef>
#include <cstdint>
#include "gfx/gfx.hpp"

namespace port_capture {

// Packed, top-to-bottom GS color bytes. Unsupported formats or short buffers write nothing.
size_t ImageBytes(int width, int height, int bpp);
bool PackColor(const uint8_t *rgba, size_t rgba_bytes, int width, int height, int bpp,
               void *destination, size_t capacity);
bool HalfColor(const uint8_t *rgba, size_t rgba_bytes, int width, int height,
               uint8_t *destination, size_t capacity, int padded_width, int padded_height);

// Reads a logical rectangle through a plain, unscaled staging texture. This excludes window
// bars and display interpolation, and follows renderer mappings after a resize. Synchronous
// completed-frame readback preserves an open display list. Current targets and explicit
// textures require recording to be closed; active GPU frames reject every readback.
bool ReadColor(gfx::TextureHandle source, gfx::Rect rect, int width, int height, int bpp,
               void *destination, size_t capacity);
}
