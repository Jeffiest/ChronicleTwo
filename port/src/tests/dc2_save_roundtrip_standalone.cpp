#include <libmc.h>

#include "platform/paths.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#undef assert
#define assert(condition)                                                                                              \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "check failed: " #condition << " at " << __FILE__ << ':' << __LINE__ << '\n';              \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

namespace fs = std::filesystem;

namespace {
constexpr int kPort = 0;
constexpr int kOpenRead = 0x0001;
constexpr int kOpenWrite = 0x0002;
constexpr int kOpenCreate = 0x0200;
constexpr int kSaveBytes = 0x659c0;
constexpr int kPatchOffset = 0x45;

int Sync(int expected_function) {
    int command = -1;
    int result = -1;
    assert(sceMcSync(MC_WAIT, &command, &result) == 1);
    assert(command == expected_function);
    return result;
}

void Succeed(int command) { assert(Sync(command) == sceMcResSucceed); }

std::vector<std::uint8_t> Image() {
    std::vector<std::uint8_t> bytes(kSaveBytes);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>((i * 37u + (i >> 8u) + 0x5bu) & 0xffu);
    }
    return bytes;
}

void MakeDirectory(const char *path) {
    assert(sceMcMkdir(kPort, 0, path) == 0);
    Succeed(sceMcFuncNoMkdir);
}
} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() /
                          ("dc2_save_roundtrip_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    PathsSetSaveRoot(root);
    assert(sceMcInit() == sceMcIniSucceed);

    // These names come from CMemoryCardManager's game, album, and bonus flows.
    MakeDirectory("/BESCES-51190dkcl0");
    MakeDirectory("/BESCES-51190dc2album");
    MakeDirectory("/BESCES-51190dc2omake");

    const std::string save_path = "/BESCES-51190dkcl0/BESCES-51190dkcl0";
    const std::string album_path = "/BESCES-51190dc2album/BESCES-51190dc2album";
    const std::string omake_path = "/BESCES-51190dc2omake/BESCES-51190dc2omake";
    std::vector<std::uint8_t> expected = Image();
    expected[kPatchOffset] = 0x00; // Retail clears the incomplete marker at byte 0x45.

    assert(sceMcOpen(kPort, 0, save_path.c_str(), kOpenCreate | kOpenWrite) == 0);
    int fd = Sync(sceMcFuncNoOpen);
    assert(fd >= 0);
    assert(sceMcWrite(fd, expected.data(), static_cast<int>(expected.size())) == 0);
    assert(Sync(sceMcFuncNoWrite) == kSaveBytes);
    assert(sceMcFlush(fd) == 0);
    Succeed(sceMcFuncNoFlush);
    assert(sceMcClose(fd) == 0);
    Succeed(sceMcFuncNoClose);

    // The SDK's typed directory table is the game's stat operation.
    std::array<sceMcTblGetDir, 8> entries{};
    assert(sceMcGetDir(kPort, 0, "/BESCES-51190dkcl0/*", 0, static_cast<int>(entries.size()), entries.data()) == 0);
    assert(Sync(sceMcFuncNoGetDir) == 3); // The SDK-facing host adapter includes . and .. entries.
    bool found = false;
    for (const auto &entry : entries) {
        if (std::string(entry.entry_name) == "BESCES-51190dkcl0") {
            assert(entry.file_size_byte == kSaveBytes);
            found = true;
        }
    }
    assert(found);

    assert(sceMcOpen(kPort, 0, save_path.c_str(), kOpenRead) == 0);
    fd = Sync(sceMcFuncNoOpen);
    assert(fd >= 0);
    assert(sceMcSeek(fd, kPatchOffset, 0) == 0);
    assert(Sync(sceMcFuncNoSeek) == kPatchOffset);
    std::uint8_t marker = 0xff;
    assert(sceMcRead(fd, &marker, 1) == 0);
    assert(Sync(sceMcFuncNoRead) == 1);
    assert(marker == 0x00);
    assert(sceMcSeek(fd, 0, 0) == 0);
    assert(Sync(sceMcFuncNoSeek) == 0);
    std::vector<std::uint8_t> actual(kSaveBytes);
    assert(sceMcRead(fd, actual.data(), static_cast<int>(actual.size())) == 0);
    assert(Sync(sceMcFuncNoRead) == kSaveBytes);
    assert(actual == expected);
    assert(sceMcClose(fd) == 0);
    Succeed(sceMcFuncNoClose);

    // Confirm the album and bonus file naming contract through SDK calls too.
    for (const std::string *path : {&album_path, &omake_path}) {
        assert(sceMcOpen(kPort, 0, path->c_str(), kOpenCreate | kOpenWrite) == 0);
        fd = Sync(sceMcFuncNoOpen);
        assert(fd >= 0);
        const std::uint8_t byte = 0;
        assert(sceMcWrite(fd, const_cast<std::uint8_t *>(&byte), 1) == 0);
        assert(Sync(sceMcFuncNoWrite) == 1);
        assert(sceMcClose(fd) == 0);
        Succeed(sceMcFuncNoClose);
    }

    std::string mutable_path = save_path;
    assert(sceMcDelete(kPort, 0, mutable_path.data()) == 0);
    Succeed(sceMcFuncNoDelete);
    assert(!fs::exists(root / "mc0/BESCES-51190dkcl0/BESCES-51190dkcl0"));
    assert(fs::exists(root / "mc0/BESCES-51190dc2album/BESCES-51190dc2album"));
    assert(fs::exists(root / "mc0/BESCES-51190dc2omake/BESCES-51190dc2omake"));
    assert(sceMcEnd() == sceMcIniSucceed);

    fs::remove_all(root);
    std::cout << "DC2 SDK save round trip passed: " << kSaveBytes
              << " bytes, typed stat, offset 0x45, album/omake paths, delete, isolated root\n";
}
