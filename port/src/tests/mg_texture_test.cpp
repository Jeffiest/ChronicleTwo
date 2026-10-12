#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "gfx/gfx.hpp"
#include "gfx/tim2.hpp"
#include "gfx_fixture.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mg_texture_port.hpp"

namespace {

using Quads = std::vector<u_long128>;
using Texel4 = std::array<uint8_t, 4>;
using Levels = std::array<u_long128 *, MG_TEXTURE_LEVEL_MAX>;

constexpr int kTextureCount = 64;
constexpr int kBlockCount = 8;
constexpr size_t kStorageQuads = 4096;

// The retail converter the game runs on 8-bit pixels stored in 32-bit page order, copied from
// ps2/src/mg_texture.cpp as the reference Unswizzle8 is checked against.
/**
 *
 * Maps texels in alternating 32-bit columns to their 8-bit block positions.
 *
 */
static u_char lut_1246[128] __attribute__((aligned(16))) = {
    0x00,
    0x24,
    0x08,
    0x2C,
    0x01,
    0x25,
    0x09,
    0x2D,
    0x02,
    0x26,
    0x0A,
    0x2E,
    0x03,
    0x27,
    0x0B,
    0x2F,
    0x04,
    0x20,
    0x0C,
    0x28,
    0x05,
    0x21,
    0x0D,
    0x29,
    0x06,
    0x22,
    0x0E,
    0x2A,
    0x07,
    0x23,
    0x0F,
    0x2B,
    0x10,
    0x34,
    0x18,
    0x3C,
    0x11,
    0x35,
    0x19,
    0x3D,
    0x12,
    0x36,
    0x1A,
    0x3E,
    0x13,
    0x37,
    0x1B,
    0x3F,
    0x14,
    0x30,
    0x1C,
    0x38,
    0x15,
    0x31,
    0x1D,
    0x39,
    0x16,
    0x32,
    0x1E,
    0x3A,
    0x17,
    0x33,
    0x1F,
    0x3B,
    0x04,
    0x20,
    0x0C,
    0x28,
    0x05,
    0x21,
    0x0D,
    0x29,
    0x06,
    0x22,
    0x0E,
    0x2A,
    0x07,
    0x23,
    0x0F,
    0x2B,
    0x00,
    0x24,
    0x08,
    0x2C,
    0x01,
    0x25,
    0x09,
    0x2D,
    0x02,
    0x26,
    0x0A,
    0x2E,
    0x03,
    0x27,
    0x0B,
    0x2F,
    0x14,
    0x30,
    0x1C,
    0x38,
    0x15,
    0x31,
    0x1D,
    0x39,
    0x16,
    0x32,
    0x1E,
    0x3A,
    0x17,
    0x33,
    0x1F,
    0x3B,
    0x10,
    0x34,
    0x18,
    0x3C,
    0x11,
    0x35,
    0x19,
    0x3D,
    0x12,
    0x36,
    0x1A,
    0x3E,
    0x13,
    0x37,
    0x1B,
    0x3F,
};

/**
 *
 * Maps row-major 8-bit blocks to their GS page block numbers.
 *
 */
static int block_table8_1266[32] __attribute__((aligned(16))) = {
    0,
    1,
    4,
    5,
    16,
    17,
    20,
    21,
    2,
    3,
    6,
    7,
    18,
    19,
    22,
    23,
    8,
    9,
    12,
    13,
    24,
    25,
    28,
    29,
    10,
    11,
    14,
    15,
    26,
    27,
    30,
    31,
};

/**
 *
 * Maps row-major 32-bit blocks to their GS page block numbers.
 *
 */
static int block_table32_1267[32] __attribute__((aligned(16))) = {
    0,
    1,
    4,
    5,
    16,
    17,
    20,
    21,
    2,
    3,
    6,
    7,
    18,
    19,
    22,
    23,
    8,
    9,
    12,
    13,
    24,
    25,
    28,
    29,
    10,
    11,
    14,
    15,
    26,
    27,
    30,
    31,
};

/**
 *
 * Reorders one block of 8-bit pixels stored in 32-bit page order back
 * into linear order.
 *
 */
// Each block is four columns of 64 bytes; odd columns use the second half of the table.
static int BlockConv32to8(u_char *src, u_char *dst) {
    u_int column;
    u_int texel;
    u_int row;
    int   lut_index;
    int   src_index;
    src_index = 0;

    for (row = 0; row < 4; row++) {
        lut_index = (row & 1) << 6;

        for (column = 0; column < 16; column++) {
            for (texel = 0; texel < 4; texel++) {
                u_char dst_index = lut_1246[lut_index];
                lut_index++;
                dst[dst_index] = src[src_index];
                src_index++;
            }
        }

        dst += 0x40;
    }

    return 0;
}

/**
 *
 * Reorders one page of 8-bit pixels stored in 32-bit page order back
 * into linear order, block by block.
 *
 */
static int PageConv32to8(int width, int height, u_char *src, u_char *dst) {
    int     block_column[32];
    int     block_row[32];
    u_char  block_out[0x100];
    u_char  block_in[0x100];
    int     index;
    int     row;
    int     column;
    int     line;
    int     blocks_wide;
    u_char *block_ptr;
    u_char *page_ptr;
    int     blocks_high;
    int     block;

    index = 0;

    for (row = 0; row < 4; row++) {
        for (column = 0; column < 8; column++) {
            int entry = block_table32_1267[index];
            block_column[entry] = column;
            block_row[entry] = row;
            index++;
        }
    }

    blocks_wide = width / 16;
    blocks_high = height / 16;
    memset(block_out, 0, sizeof(block_out));
    memset(block_in, 0, sizeof(block_in));

    for (row = 0; row < blocks_high; row++) {
        for (column = 0; column < blocks_wide; column++) {
            block = block_table8_1266[column + row * blocks_wide];
            block_ptr = block_in;
            page_ptr = src + (block_row[block] << 11) + (block_column[block] << 5);

            for (line = 0; line < 8; line++) {
                memcpy(block_ptr, page_ptr, 0x20);
                block_ptr += 0x20;
                page_ptr += 0x100;
            }

            BlockConv32to8(block_in, block_out);
            block_ptr = block_out;
            page_ptr = dst + (row << 11) + column * 0x10;

            for (line = 0; line < 0x10; line++) {
                memcpy(page_ptr, block_ptr, 0x10);
                block_ptr += 0x10;
                page_ptr += 0x80;
            }
        }
    }

    return 0;
}

/**
 *
 * Converts 8-bit pixels stored in 32-bit page order back into linear
 * order in place; gives 0 when the image is too large to convert.
 *
 */
static int Conv32To8(int width, int height, u_char *image) {
    static u_char conv_work[0x10000];
    u_char        work8[0x2000];
    u_char        work32[0x2000];
    int           k;
    int           pages_x;
    int           row_count;
    int           row_bytes;
    int           j;
    int           i;
    int           pages_y;
    u_char       *source_cursor;
    int           size;
    u_char       *work_cursor;
    int           page_width;
    u_char       *destination_cursor;

    size = width * height;

    if (size > 0x10000) {
        return 0;
    }

    memset(work8, 0, sizeof(work8));
    memset(work32, 0, sizeof(work32));
    pages_x = (width - 1) / 128 + 1;
    pages_y = (height - 1) / 64 + 1;

    page_width = 128;
    row_bytes = 256;

    if (pages_x == 1) {
        row_bytes = width * 2;
    } else {
        width = page_width;
    }

    if (pages_y == 1) {
        row_count = height / 2;
    } else {
        height = 64;
        row_count = 32;
    }

    for (i = 0; i < pages_y; i++) {
        for (j = 0; j < pages_x; j++) {
            source_cursor = image + i * (pages_x * (row_bytes * row_count)) + row_bytes * j;
            work_cursor = work32;

            for (k = 0; k < row_count; k++) {
                memcpy(work_cursor, source_cursor, row_bytes);
                source_cursor += row_bytes * pages_x;
                work_cursor += 256;
            }

            PageConv32to8(128, 64, work32, work8);
            destination_cursor = conv_work + i * (pages_x * (width * 64)) + width * j;
            work_cursor = work8;

            for (k = 0; k < height; k++) {
                memcpy(destination_cursor, work_cursor, width);
                destination_cursor += width * pages_x;
                work_cursor += 128;
            }
        }
    }

    memcpy(image, conv_work, size);
    return 1;
}

template <class T>
uint32_t U(T value) {
    return static_cast<uint32_t>(value);
}

Quads Pack(const std::vector<uint8_t> &bytes) {
    Quads quads((bytes.size() + 15) / 16 + 1, 0);
    std::memcpy(quads.data(), bytes.data(), bytes.size());
    return quads;
}

void PutLe(std::vector<uint8_t> &buf, size_t offset, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) {
        buf[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

std::vector<uint8_t> Ct32Pixels(uint32_t width, uint32_t height) {
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t *p = &out[(static_cast<size_t>(y) * width + x) * 4];
            p[0] = static_cast<uint8_t>(x * 40);
            p[1] = static_cast<uint8_t>(y * 40);
            p[2] = 0x10;
            p[3] = 0x80;
        }
    }
    return out;
}

// Entry p is (p, 255 - p, 0x10, 0x80): file entry p lands at palette index ClutCsm1(p).
std::vector<uint8_t> Clut8() {
    std::vector<uint8_t> out(256 * 4);
    for (size_t p = 0; p < 256; p++) {
        out[p * 4] = static_cast<uint8_t>(p);
        out[p * 4 + 1] = static_cast<uint8_t>(255 - p);
        out[p * 4 + 2] = 0x10;
        out[p * 4 + 3] = 0x80;
    }
    return out;
}

struct TimPicture {
    uint8_t                           image_type = TIM2_RGB32;
    uint16_t                          width = 0;
    uint16_t                          height = 0;
    std::vector<std::vector<uint8_t>> levels;
    std::vector<uint8_t>              clut;
    uint64_t                          tex1 = 0;
};

std::vector<uint8_t> BuildPicture(const TimPicture &picture) {
    const bool   mipmapped = picture.levels.size() > 1;
    const size_t header = mipmapped ? 0x60 : 0x30;
    size_t       image_bytes = 0;
    for (const std::vector<uint8_t> &level : picture.levels) {
        image_bytes += level.size();
    }

    std::vector<uint8_t> out(header + image_bytes + picture.clut.size(), 0);
    PutLe(out, 0x00, out.size(), 4);
    PutLe(out, 0x04, picture.clut.size(), 4);
    PutLe(out, 0x08, image_bytes, 4);
    PutLe(out, 0x0C, header, 2);
    out[0x11] = static_cast<uint8_t>(picture.levels.size());
    out[0x12] = 3;
    out[0x13] = picture.image_type;
    PutLe(out, 0x14, picture.width, 2);
    PutLe(out, 0x16, picture.height, 2);
    PutLe(out, 0x20, picture.tex1, 8);

    size_t at = header;
    for (size_t i = 0; i < picture.levels.size(); i++) {
        if (mipmapped) {
            PutLe(out, 0x40 + 4 * i, picture.levels[i].size(), 4);
        }
        std::copy(picture.levels[i].begin(), picture.levels[i].end(), out.begin() + static_cast<std::ptrdiff_t>(at));
        at += picture.levels[i].size();
    }
    std::copy(picture.clut.begin(), picture.clut.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
    return out;
}

std::vector<uint8_t> Tim2File(const TimPicture &picture) {
    const std::vector<uint8_t> body = BuildPicture(picture);
    std::vector<uint8_t>       file(0x10, 0);
    std::memcpy(file.data(), "TIM2", 4);
    file[4] = 4;
    PutLe(file, 6, 1, 2);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

struct NamedFile {
    std::string          name;
    std::vector<uint8_t> tim2;
};

std::vector<uint8_t> Im2Archive(const std::vector<NamedFile> &files) {
    std::vector<uint8_t> out(0x10 + 0x30 * files.size(), 0);
    std::memcpy(out.data(), "IM2", 4);
    PutLe(out, 4, files.size(), 4);
    for (size_t i = 0; i < files.size(); i++) {
        const size_t entry = 0x10 + 0x30 * i;
        std::memcpy(&out[entry], files[i].name.data(), files[i].name.size());
        while (out.size() % 16 != 0) {
            out.push_back(0);
        }
        PutLe(out, entry + 0x20, out.size(), 4);
        out.insert(out.end(), files[i].tim2.begin(), files[i].tim2.end());
    }
    return out;
}

// The manager over a quadword buffer; Initialize releases its textures while the renderer is still up.
class Harness {
public:
    Harness() {
        memory.stSetBuffer(storage.data(), static_cast<int>(kStorageQuads));
        manager.SetTableBuffer(kTextureCount, kBlockCount, &memory);
        manager.Initialize(0x1000, -1);
    }

    ~Harness() {
        manager.Initialize(0x1000, -1);
    }

    Harness(const Harness &) = delete;
    Harness &operator=(const Harness &) = delete;

    Quads         storage = Quads(kStorageQuads, 0);
    mgCMemory     memory;
    mgCTextureManager manager;
};

mgCTexture *Enter(Harness &h, int block, const std::string &name, int width, int height, int bpp,
                  Levels levels = {}, u_long128 *clut = nullptr, u_long tex1 = 0, int swizzled = 0) {
    std::string owned = name;
    return h.manager.EnterTexture(block, owned.data(), levels.data(), width, height, bpp, clut, tex1, swizzled);
}

uint32_t Tbp0(const mgCTexture *texture) {
    return U(texture->tex0.TBP0);
}

uint32_t Cbp(const mgCTexture *texture) {
    return U(texture->tex0.CBP);
}

struct Readback {
    std::vector<uint8_t> pixels;
    uint32_t             width = 0;
    uint32_t             height = 0;
};

Readback Read(gfx::TextureHandle texture) {
    Readback result;
    EXPECT_TRUE(gfx::ReadbackTexture(texture, result.pixels, result.width, result.height));
    return result;
}

Texel4 TexelOf(const Readback &readback, uint32_t x, uint32_t y) {
    const uint8_t *p = &readback.pixels[(static_cast<size_t>(y) * readback.width + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}

Texel4 PaletteAt(gfx::TextureHandle palette, uint32_t index) {
    const Readback readback = Read(palette);
    return TexelOf(readback, index, 0);
}

void ExpectUnswizzleMatchesConverter(uint32_t width, uint32_t height) {
    std::mt19937                       rng(0x5EED);
    std::uniform_int_distribution<int> byte(0, 0xFF);
    std::vector<uint8_t>               src(static_cast<size_t>(width) * height);
    for (uint8_t &value : src) {
        value = static_cast<uint8_t>(byte(rng));
    }

    std::vector<uint8_t> expected = src;
    ASSERT_EQ(Conv32To8(static_cast<int>(width), static_cast<int>(height), expected.data()), 1);
    std::vector<uint8_t> actual(src.size());
    gfx::tim2::Unswizzle8(src.data(), actual.data(), width, height);

    size_t first = 0;
    while (first < src.size() && actual[first] == expected[first]) {
        first++;
    }
    EXPECT_EQ(first, src.size()) << width << "x" << height << ": first difference at index " << first;
}

// Mirrors the retail name hash: signed chars and a u_char accumulator, so high-bit names wrap as they do in retail.
int RetailBucket(const std::string &name) {
    uint8_t value = 0;
    for (char c : name) {
        value = static_cast<uint8_t>(((value << 8) + static_cast<signed char>(c)) % 101);
    }
    return value;
}

} // namespace

TEST(MgTexture, KeysLiveInTex0) {
    Harness h;
    Quads   first_px = Pack(Ct32Pixels(24, 10));
    Quads   second_px = Pack(Ct32Pixels(24, 10));
    mgCTexture *first = Enter(h, 0, "first", 24, 10, 32, {first_px.data()});
    mgCTexture *second = Enter(h, 0, "second", 24, 10, 32, {second_px.data()});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_EQ(Tbp0(first), Cbp(first));
    EXPECT_EQ(Tbp0(second), Cbp(second));
    EXPECT_NE(Tbp0(first), Tbp0(second));
    EXPECT_EQ(Tbp0(first), kMgTextureKeyFirst);
    for (const mgCTexture *texture : {first, second}) {
        EXPECT_GE(Tbp0(texture), kMgTextureKeyLast);
        EXPECT_LE(Tbp0(texture), kMgTextureKeyFirst);
    }

    EXPECT_EQ(U(first->tex0.PSM), U(SCE_GS_PSMCT32));
    EXPECT_EQ(U(first->tex0.TW), 5u);
    EXPECT_EQ(U(first->tex0.TH), 4u);
    EXPECT_EQ(U(first->tex0.TBW), 1u);
    EXPECT_EQ(U(first->tex0.TCC), 1u);
    EXPECT_EQ(U(first->tex0.CLD), 0u);
    EXPECT_EQ(first->tex1_bits, static_cast<u_long>(SCE_GS_SET_TEX1(0, 0, 1, 1, 1, 0, 0)));

    Quads clut = Pack(Clut8());
    Quads indices = Pack(std::vector<uint8_t>(24 * 10, 0));
    mgCTexture *indexed = Enter(h, 0, "indexed", 24, 10, 8, {indices.data()}, clut.data());
    ASSERT_NE(indexed, nullptr);
    EXPECT_EQ(U(indexed->tex0.PSM), U(SCE_GS_PSMT8));
    EXPECT_EQ(U(indexed->tex0.TW), 5u);
    EXPECT_EQ(U(indexed->tex0.TH), 4u);
    EXPECT_EQ(U(indexed->tex0.CLD), 1u);
    EXPECT_EQ(indexed->clut, clut.data());

    mgCTexture *wide = Enter(h, 0, "wide", 130, 2, 32);
    ASSERT_NE(wide, nullptr);
    EXPECT_EQ(U(wide->tex0.TBW), 3u);
    EXPECT_EQ(U(wide->tex0.TW), 8u);
    EXPECT_EQ(U(wide->tex0.TH), 1u);

    const uint32_t freed = Tbp0(first);
    h.manager.DeleteTexture(first);
    mgCTexture *reused = Enter(h, 0, "reused", 24, 10, 32);
    ASSERT_NE(reused, nullptr);
    EXPECT_EQ(Tbp0(reused), freed);
}

TEST(MgTexture, FourMipLevelsKept) {
    Harness h;
    Quads   l0 = Pack(Ct32Pixels(32, 32));
    Quads   l1 = Pack(Ct32Pixels(16, 16));
    Quads   l2 = Pack(Ct32Pixels(8, 8));
    Quads   l3 = Pack(Ct32Pixels(4, 4));
    const Levels levels = {l0.data(), l1.data(), l2.data(), l3.data()};

    mgCTexture *plain = Enter(h, 0, "plain", 32, 32, 32, levels);
    ASSERT_NE(plain, nullptr);
    for (size_t i = 0; i < levels.size(); i++) {
        EXPECT_EQ(plain->image[i], levels[i]) << "level " << i;
    }
    EXPECT_EQ(U(plain->tex1.MXL), 2u);

    mgCTexture *biased = Enter(h, 0, "biased", 32, 32, 32, levels, nullptr, SCE_GS_SET_TEX1(0, 0, 1, 1, 1, 2, 5));
    ASSERT_NE(biased, nullptr);
    for (size_t i = 0; i < levels.size(); i++) {
        EXPECT_EQ(biased->image[i], levels[i]) << "level " << i;
    }
    EXPECT_EQ(U(biased->tex1.MXL), 3u);
    EXPECT_EQ(U(biased->tex1.L), 2u);
    EXPECT_EQ(U(biased->tex1.K), 5u);
}

TEST(MgTexture, RemainVramUnbounded) {
    Harness h;
    for (int i = 0; i < 8; i++) {
        ASSERT_NE(Enter(h, 0, "big" + std::to_string(i), 1024, 1024, 32), nullptr) << i;
    }
    EXPECT_EQ(h.manager.GetRemainVRAM(0), INT_MAX);
    EXPECT_EQ(h.manager.GetRemainVRAM(-1), 0);
    EXPECT_EQ(h.manager.GetRemainVRAM(h.manager.block_max), 0);

    std::vector<NamedFile> files;
    for (int i = 0; i < 5; i++) {
        TimPicture picture;
        picture.image_type = TIM2_RGB32;
        picture.width = 4;
        picture.height = 4;
        picture.levels = {Ct32Pixels(4, 4)};
        files.push_back({"im2_" + std::to_string(i), Tim2File(picture)});
    }
    Quads archive = Pack(Im2Archive(files));
    h.manager.EnterIMGFile(reinterpret_cast<u_char *>(archive.data()), 2, &h.memory, nullptr);

    int count = 0;
    for (const mgCTexture *texture = h.manager.GetTextureBlock(2)->texture; texture != nullptr; texture = texture->next) {
        EXPECT_EQ(texture->block, 2);
        count++;
    }
    EXPECT_EQ(count, 5);
}

TEST(MgTexture, LookupBeforeRendererIsNull) {
    Harness h;
    Quads   px = Pack(Ct32Pixels(8, 8));
    mgCTexture *texture = Enter(h, 0, "no_renderer", 8, 8, 32, {px.data()});
    ASSERT_NE(texture, nullptr);

    const MgTextureHandles direct = MgTextureLookup(texture);
    EXPECT_EQ(direct.texture, gfx::kNullTexture);
    EXPECT_EQ(direct.palette, gfx::kNullTexture);

    const MgTextureHandles by_tex0 = MgTextureLookupTex0(static_cast<uint64_t>(texture->tex0_bits));
    EXPECT_EQ(by_tex0.texture, gfx::kNullTexture);
    EXPECT_EQ(by_tex0.palette, gfx::kNullTexture);

    EXPECT_EQ(MgTextureLookup(nullptr).texture, gfx::kNullTexture);
}

TEST(MgTexture, NoImageSwizzledEntryDoesNotTouchPixels) {
    Harness h;
    TimPicture picture;
    picture.image_type = TIM2_IDTEX8;
    picture.width = 16;
    picture.height = 16;
    picture.levels = {std::vector<uint8_t>(256, 0x5A)};
    picture.clut = Clut8();
    Quads file = Pack(Tim2File(picture));

    std::string name = "no_image";
    mgCTexture *texture =
        h.manager.EnterTexture(0, name.data(), reinterpret_cast<TM2_head *>(file.data()), 1, 1);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->image[0], nullptr);
    EXPECT_EQ(texture->swizzled, 1);
}

TEST(MgTexture, Unswizzle8MatchesRetailConv32To8) {
    const std::array<std::array<uint32_t, 2>, 6> sizes = {{{128, 64}, {128, 128}, {256, 128}, {256, 256}, {64, 64}, {32, 32}}};
    for (const std::array<uint32_t, 2> &size : sizes) {
        ExpectUnswizzleMatchesConverter(size[0], size[1]);
    }
}

class MgTextureGpu : public testing::Test {
protected:
    dc::test::GfxFixture gfx_;
    Harness              h_;
};

TEST_F(MgTextureGpu, EachTypeUploads) {
    Quads ct32_px = Pack(Ct32Pixels(4, 4));
    mgCTexture *ct32 = Enter(h_, 0, "ct32", 4, 4, 32, {ct32_px.data()});
    ASSERT_NE(ct32, nullptr);
    const MgTextureHandles ct32_handles = MgTextureLookup(ct32);
    ASSERT_NE(ct32_handles.texture, gfx::kNullTexture);
    EXPECT_EQ(ct32_handles.palette, gfx::kNullTexture);
    const Readback ct32_rb = Read(ct32_handles.texture);
    ASSERT_EQ(ct32_rb.width, 4u);
    ASSERT_EQ(ct32_rb.height, 4u);
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 4; x++) {
            EXPECT_EQ(TexelOf(ct32_rb, x, y), (Texel4{static_cast<uint8_t>(x * 40), static_cast<uint8_t>(y * 40), 0x10, 0xFF}))
                << "Ct32 texel " << x << "," << y;
        }
    }

    std::vector<uint8_t> ct24_bytes(4 * 4 * 3);
    for (size_t t = 0; t < 16; t++) {
        ct24_bytes[t * 3] = static_cast<uint8_t>(t);
        ct24_bytes[t * 3 + 1] = 0x40;
        ct24_bytes[t * 3 + 2] = 0x80;
    }
    Quads ct24_px = Pack(ct24_bytes);
    mgCTexture *ct24 = Enter(h_, 0, "ct24", 4, 4, 24, {ct24_px.data()});
    ASSERT_NE(ct24, nullptr);
    const MgTextureHandles ct24_handles = MgTextureLookup(ct24);
    ASSERT_NE(ct24_handles.texture, gfx::kNullTexture);
    const std::optional<gfx::TextureInfo> ct24_info = gfx::GetTextureInfo(ct24_handles.texture);
    ASSERT_TRUE(ct24_info.has_value());
    EXPECT_FALSE(ct24_info->has_alpha);
    const Readback ct24_rb = Read(ct24_handles.texture);
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 4; x++) {
            const size_t t = static_cast<size_t>(y) * 4 + x;
            EXPECT_EQ(TexelOf(ct24_rb, x, y), (Texel4{ct24_bytes[t * 3], ct24_bytes[t * 3 + 1], ct24_bytes[t * 3 + 2], 0xFF}))
                << "Ct24 texel " << x << "," << y;
        }
    }

    std::vector<uint8_t> ct16_bytes(4 * 4 * 2);
    for (size_t t = 0; t < 16; t++) {
        ct16_bytes[t * 2] = 0xFF;
        ct16_bytes[t * 2 + 1] = 0x7F;
    }
    Quads ct16_px = Pack(ct16_bytes);
    mgCTexture *ct16 = Enter(h_, 0, "ct16", 4, 4, 16, {ct16_px.data()});
    ASSERT_NE(ct16, nullptr);
    const MgTextureHandles ct16_handles = MgTextureLookup(ct16);
    ASSERT_NE(ct16_handles.texture, gfx::kNullTexture);
    const Readback ct16_rb = Read(ct16_handles.texture);
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 4; x++) {
            EXPECT_EQ(TexelOf(ct16_rb, x, y), (Texel4{248, 248, 248, 0xFF})) << "Ct16 texel " << x << "," << y;
        }
    }

    std::vector<uint8_t> idx8(16);
    for (size_t t = 0; t < 16; t++) {
        idx8[t] = static_cast<uint8_t>(t * 37 + 8);
    }
    Quads idx8_px = Pack(idx8);
    Quads clut8 = Pack(Clut8());
    mgCTexture *t8 = Enter(h_, 0, "t8", 4, 4, 8, {idx8_px.data()}, clut8.data());
    ASSERT_NE(t8, nullptr);
    const MgTextureHandles t8_handles = MgTextureLookup(t8);
    ASSERT_NE(t8_handles.texture, gfx::kNullTexture);
    ASSERT_NE(t8_handles.palette, gfx::kNullTexture);
    const Readback t8_rb = Read(t8_handles.texture);
    ASSERT_EQ(t8_rb.width, 4u);
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 4; x++) {
            EXPECT_EQ(t8_rb.pixels[y * 4 + x], idx8[y * 4 + x]) << "T8 index " << x << "," << y;
        }
    }
    EXPECT_EQ(PaletteAt(t8_handles.palette, 16), (Texel4{8, 247, 0x10, 0xFF}));

    std::vector<uint8_t> clut4(16 * 4);
    for (size_t p = 0; p < 16; p++) {
        clut4[p * 4] = static_cast<uint8_t>(p * 16);
        clut4[p * 4 + 1] = 0x20;
        clut4[p * 4 + 2] = 0x30;
        clut4[p * 4 + 3] = 0x80;
    }
    Quads t4_px = Pack(std::vector<uint8_t>(8, 0x21));
    Quads t4_clut = Pack(clut4);
    mgCTexture *t4 = Enter(h_, 0, "t4", 4, 4, 4, {t4_px.data()}, t4_clut.data());
    ASSERT_NE(t4, nullptr);
    const MgTextureHandles t4_handles = MgTextureLookup(t4);
    ASSERT_NE(t4_handles.texture, gfx::kNullTexture);
    ASSERT_NE(t4_handles.palette, gfx::kNullTexture);
    const Readback t4_rb = Read(t4_handles.texture);
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 4; x++) {
            EXPECT_EQ(t4_rb.pixels[y * 4 + x], ((x & 1) != 0 ? 2 : 1)) << "T4 index " << x << "," << y;
        }
    }
    for (uint32_t p = 0; p < 16; p++) {
        EXPECT_EQ(PaletteAt(t4_handles.palette, p),
                  (Texel4{static_cast<uint8_t>(p * 16), 0x20, 0x30, 0xFF}))
            << "T4 palette " << p;
    }

    const std::vector<uint8_t> wide_src = Ct32Pixels(24, 10);
    Quads wide_px = Pack(wide_src);
    mgCTexture *wide = Enter(h_, 0, "wide_24x10", 24, 10, 32, {wide_px.data()});
    ASSERT_NE(wide, nullptr);
    const MgTextureHandles wide_handles = MgTextureLookup(wide);
    ASSERT_NE(wide_handles.texture, gfx::kNullTexture);
    const std::optional<gfx::TextureInfo> wide_info = gfx::GetTextureInfo(wide_handles.texture);
    ASSERT_TRUE(wide_info.has_value());
    EXPECT_EQ(wide_info->width, 32u);
    EXPECT_EQ(wide_info->height, 16u);
    const Readback wide_rb = Read(wide_handles.texture);
    ASSERT_EQ(wide_rb.width, 32u);
    ASSERT_EQ(wide_rb.height, 16u);
    for (uint32_t y = 0; y < 16; y++) {
        for (uint32_t x = 0; x < 32; x++) {
            Texel4 expected = {};
            if (x < 24 && y < 10) {
                const size_t at = (static_cast<size_t>(y) * 24 + x) * 4;
                expected = {wide_src[at], wide_src[at + 1], wide_src[at + 2], 0xFF};
            }
            EXPECT_EQ(TexelOf(wide_rb, x, y), expected) << "24x10 texel " << x << "," << y;
        }
    }
}

TEST_F(MgTextureGpu, Im2SwizzledMipmapsDrawLinear) {
    std::mt19937                       rng(0x2A);
    std::uniform_int_distribution<int> byte(0, 0xFF);
    std::vector<uint8_t>               linear0(32 * 32);
    std::vector<uint8_t>               linear1(16 * 16);
    for (uint8_t &value : linear0) {
        value = static_cast<uint8_t>(byte(rng));
    }
    for (uint8_t &value : linear1) {
        value = static_cast<uint8_t>(byte(rng));
    }
    std::vector<uint8_t> swizzled0(linear0.size());
    std::vector<uint8_t> swizzled1(linear1.size());
    gfx::tim2::Swizzle8(linear0.data(), swizzled0.data(), 32, 32);
    gfx::tim2::Swizzle8(linear1.data(), swizzled1.data(), 16, 16);

    TimPicture picture;
    picture.image_type = TIM2_IDTEX8;
    picture.width = 32;
    picture.height = 32;
    picture.levels = {swizzled0, swizzled1};
    picture.clut = Clut8();

    Quads archive = Pack(Im2Archive({{"mip8", Tim2File(picture)}}));
    h_.manager.EnterIMGFile(reinterpret_cast<u_char *>(archive.data()), 0, &h_.memory, nullptr);

    ASSERT_NE(h_.manager.GetTextureBlock(0)->texture, nullptr);
    mgCTexture *texture = h_.manager.GetTextureBlock(0)->texture;
    EXPECT_EQ(texture->swizzled, 1);
    ASSERT_NE(texture->image[1], nullptr);

    const MgTextureHandles handles = MgTextureLookup(texture);
    ASSERT_NE(handles.texture, gfx::kNullTexture);
    const Readback readback = Read(handles.texture);
    ASSERT_EQ(readback.width, 32u);
    ASSERT_EQ(readback.height, 32u);
    EXPECT_EQ(readback.pixels, linear0);

    const std::optional<gfx::TextureInfo> info = gfx::GetTextureInfo(handles.texture);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->mip_levels, 2u);
}

TEST_F(MgTextureGpu, ReloadClutUpdatesPalette) {
    std::vector<uint8_t> idx(8 * 8);
    for (size_t t = 0; t < idx.size(); t++) {
        idx[t] = static_cast<uint8_t>(t * 5);
    }
    Quads px = Pack(idx);
    Quads clut = Pack(Clut8());
    mgCTexture *texture = Enter(h_, 0, "reload_clut", 8, 8, 8, {px.data()}, clut.data());
    ASSERT_NE(texture, nullptr);

    const MgTextureHandles first = MgTextureLookup(texture);
    ASSERT_NE(first.palette, gfx::kNullTexture);
    EXPECT_EQ(PaletteAt(first.palette, 16)[0], 8);

    reinterpret_cast<uint8_t *>(clut.data())[8 * 4] = 0xAB;
    h_.manager.ReloadCLUT(texture, static_cast<sceVif1Packet *>(nullptr));
    const MgTextureHandles second = MgTextureLookup(texture);
    ASSERT_NE(second.palette, gfx::kNullTexture);
    EXPECT_EQ(PaletteAt(second.palette, 16)[0], 0xAB);

    Quads other = Pack(Clut8());
    reinterpret_cast<uint8_t *>(other.data())[8 * 4] = 0x55;
    texture->clut = other.data();
    h_.manager.ReloadCLUT(texture, static_cast<sceVif1Packet *>(nullptr));
    const MgTextureHandles third = MgTextureLookup(texture);
    ASSERT_NE(third.palette, gfx::kNullTexture);
    EXPECT_EQ(PaletteAt(third.palette, 16)[0], 0x55);
}

TEST_F(MgTextureGpu, ReloadTextureUploadsSwappedPixels) {
    Quads before = Pack(Ct32Pixels(4, 4));
    mgCTexture *texture = Enter(h_, 1, "swap", 4, 4, 32, {before.data()});
    ASSERT_NE(texture, nullptr);
    const Readback first = Read(MgTextureLookup(texture).texture);
    EXPECT_EQ(TexelOf(first, 0, 0), (Texel4{0, 0, 0x10, 0xFF}));

    std::vector<uint8_t> after_bytes(4 * 4 * 4);
    for (size_t t = 0; t < 16; t++) {
        after_bytes[t * 4] = 0x77;
        after_bytes[t * 4 + 1] = 0x33;
        after_bytes[t * 4 + 2] = 0x11;
        after_bytes[t * 4 + 3] = 0x80;
    }
    Quads after = Pack(after_bytes);
    texture->image[0] = after.data();
    h_.manager.ReloadTexture(1, static_cast<sceVif1Packet *>(nullptr));
    EXPECT_EQ(h_.manager.last_block, 1);

    const Readback second = Read(MgTextureLookup(texture).texture);
    EXPECT_EQ(TexelOf(second, 0, 0), (Texel4{0x77, 0x33, 0x11, 0xFF}));
    EXPECT_EQ(TexelOf(second, 3, 3), (Texel4{0x77, 0x33, 0x11, 0xFF}));
}

TEST_F(MgTextureGpu, Tex0LookupTakesPaletteByCbp) {
    Quads indices = Pack(std::vector<uint8_t>(16, 1));
    Quads clut_a = Pack(Clut8());
    Quads clut_b = Pack(std::vector<uint8_t>(256 * 4, 0x66));
    mgCTexture *a = Enter(h_, 0, "tex0_a", 4, 4, 8, {indices.data()}, clut_a.data());
    mgCTexture *b = Enter(h_, 0, "tex0_b", 4, 4, 8, {indices.data()}, clut_b.data());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    const gfx::TextureHandle a_image = MgTextureLookup(a).texture;
    const gfx::TextureHandle a_palette = MgTextureLookup(a).palette;
    const gfx::TextureHandle b_palette = MgTextureLookup(b).palette;
    ASSERT_NE(a_image, gfx::kNullTexture);
    ASSERT_NE(a_palette, gfx::kNullTexture);
    ASSERT_NE(b_palette, gfx::kNullTexture);
    ASSERT_NE(a_palette, b_palette);

    sceGsTex0 mixed;
    mixed.value = a->tex0_bits;
    mixed.CBP = b->tex0.CBP;
    const MgTextureHandles got = MgTextureLookupTex0(static_cast<uint64_t>(mixed.value));
    EXPECT_EQ(got.texture, a_image);
    EXPECT_EQ(got.palette, b_palette);
}

TEST_F(MgTextureGpu, NoImageTextureIsZeroedCanvas) {
    mgCTexture *texture = Enter(h_, 0, "blank", 64, 32, 32);
    ASSERT_NE(texture, nullptr);
    const MgTextureHandles handles = MgTextureLookup(texture);
    ASSERT_NE(handles.texture, gfx::kNullTexture);
    const std::optional<gfx::TextureInfo> info = gfx::GetTextureInfo(handles.texture);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->width, 64u);
    EXPECT_EQ(info->height, 32u);
    const Readback readback = Read(handles.texture);
    ASSERT_EQ(readback.pixels.size(), 64u * 32 * 4);
    EXPECT_TRUE(std::all_of(readback.pixels.begin(), readback.pixels.end(), [](uint8_t v) { return v == 0; }));
}

TEST_F(MgTextureGpu, InitializeReleasesEntries) {
    Quads px = Pack(Ct32Pixels(4, 4));
    mgCTexture *old = Enter(h_, 0, "old", 4, 4, 32, {px.data()});
    ASSERT_NE(old, nullptr);
    ASSERT_NE(MgTextureLookup(old).texture, gfx::kNullTexture);
    const uint32_t old_key = Tbp0(old);

    h_.manager.Initialize(0x1000, -1);
    EXPECT_EQ(MgTextureLookup(old).texture, gfx::kNullTexture);
    EXPECT_EQ(MgTextureLookup(old).palette, gfx::kNullTexture);

    mgCTexture *fresh = Enter(h_, 0, "fresh", 4, 4, 32, {px.data()});
    ASSERT_NE(fresh, nullptr);
    EXPECT_GE(Tbp0(fresh), kMgTextureKeyLast);
    EXPECT_LE(Tbp0(fresh), kMgTextureKeyFirst);
    EXPECT_TRUE(Tbp0(fresh) == old_key || Tbp0(fresh) == kMgTextureKeyFirst);
    EXPECT_NE(MgTextureLookup(fresh).texture, gfx::kNullTexture);
}

TEST_F(MgTextureGpu, DeleteBlockReleases) {
    Quads px = Pack(Ct32Pixels(4, 4));
    mgCTexture *doomed = Enter(h_, 3, "doomed", 4, 4, 32, {px.data()});
    mgCTexture *kept = Enter(h_, 0, "kept", 4, 4, 32, {px.data()});
    ASSERT_NE(doomed, nullptr);
    ASSERT_NE(kept, nullptr);
    ASSERT_NE(MgTextureLookup(doomed).texture, gfx::kNullTexture);
    ASSERT_NE(MgTextureLookup(kept).texture, gfx::kNullTexture);

    h_.manager.DeleteBlock(3);
    EXPECT_EQ(MgTextureLookup(doomed).texture, gfx::kNullTexture);
    EXPECT_EQ(h_.manager.GetTextureBlock(3)->texture, nullptr);
    EXPECT_NE(MgTextureLookup(kept).texture, gfx::kNullTexture);
}

TEST(MgTexture, TablesAboveFourGiB) {
    Harness h;
    const uintptr_t arena_lo = reinterpret_cast<uintptr_t>(h.memory.stack);
    if (arena_lo <= 0xFFFFFFFFu) {
        GTEST_SKIP() << "arena at 0x" << std::hex << arena_lo << " is below 4 GiB";
    }

    ASSERT_GT(reinterpret_cast<uintptr_t>(h.manager.blocks), 0xFFFFFFFFu);

    const uintptr_t arena_used = arena_lo + static_cast<uintptr_t>(h.memory.stack_used) * sizeof(u_long128);
    const struct {
        const char *what;
        uintptr_t   begin;
        uintptr_t   end;
    } regions[] = {
        {"blocks", reinterpret_cast<uintptr_t>(h.manager.blocks),
         reinterpret_cast<uintptr_t>(&h.manager.blocks[kBlockCount - 1] + 1)},
        {"texture_buf", reinterpret_cast<uintptr_t>(h.manager.texture_buf),
         reinterpret_cast<uintptr_t>(h.manager.texture_buf + kTextureCount)},
        {"texture_stack", reinterpret_cast<uintptr_t>(h.manager.texture_stack),
         reinterpret_cast<uintptr_t>(h.manager.texture_stack + kTextureCount)},
        {"hash_buf", reinterpret_cast<uintptr_t>(h.manager.hash_buf),
         reinterpret_cast<uintptr_t>(h.manager.hash_buf + kTextureCount)},
        {"hash_stack", reinterpret_cast<uintptr_t>(h.manager.hash_stack),
         reinterpret_cast<uintptr_t>(h.manager.hash_stack + kTextureCount)},
    };
    for (const auto &region : regions) {
        EXPECT_GE(region.begin, arena_lo) << region.what;
        EXPECT_LE(region.end, arena_used) << region.what;
    }

    std::vector<std::string> odd;
    std::vector<std::string> even;
    for (int i = 0; (odd.size() < 16 || even.size() < 16) && i < 1000; i++) {
        std::string name = "tex_" + std::to_string(i);
        std::vector<std::string> &list = (RetailBucket(name) & 1) != 0 ? odd : even;
        if (list.size() < 16) {
            list.push_back(name);
        }
    }
    ASSERT_EQ(odd.size(), 16u);
    ASSERT_EQ(even.size(), 16u);

    std::vector<std::string> names;
    for (size_t i = 0; i < 16; i++) {
        names.push_back(odd[i]);
        names.push_back(even[i]);
    }

    std::vector<int> buckets;
    for (const std::string &name : names) {
        std::string owned = name;
        EXPECT_EQ(h.manager.hash(owned.data()), RetailBucket(name)) << name;
        buckets.push_back(RetailBucket(name));
    }
    std::sort(buckets.begin(), buckets.end());
    ASSERT_LT(static_cast<size_t>(std::unique(buckets.begin(), buckets.end()) - buckets.begin()), names.size())
        << "no two names share a bucket, so chains are not exercised";

    auto block_of = [](size_t k) { return static_cast<int>(k % kBlockCount); };
    for (size_t k = 0; k < names.size(); k++) {
        ASSERT_NE(Enter(h, block_of(k), names[k], 4, 4, 32), nullptr) << names[k];
    }

    for (size_t k = 0; k < names.size(); k++) {
        const int block = block_of(k);
        std::string name = names[k];
        mgCTexture *found = h.manager.GetTexture(name.data(), block);
        ASSERT_NE(found, nullptr) << name << " in block " << block;
        EXPECT_STREQ(found->name, name.c_str());
        EXPECT_EQ(found->block, block);
        EXPECT_EQ(h.manager.GetTexture(name.data(), (block + 1) % kBlockCount), nullptr) << name;
        EXPECT_NE(h.manager.GetTexture(name.data(), -1), nullptr) << name;
    }

    h.manager.DeleteBlock(2);
    for (size_t k = 0; k < names.size(); k++) {
        std::string name = names[k];
        const int block = block_of(k);
        if (block == 2) {
            EXPECT_EQ(h.manager.GetTexture(name.data(), block), nullptr) << name;
        } else {
            EXPECT_NE(h.manager.GetTexture(name.data(), block), nullptr) << name;
        }
    }

    for (size_t k = 0; k < names.size(); k++) {
        if (block_of(k) != 2) {
            continue;
        }
        ASSERT_NE(Enter(h, 2, names[k], 4, 4, 32), nullptr) << names[k];
        std::string name = names[k];
        EXPECT_NE(h.manager.GetTexture(name.data(), 2), nullptr) << names[k];
    }
}
