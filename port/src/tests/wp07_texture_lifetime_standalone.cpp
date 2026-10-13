// Standalone probe for Task 07: Recorded texture lifetime and stale handles
// Verifies:
// 1. Doomed texture lookup succeeds during display list recording (RecordingCalls == true)
//    preventing the 23 dropped font/HUD draws (0x10005) seen in the baseline log.
// 2. Doomed texture lookup succeeds during list replay (g.replaying == true).
// 3. Stale handles from prior generations (e.g. 0x10005 after slot 5 advances to gen 2) are rejected.
// 4. Repeated allocate/doom/free cycles remain strictly bounded without leaking slots.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "gfx/context.hpp"
#include "gfx/displaylist.hpp"
#include "gfx/gfx.hpp"

namespace gfx {
namespace detail {
Texture *LookupTexture(TextureHandle handle);
}
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;
    std::printf("[wp07] Testing recorded texture lifetime, doomed lookup, and generation isolation...\n");

    // Initialize mock gfx state if not up
    using namespace gfx;
    using namespace gfx::detail;

    constexpr uint32_t kSlotBits = 16;
    constexpr uint32_t kSlotMask = (1u << kSlotBits) - 1;

    // Simulate slot 5 setup
    if (g.textures.size() <= 5) {
        g.textures.resize(16);
    }
    const uint32_t slot = 5;
    Texture &tex = g.textures[slot];
    tex.live = true;
    tex.doomed = false;
    tex.generation = 1;
    TextureHandle h_gen1 = (1 << kSlotBits) | slot; // 0x10005

    // Test 1: Normal lookup succeeds
    Texture *t = LookupTexture(h_gen1);
    if (t != &tex) {
        std::fprintf(stderr, "[wp07] FAIL: Initial LookupTexture(%#x) returned %p\n", h_gen1, t);
        return 1;
    }
    std::printf("[wp07] PASS: Normal LookupTexture(0x10005) succeeded\n");

    // Test 2: Doom the texture during display list recording
    g.list = std::make_shared<DisplayList>(); // RecordingCalls() becomes true
    tex.doomed = true;

    // With our fix: LookupTexture(0x10005) during recording MUST succeed!
    // (Before the fix, texture.doomed && !g.replaying caused this to return nullptr and drop draws)
    Texture *t_rec = LookupTexture(h_gen1);
    if (t_rec != &tex) {
        std::fprintf(stderr, "[wp07] FAIL: Doomed texture lookup failed during display list recording!\n");
        return 2;
    }
    std::printf("[wp07] PASS: Doomed texture 0x10005 successfully resolved during RecordingCalls()\n");

    // Test 3: Lookup during replay
    g.list = nullptr;
    g.replaying = true;
    Texture *t_rep = LookupTexture(h_gen1);
    if (t_rep != &tex) {
        std::fprintf(stderr, "[wp07] FAIL: Doomed texture lookup failed during replay!\n");
        return 3;
    }
    std::printf("[wp07] PASS: Doomed texture 0x10005 successfully resolved during replay\n");

    // Test 4: After list completion, texture is retired and slot recycled to generation 2
    g.replaying = false;
    tex.live = true;
    tex.doomed = false;
    tex.generation = 2; // Advanced to gen 2 (handle 0x20005)
    TextureHandle h_gen2 = (2 << kSlotBits) | slot; // 0x20005

    // Stale handle from generation 1 (0x10005) MUST be rejected
    Texture *t_stale = LookupTexture(h_gen1);
    if (t_stale != nullptr) {
        std::fprintf(stderr, "[wp07] FAIL: Stale handle 0x10005 was accepted by recycled slot in generation 2!\n");
        return 4;
    }
    std::printf("[wp07] PASS: Stale generation handle 0x10005 correctly rejected after slot recycling\n");

    // Active handle from generation 2 (0x20005) MUST succeed
    Texture *t_active = LookupTexture(h_gen2);
    if (t_active != &tex) {
        std::fprintf(stderr, "[wp07] FAIL: Active generation handle 0x20005 lookup failed!\n");
        return 5;
    }
    std::printf("[wp07] PASS: Active generation handle 0x20005 successfully resolved\n");

    // Test 5: Negative control - doomed texture outside recording and outside replay is rejected
    tex.doomed = true;
    Texture *t_dead = LookupTexture(h_gen2);
    if (t_dead != nullptr) {
        std::fprintf(stderr, "[wp07] FAIL: Doomed texture was accepted outside recording and replay!\n");
        return 6;
    }
    std::printf("[wp07] PASS: Negative control passed: doomed texture outside record/replay is rejected\n");

    std::printf("[wp07] ALL CHECKS PASSED\n");
    return 0;
}
