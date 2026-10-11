#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mg_memory.hpp"
#include "photo_name_storage.hpp"
#include "inventmn.hpp"
#include "mainloop.hpp"
#include "dataread.hpp"
#include "platform/paths.hpp"

using HostPhotoNameInfo = PIC_NAME_INFO;

static int Fail(const char *message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1], "local-pal") == 0) {
        PathsSetDataRoot(PathsFromUtf8(argv[2]));
        LanguageCode = LANG_ENGLISH;
        InitCDFile();
        LoadFilePictureName();
        auto &storage = port_photo_name_storage::backing_storage();
        if (storage.size() != 731) return Fail("PAL record capacity differs from measured 294-record layout");
        const auto begin = reinterpret_cast<std::uintptr_t>(storage.data());
        const auto end = begin + storage.size() * sizeof(u_long128);
        auto *records = reinterpret_cast<PIC_NAME_INFO *>(storage.data());
        int found = 0;
        for (int i = 0; i < 294; ++i) {
            if (records[i].name == nullptr) continue;
            const auto address = reinterpret_cast<std::uintptr_t>(records[i].name);
            if (address < begin || address >= end ||
                std::memchr(records[i].name, 0, end - address) == nullptr)
                return Fail("PAL name points outside persistent storage or is unterminated");
            USER_PICTURE_INFO info{};
            info.used = true;
            info.neta_id = records[i].neta_id;
            info.map_no = info.npc_no = info.monster_no = -1;
            char *name = GetPhotoName(&info);
            if (name == nullptr || std::strcmp(name, records[i].name) != 0)
                return Fail("PAL name lookup differs from loaded record");
            ++found;
        }
        if (found != 294) return Fail("PAL name loader did not populate all 294 subject records");
        std::printf("local PAL photo-name loader passed (%d stored names)\n", found);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "negative-count") == 0)
        return port_photo_name_storage::checked_record_count(-1);
    if (argc == 2 && std::strcmp(argv[1], "oversized-count") == 0)
        return port_photo_name_storage::checked_record_count(port_photo_name_storage::kMaxRecordsFromScriptBytes + 1);
    if (argc == 2 && std::strcmp(argv[1], "arithmetic-overflow") == 0) {
        mgCMemory invalid;
        port_photo_name_storage::prepare(&invalid, 2, static_cast<std::size_t>(-1));
    }
    if (argc == 2 && std::strcmp(argv[1], "required-allocation-failure") == 0) {
        mgCMemory undersized;
        u_long128 storage[3]{};
        undersized.stSetBuffer(storage, 3);
        port_photo_name_storage::allocate_records(&undersized, 1, 1);
    }
    if (argc == 2 && std::strcmp(argv[1], "required-name-failure") == 0) {
        mgCMemory undersized;
        u_long128 storage[1]{};
        undersized.stSetBuffer(storage, 1);
        char name[] = "Name";
        port_photo_name_storage::copy_required_name(name, &undersized, 1);
    }
    static_assert(sizeof(HostPhotoNameInfo) > 8, "host record must include expanded pointer");
    mgCMemory arena;
    port_photo_name_storage::prepare(&arena, 294, sizeof(HostPhotoNameInfo));
    auto &storage = port_photo_name_storage::backing_storage();
    if (arena.stack_size != 584 + ((294 * (sizeof(HostPhotoNameInfo) - 8) + 15) / 16))
        return Fail("capacity does not preserve retail budget plus host record growth");
    if (std::strcmp(arena.name, "photo-names") != 0)
        return Fail("arena has no descriptive owner name");

    auto *tail_words = reinterpret_cast<std::uint64_t *>(&storage.back());
    tail_words[0] = 0xA55AA55AA55AA55AULL;
    const int record_blocks = static_cast<int>((294 * sizeof(HostPhotoNameInfo) + 15) / 16);
    auto *records = reinterpret_cast<HostPhotoNameInfo *>(port_photo_name_storage::allocate_records(
        &arena, record_blocks, 294));
    if (records == nullptr) return Fail("typed record allocation failed");
    for (int i = 0; i < 294; ++i) {
        records[i].neta_id = static_cast<unsigned short>(i);
        records[i].sort_key = static_cast<short>(i);
        char text[16];
        std::snprintf(text, sizeof(text), "Name %03d", i);
        records[i].name = port_photo_name_storage::copy_required_name(text, &arena, 294);
    }
    if (records[293].neta_id != 293 || std::strcmp(records[293].name, "Name 293") != 0)
        return Fail("last synthetic record was corrupted");
    if (tail_words[0] != 0xA55AA55AA55AA55AULL)
        return Fail("allocation overwrote the tail canary");

    auto *old_owner = records[0].name;
    mgCMemory next_load;
    port_photo_name_storage::prepare(&next_load, 294, sizeof(HostPhotoNameInfo));
    if (std::strcmp(old_owner, "Name 000") != 0)
        return Fail("backing storage did not remain owned after loader return");
    if (next_load.stack != storage.data() || next_load.stack_used != 0)
        return Fail("repeated load did not rebind and reset the arena");
    std::puts("photo-name storage standalone passed");
    return 0;
}
