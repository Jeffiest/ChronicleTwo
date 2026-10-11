#pragma once

// The game's files as the host sees them: the extracted disc tree, where DATA.DAT holds the
// files DATA.HD4 indexes and every other name (MOVIE/*.PSS above all) is a plain file beside it.
// dataread.cpp builds and serves the index; port/src/sce uses the same lookups so the SDK file
// calls resolve the names the game passes them.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace cdvd {

constexpr std::uint64_t kSector = 2048;

// One game file on the host: a whole plain file, or a byte range of DATA.DAT (the sector and
// size a DATA.HD4 record gives it).
struct File {
    std::filesystem::path path;
    std::uint64_t         offset = 0;
    std::uint64_t         size = 0;
    bool                  packed = false; // an extent of DATA.DAT, which the drive read whole sectors
};

// Reads DATA.HD4 and maps the data directory (PathsDataRoot()); InitCDFile calls this, and a
// second call does nothing. Aborts, naming the directory, when there is no game data at all.
void LoadIndex();

// Resolves a game path: drops a "device:" prefix and an ISO ";1" version, folds case and stores
// '\' as '/', then looks the name up in DATA.HD4 and in the tree. False when nothing holds it.
bool Find(std::string_view path, File &out);

// Reads size bytes at offset from the host file a File names (offset 0 is the file's start);
// false when the range lies outside the file or the read comes up short.
bool ReadAt(const File &file, std::uint64_t offset, void *buffer, std::uint64_t size);

// The folded key the lookups use: lower case, '\' as '/', no leading separator, no device prefix
// and no ";1".
std::string Fold(std::string_view path);

} // namespace cdvd
