// Vulkan bring-up check for headless machines. Builds against the port's host layer (dc_host)
// alone - no game objects - so a new server or driver can be checked before the game links.
//
// It runs the frame path the game's --offscreen takes (clear, readback, PNG) and, with --surface,
// the --headless path through SDL's offscreen window and VK_EXT_headless_surface.
//
// Not wired into CMake: it is a bring-up tool, not part of the game. Build it with build.sh beside
// this file against a configured port build tree; see /home/jeffie/reports/port3-G1.md.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#include "gfx/gfx.hpp"
#include "platform/window.hpp"

namespace {

int g_failures = 0;

void Expect(bool ok, const char *what) {
    std::printf("g1: %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        g_failures++;
    }
}

std::string Hex(const uint8_t *p) {
    char text[32];
    std::snprintf(text, sizeof(text), "%02x,%02x,%02x,%02x", p[0], p[1], p[2], p[3]);
    return text;
}

void CheckPixel(const std::vector<uint8_t> &rgba, uint32_t width, uint32_t x, uint32_t y,
                const uint8_t want[4], const char *what) {
    const uint8_t *p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
    bool           ok = p[0] == want[0] && p[1] == want[1] && p[2] == want[2] && p[3] == want[3];
    std::printf("g1: %s %s pixel(%u,%u)=%s want=%02x,%02x,%02x,%02x\n", ok ? "ok  " : "FAIL", what, x, y,
                Hex(p).c_str(), want[0], want[1], want[2], want[3]);
    if (!ok) {
        g_failures++;
    }
}

// A stored-deflate PNG from WritePng: signature, IHDR size, IDAT present. The fixed layout is
// signature 0..7, IHDR length 8..11, type 12..15, size 16..23, CRC 29..32, IDAT type 37..40.
bool PngLooksValid(const std::filesystem::path &path, uint32_t width, uint32_t height) {
    std::FILE *file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t header[41];
    size_t  got = std::fread(header, 1, sizeof(header), file);
    std::fclose(file);
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (got != sizeof(header) || std::memcmp(header, signature, sizeof(signature)) != 0) {
        return false;
    }
    if (std::memcmp(header + 12, "IHDR", 4) != 0) {
        return false;
    }
    auto big_endian = [&](int at) {
        return static_cast<uint32_t>(header[at]) << 24 | static_cast<uint32_t>(header[at + 1]) << 16 |
               static_cast<uint32_t>(header[at + 2]) << 8 | header[at + 3];
    };
    return big_endian(16) == width && big_endian(20) == height && std::memcmp(header + 37, "IDAT", 4) == 0;
}

} // namespace

int main(int argc, char **argv) {
    bool        surface = false;
    const char *png = "g1-clear.png";
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--surface") == 0) {
            surface = true;
        } else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc) {
            png = argv[++i];
        } else {
            std::fprintf(stderr, "usage: %s [--surface] [--png PATH]\n", argv[0]);
            return 2;
        }
    }

    // What the game's --headless checks before it decides surface or offscreen.
    std::printf("g1: HeadlessSurfaceAvailable=%d (mode: %s)\n", gfx::HeadlessSurfaceAvailable() ? 1 : 0,
                surface ? "surface" : "offscreen");

    WindowConfig window{640, 480, true};
    window.vulkan = surface;
    WindowInit(window);

    gfx::RendererConfig config;
    config.offscreen = !surface;
    // The bring-up server has no VK_LAYER_KHRONOS_validation; asking for it would only log.
    config.validation = false;
    config.pipeline_cache = std::filesystem::temp_directory_path() / "dc_g1" / "pipeline_cache.bin";
    config.progress = [](uint32_t done, uint32_t total) {
        if (done == total) {
            std::printf("g1: compiled %u pipelines\n", total);
        }
    };
    gfx::RendererInit(WindowHandle(), config);

    gfx::RendererFeatures features = gfx::ActiveRendererFeatures();
    std::printf("g1: features api=%u.%u.%u offscreen=%d fans=%d stencil_masks=%d dyn_cwm=%d portability=%d\n",
                features.api_version >> 22, (features.api_version >> 12) & 0x3FF, features.api_version & 0xFFF,
                features.offscreen, features.triangle_fans, features.separate_stencil_masks,
                features.dynamic_color_write_mask, features.portability_subset);
    std::printf("g1: pipelines=%u\n", gfx::PipelineCount());

    std::vector<uint8_t> pixels;
    uint32_t             width = 0;
    uint32_t             height = 0;

    // Frame 0: fill the colour buffer, read it back.
    const uint8_t color0[4] = {0x40, 0x80, 0xC0, 0x80};
    const uint8_t want0[4] = {0x40, 0x80, 0xC0, 0xFF};
    Expect(gfx::BeginFrame(), "BeginFrame 0");
    gfx::Clear(true, color0, true, 0.0f);
    gfx::EndFrame();
    bool read0 = gfx::ReadbackFrame(pixels, width, height);
    Expect(read0, "ReadbackFrame 0");
    std::printf("g1: frame0 %ux%u bytes=%zu\n", width, height, pixels.size());
    if (read0 && width == 640 && height == 480) {
        CheckPixel(pixels, width, 320, 240, want0, "frame0");
        CheckPixel(pixels, width, 0, 0, want0, "frame0");
    } else if (read0) {
        Expect(false, "frame0 size 640x480");
    }

    // Frame 1: a second colour, then the screenshot path the game's --screenshot uses.
    const uint8_t color1[4] = {0x20, 0x40, 0x60, 0x80};
    const uint8_t want1[4] = {0x20, 0x40, 0x60, 0xFF};
    Expect(gfx::BeginFrame(), "BeginFrame 1");
    gfx::Clear(true, color1, true, 0.0f);
    gfx::EndFrame();
    bool read1 = gfx::ReadbackFrame(pixels, width, height);
    Expect(read1, "ReadbackFrame 1");
    if (read1 && width == 640 && height == 480) {
        CheckPixel(pixels, width, 320, 240, want1, "frame1");
    }
    bool wrote = read1 && gfx::WritePng(png, pixels.data(), width, height);
    Expect(wrote, "WritePng");
    Expect(wrote && PngLooksValid(png, width, height), "PNG signature, IHDR and IDAT");

    // Zero with DC_VULKAN_VALIDATION set and the layer installed means the run was clean.
    uint32_t messages = gfx::ValidationMessageCount();
    std::printf("g1: validation_messages=%u\n", messages);
    Expect(messages == 0, "no validation messages");

    gfx::RendererShutdown();
    WindowShutdown();

    if (g_failures == 0) {
        std::printf("g1: PASS\n");
        return 0;
    }
    std::printf("g1: FAIL %d checks\n", g_failures);
    return 1;
}
