#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "menucommon.hpp"

int LanguageCode = 1;

namespace {

struct File {
    const char *path;
    std::array<unsigned char, 31> bytes;
    int size;
};

File files[] = {{"menu/1/first.bin", {}, 17}, {"menu/1/second.bin", {}, 31},
                {"menu/1/empty.bin", {}, 0}};
int start_read_count = 0;
int bg_read_count = 0;
int file_read_count = 0;

void ResetFiles() {
    for (size_t f = 0; f < sizeof(files) / sizeof(files[0]); ++f) {
        for (size_t i = 0; i < files[f].bytes.size(); ++i) {
            files[f].bytes[i] = static_cast<unsigned char>(0x20 + f * 0x30 + i);
        }
    }
}

bool Check(bool condition, const char *message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

} // namespace

void mgCMemory::Init() {
    heap_size = 0;
    heap = nullptr;
    heap_top = nullptr;
    name[0] = '\0';
    lock = 0;
    stack = nullptr;
    stack_used = 0;
    stack_size = 0;
    stack_block = nullptr;
}

void StartReadBG() { ++start_read_count; }

int LoadFile2(char *path, void *buffer, int *out_size, int mode) {
    if (out_size) *out_size = 0;
    for (const File &file : files) {
        if (std::strcmp(path, file.path) != 0) continue;
        if (out_size) *out_size = file.size;
        if (mode != LOAD_FILE_SIZE && file.size != 0) {
            if (buffer == nullptr) return 0;
            ++file_read_count;
            std::memcpy(buffer, file.bytes.data(), static_cast<size_t>(file.size));
        }
        return 1;
    }
    return 0;
}

int LoadFileBG(char *path, u_long128 *buffer, int *out_size) {
    ++bg_read_count;
    return LoadFile2(path, buffer, out_size, LOAD_FILE_READ);
}

int main(int argc, char **argv) {
    ResetFiles();
    if (argc != 2) return 2;
    const char *case_name = argv[1];
    bool ok = true;

    if (std::strcmp(case_name, "typed-names-and-content") == 0 ||
        std::strcmp(case_name, "background-read") == 0) {
        alignas(64) std::array<u_long128, 12> storage{};
        mgCMemory memory;
        memory.stack = storage.data();
        memory.stack_size = static_cast<int>(storage.size());
        char first[] = "first.bin";
        char second[] = "second.bin";
        char *names[] = {first, second, nullptr};
        int mode = std::strcmp(case_name, "background-read") == 0 ? MENU_FILE_LOAD_BG : MENU_FILE_LOAD_DIRECT;
        ok &= Check(MenuCommonReadData(&memory, names, mode) == 48, "both file byte sizes are returned");
        ok &= Check(memory.stack_used == 8, "both files reserve aligned quadword storage");
        ok &= Check(std::memcmp(storage.data(), files[0].bytes.data(), 17) == 0,
                    "first file bytes are present");
        ok &= Check(std::memcmp(reinterpret_cast<unsigned char *>(storage.data()) + 64,
                                files[1].bytes.data(), 31) == 0,
                    "second filename was read at native pointer width and its bytes are present");
        if (mode == MENU_FILE_LOAD_BG) ok &= Check(bg_read_count == 2, "background reads use host synchronous path");
    } else if (std::strcmp(case_name, "invalid-missing") == 0) {
        alignas(64) std::array<u_long128, 8> storage{};
        mgCMemory memory;
        memory.stack = storage.data();
        memory.stack_size = static_cast<int>(storage.size());
        char empty[] = "";
        char *invalid[] = {empty, nullptr};
        char missing[] = "missing.bin";
        char *absent[] = {missing, nullptr};
        ok &= Check(MenuCommonReadData(&memory, invalid, MENU_FILE_LOAD_DIRECT) == -1,
                    "empty filename fails");
        ok &= Check(MenuCommonReadData(&memory, absent, MENU_FILE_LOAD_DIRECT) == -1,
                    "missing file fails");
        ok &= Check(LoadFileMenu(nullptr, storage.data(), MENU_FILE_LOAD_DIRECT) == -1,
                    "null filename fails");
        char empty_file[] = "empty.bin";
        ok &= Check(LoadFileMenu(empty_file, storage.data(), MENU_FILE_LOAD_DIRECT) == 0,
                    "existing empty file succeeds with size zero");
        ok &= Check(LoadFileMenu(empty_file, storage.data(), 99) == -1,
                    "unsupported load mode fails");
        int used_before_unsupported_mode = memory.stack_used;
        char valid_names[] = "first.bin";
        char *valid_list[] = {valid_names, nullptr};
        ok &= Check(MenuCommonReadData(&memory, valid_list, 99) == -1 &&
                        memory.stack_used == used_before_unsupported_mode,
                    "unsupported mode fails before reserving storage");
        LanguageCode = 7;
        char valid[] = "first.bin";
        ok &= Check(LoadFileMenu(valid, storage.data(), MENU_FILE_LOAD_DIRECT) == -1,
                    "invalid language fails");
        LanguageCode = 1;
    } else if (std::strcmp(case_name, "insufficient-capacity") == 0) {
        alignas(64) std::array<u_long128, 2> storage{};
        mgCMemory memory;
        memory.stack = storage.data();
        memory.stack_size = static_cast<int>(storage.size());
        char first[] = "first.bin";
        char *names[] = {first, nullptr};
        ok &= Check(MenuCommonReadData(&memory, names, MENU_FILE_LOAD_DIRECT) == -1,
                    "capacity failure is reported");
        ok &= Check(memory.stack_used == 0, "capacity is checked before reading or reserving");
        ok &= Check(file_read_count == 0, "insufficient capacity does not read file contents");
    } else {
        return 2;
    }

    if (ok) std::printf("PASS: %s\n", case_name);
    return ok ? 0 : 1;
}
