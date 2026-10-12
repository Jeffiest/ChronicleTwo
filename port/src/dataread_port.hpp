#pragma once

#include <filesystem>
#include <optional>

// Rebuilds the index of the extracted data under root, the files that replace DATA.HD4. Each entry is a
// regular file keyed by its lower-cased path below root, as it sits on disk; lookups percent-encode
// bytes >= 0x80 and '%' the way dc2data's DiskName does before they reach the index. Root-level
// manifest.txt, dot entries, *.dc2tmp files and the movie/ and sound/ trees are left out. Logs its
// count to stderr.
void DataReadIndex(const std::filesystem::path &root);

// The file a game path names, through the same device, current-directory and version rules the
// loaders use; empty when the index holds no such file.
std::optional<std::filesystem::path> DataReadFind(const char *game_name);
