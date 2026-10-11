#include <cstdint>
#include <cstring>

#include "menucommon.hpp"
#include "mainloop.hpp"

// Retail aligns by rounding the pointer's 32-bit value; on the host the address is 64-bit, so
// the same round-to-64-bytes is done on the full value.
PORT_OVERRIDE u_long128 *MenuCalcBufAlignment(u_long128 *buffer) {
    std::uintptr_t size = reinterpret_cast<std::uintptr_t>(buffer);

    if ((size % 64) != 0) {
        size = ((size >> 6) + 1) << 6;
    }

    return reinterpret_cast<u_long128 *>(size);
}

namespace {

constexpr const char *kMenuLanguageDirectories[] = {"0/", "1/", "2/", "3/", "4/", "5/", "1/"};

bool BuildMenuPath(const char *name, char (&path)[0x8C]) {
    if (name == nullptr || *name == '\0' || LanguageCode < 0 ||
        LanguageCode >= static_cast<int>(sizeof(kMenuLanguageDirectories) / sizeof(kMenuLanguageDirectories[0]))) {
        return false;
    }

    constexpr char prefix[] = "menu/";
    const char *language = kMenuLanguageDirectories[LanguageCode];
    const size_t prefix_size = sizeof(prefix) - 1;
    const size_t language_size = std::strlen(language);
    const size_t name_size = std::strlen(name);
    if (name_size > sizeof(path) - 1 - prefix_size - language_size) {
        return false;
    }

    std::memcpy(path, prefix, prefix_size);
    std::memcpy(path + prefix_size, language, language_size);
    std::memcpy(path + prefix_size + language_size, name, name_size + 1);
    return true;
}

} // namespace

PORT_OVERRIDE int LoadFileMenu(char *name, u_long128 *buffer, int mode) {
    char path[0x8C];
    if (buffer == nullptr || !BuildMenuPath(name, path)) {
        return -1;
    }

    int size = 0;
    if (LoadFile2(path, nullptr, &size, LOAD_FILE_SIZE) == 0 || size < 0) {
        return -1;
    }

    if (mode == MENU_FILE_LOAD_BG) {
        return LoadFileBG(path, buffer, &size) ? size : -1;
    }
    if (mode == MENU_FILE_LOAD_DIRECT) {
        return LoadFile2(path, buffer, &size, LOAD_FILE_READ) ? size : -1;
    }
    return -1;
}

PORT_OVERRIDE int MenuCommonReadData(mgCMemory *memory, char **names, int mode) {
    if (memory == nullptr || names == nullptr || memory->stack == nullptr || memory->lock != 0 ||
        memory->stack_used < 0 || memory->stack_size < memory->stack_used ||
        (mode != MENU_FILE_LOAD_BG && mode != MENU_FILE_LOAD_DIRECT)) {
        return -1;
    }

    StartReadBG();
    int total = 0;
    memory->Align64();

    for (size_t index = 0; names[index] != nullptr; ++index) {
        char path[0x8C];
        if (!BuildMenuPath(names[index], path)) {
            return -1;
        }

        int size = 0;
        if (LoadFile2(path, nullptr, &size, LOAD_FILE_SIZE) == 0 || size < 0 ||
            size > INT32_MAX - total) {
            return -1;
        }

        const int blocks = size / 16 + (size % 16 != 0);
        if (memory->stack_used < 0 || memory->stack_size < memory->stack_used) {
            return -1;
        }
        const int available = memory->stack_size - memory->stack_used;
        if (blocks > 0 && blocks >= available) {
            return -1;
        }

        const int used_before_reserve = memory->stack_used;
        u_long128 *destination = blocks > 0 ? memory->Alloc(blocks) : memory->stack + memory->stack_used;
        if (destination == nullptr) {
            return -1;
        }

        const int loaded = LoadFileMenu(names[index], destination, mode);
        if (loaded < 0 || loaded != size) {
            memory->stack_used = used_before_reserve;
            return -1;
        }
        total += loaded;
        memory->Align64();
    }

    return total;
}
