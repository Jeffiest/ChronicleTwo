#pragma once

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include "mg_memory.hpp"

// The retail photo-name arena is 0x248 (584) 16-byte blocks. Host records
// grow from 8 bytes to sizeof(PIC_NAME_INFO); reserve only that delta while
// retaining the complete retail string and tail budget.
namespace port_photo_name_storage {

constexpr std::size_t kRetailBlocks = 0x248;
constexpr std::size_t kBlockBytes = 16;
// LoadFilePictureName reads neta2.lst into an aligned 0x5000-byte buffer.
constexpr int kMaxRecordsFromScriptBytes = 0x5000;

inline std::vector<u_long128> &backing_storage() {
    static std::vector<u_long128> storage;
    return storage;
}

[[noreturn]] inline void fail(const char *message, int count) {
    std::fprintf(stderr, "photo-name storage: %s (record count %d)\n", message, count);
    std::abort();
}

inline int checked_record_count(int count) {
    if (count < 0 || count > kMaxRecordsFromScriptBytes) {
        fail("PIC_INFO count exceeds the neta2.lst script-buffer limit", count);
    }
    return count;
}

inline void prepare(mgCMemory *arena, int count, std::size_t record_size) {
    checked_record_count(count);
    if (arena == nullptr || record_size < 8) {
        fail("invalid arena or host record size", count);
    }
    const std::size_t n = static_cast<std::size_t>(count);
    if (n != 0 && record_size > std::numeric_limits<std::size_t>::max() / n) {
        fail("record byte-size overflow", count);
    }
    const std::size_t record_bytes = n * record_size;
    const std::size_t record_blocks = record_bytes / kBlockBytes + (record_bytes % kBlockBytes != 0);
    if (record_blocks > static_cast<std::size_t>(std::numeric_limits<int>::max()) - 2) {
        fail("record allocation exceeds mgCMemory block limit", count);
    }
    const std::size_t delta = record_size - 8;
    if (n != 0 && delta > (std::numeric_limits<std::size_t>::max() - (kBlockBytes - 1)) / n) {
        fail("record-size arithmetic overflow", count);
    }
    const std::size_t extra_blocks = (n * delta + kBlockBytes - 1) / kBlockBytes;
    if (extra_blocks > static_cast<std::size_t>(std::numeric_limits<int>::max()) - kRetailBlocks) {
        fail("arena exceeds mgCMemory block limit", count);
    }
    const std::size_t blocks = kRetailBlocks + extra_blocks;
    auto &storage = backing_storage();
    try {
        storage.resize(blocks);
    } catch (...) {
        fail("required backing allocation failed", count);
    }
    std::snprintf(arena->name, sizeof(arena->name), "%s", "photo-names");
    arena->stSetBuffer(storage.data(), static_cast<int>(storage.size()));
}

inline u_long128 *allocate_records(mgCMemory *arena, int blocks, int count) {
    if (arena == nullptr || blocks < 0 || blocks > std::numeric_limits<int>::max() - 2) {
        fail("invalid required record allocation", count);
    }
    u_long128 *records = arena->Alloc(blocks + 2);
    if (records == nullptr) {
        fail("required PIC_INFO record allocation failed", count);
    }
    return records;
}

inline char *copy_required_name(char *source, mgCMemory *arena, int count) {
    char *name = mgCopyString(source, arena);
    if (name == nullptr) {
        fail("required PIC_NAME string allocation failed", count);
    }
    return name;
}

} // namespace port_photo_name_storage
