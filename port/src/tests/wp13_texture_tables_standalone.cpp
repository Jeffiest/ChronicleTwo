#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "mg_memory.hpp"
#include "mg_texture.hpp"

int main() {
    constexpr int count = 350;
    constexpr int block_count = 221;
    std::vector<u_long128> storage(16384);
    mgCMemory memory;
    memory.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
    mgCTextureManager manager;
    manager.SetTableBuffer(count, block_count, &memory);
    struct Span { const void *base; std::size_t size; };
    Span spans[] = {{manager.blocks, block_count * sizeof(mgCTextureBlock)},
                    {manager.texture_buf, count * sizeof(mgCTexture)},
                    {manager.texture_stack, count * sizeof(mgCTexture *)},
                    {manager.hash_buf, count * sizeof(mgTEXTURE_HASH)},
                    {manager.hash_stack, count * sizeof(mgTEXTURE_HASH *)}};
    // stSetBuffer promotes external storage into an arena-owned root so that
    // allocations remain valid for the full port lifetime. Check the buffer
    // actually used by mgCMemory, not the caller's seed vector.
    const auto begin = reinterpret_cast<std::uintptr_t>(memory.stack);
    const auto end = begin + memory.stack_used * 16;
    for (int i = 0; i < 5; ++i) {
        const auto lo = reinterpret_cast<std::uintptr_t>(spans[i].base), hi = lo + spans[i].size;
        if (lo < begin || hi > end) {
            std::fprintf(stderr, "texture table %d exceeds its arena allocation\n", i);
            return 1;
        }
        for (int j = 0; j < i; ++j) {
            const auto other = reinterpret_cast<std::uintptr_t>(spans[j].base);
            if (lo < other + spans[j].size && other < hi) {
                std::fprintf(stderr, "texture tables %d and %d overlap\n", i, j);
                return 1;
            }
        }
    }
    auto *following = memory.Alloc(512);
    assert(following);
    std::memset(following, 0xA5, 512 * 16);
    for (int reload = 0; reload < 8; ++reload) {
        manager.Initialize(0, -1);
        for (int i = 0; i < count; ++i) {
            assert(manager.texture_stack[i] == &manager.texture_buf[i]);
            auto *texture = &manager.texture_buf[i];
            std::snprintf(texture->name, sizeof(texture->name), "texture-%03d", i);
            texture->block = i % block_count;
            manager.AddHash(texture);
        }
        assert(manager.hash_num == count);
        manager.AddHash(&manager.texture_buf[0]);
        assert(manager.hash_num == count);
        for (int i = 0; i < count; ++i) {
            auto *texture = &manager.texture_buf[i];
            assert(manager.SearchHash(texture->name, texture->block) == texture);
        }
        for (int i = count - 1; i >= 0; --i) manager.DelHash(&manager.texture_buf[i]);
        assert(manager.hash_num == 0);
        for (int i = 0; i < count; ++i)
            assert(manager.SearchHash(manager.texture_buf[i].name, -1) == nullptr);
        for (int i = 0; i < 512 * 16; ++i)
            assert(reinterpret_cast<unsigned char *>(following)[i] == 0xA5);
    }
    std::puts("350 host-sized texture/hash records: disjoint extents and eight full reloads pass");
}
