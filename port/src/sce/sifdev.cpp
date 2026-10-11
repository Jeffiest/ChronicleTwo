#include <fcntl.h>
#include <sifdev.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>

#include "platform/paths.hpp"

// The two file servers the development kit reached through device prefixes live on host
// directories: host0: is the writable scratch below <save>/host0/ (edit.cpp's debug.txt and the
// TGA frames StoreImage dumps), and every other name - the cdrom0: tree included - is a game file
// served from the data directory. Disc names carry a ";1" version suffix and the extracted tree
// does not keep the disc's case, so the suffix is dropped and lookups fold case. Errors are
// negative errno values, as the IOP's file manager returned them.

namespace fs = std::filesystem;

namespace {

constexpr int kAccessMask = 0x0003;
constexpr int kReadWrite = 0x0003;
constexpr int kAppend = 0x0100;
constexpr int kExclusive = 0x0800;

std::unordered_set<int> g_open;

// Splits "device:name" into the host root it belongs to and the path below it.
bool SplitPath(const char *name, fs::path &root, std::string &relative) {
    if (name == nullptr) {
        return false;
    }
    std::string_view path = name;
    std::string_view device;
    if (std::size_t colon = path.find(':'); colon != std::string_view::npos) {
        device = path.substr(0, colon);
        path = path.substr(colon + 1);
    }
    root = device == "host0" ? PathsSaveRoot() / "host0" : PathsDataRoot();
    relative.assign(path);
    std::ranges::replace(relative, '\\', '/');
    if (std::size_t version = relative.rfind(';'); version != std::string::npos) {
        relative.erase(version);
    }
    relative.erase(0, relative.find_first_not_of('/'));
    fs::path normal = fs::path(relative).lexically_normal();
    if (relative.empty() || normal.empty() || *normal.begin() == ".." || normal.is_absolute()) {
        return false;
    }
    relative = normal.generic_string();
    return true;
}

// The entry of a directory whose name matches case-insensitively, the way the game's own lookups
// fold case. The exact name is tried first, so a tree already in a matching case is not walked.
bool FindIn(const fs::path &dir, const std::string &name, fs::path &out) {
    std::error_code error;
    fs::path        exact = dir / name;
    if (fs::exists(exact, error)) {
        out = exact;
        return true;
    }
    for (const fs::directory_entry &entry : fs::directory_iterator(dir, error)) {
        std::string found = entry.path().filename().string();
        if (found.size() == name.size() &&
            std::equal(found.begin(), found.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            })) {
            out = entry.path();
            return true;
        }
    }
    return false;
}

// Walks the path below the root, folding case at every component.
bool Resolve(const fs::path &root, const std::string &relative, fs::path &out) {
    fs::path    current = root;
    std::size_t start = 0;
    while (start < relative.size()) {
        std::size_t slash = relative.find('/', start);
        std::string component = relative.substr(start, slash - start);
        start = slash == std::string::npos ? relative.size() : slash + 1;
        if (!FindIn(current, component, current)) {
            return false;
        }
    }
    out = current;
    return true;
}

int HostFlags(int flags) {
    int host;
    switch (flags & kAccessMask) {
        case SCE_WRONLY:
            host = O_WRONLY;
            break;
        case kReadWrite:
            host = O_RDWR;
            break;
        default:
            host = O_RDONLY;
            break;
    }
    if (flags & kAppend) {
        host |= O_APPEND;
    }
    if (flags & SCE_CREAT) {
        host |= O_CREAT;
    }
    if (flags & SCE_TRUNC) {
        host |= O_TRUNC;
    }
    if (flags & kExclusive) {
        host |= O_EXCL;
    }
    return host | O_CLOEXEC;
}

bool Known(int fd) {
    return g_open.contains(fd);
}

} // namespace

void sceFsReset() {}

extern "C" int sceOpen(const char *name, int flags, ...) {
    fs::path    root;
    std::string relative;
    if (!SplitPath(name, root, relative)) {
        return -ENOENT;
    }
    fs::path path;
    if (flags & SCE_CREAT) {
        path = root / relative;
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
    } else if (!Resolve(root, relative, path)) {
        return -ENOENT;
    }
    int fd = ::open(path.c_str(), HostFlags(flags), 0644);
    if (fd < 0) {
        return -errno;
    }
    g_open.insert(fd);
    return fd;
}

int sceClose(int fd) {
    if (!Known(fd)) {
        return -EBADF;
    }
    g_open.erase(fd);
    return ::close(fd) < 0 ? -errno : 0;
}

int sceLseek(int fd, int offset, int whence) {
    if (!Known(fd)) {
        return -EBADF;
    }
    off_t result = ::lseek(fd, offset, whence);
    return result < 0 ? -errno : static_cast<int>(result);
}

int sceRead(int fd, void *buffer, int size) {
    if (!Known(fd)) {
        return -EBADF;
    }
    ssize_t result = ::read(fd, buffer, static_cast<std::size_t>(std::max(size, 0)));
    return result < 0 ? -errno : static_cast<int>(result);
}

int sceWrite(int fd, void *buffer, int size) {
    if (!Known(fd)) {
        return -EBADF;
    }
    ssize_t result = ::write(fd, buffer, static_cast<std::size_t>(std::max(size, 0)));
    return result < 0 ? -errno : static_cast<int>(result);
}

int sceIoctl(int fd, int request, void *argument) {
    if (!Known(fd)) {
        return -EBADF;
    }
    if (request == SCE_FS_EXECUTING && argument != nullptr) {
        // Every host operation runs to completion inside its own call, so none is still going.
        *(int *) argument = 0;
    }
    return 0;
}

int sceGetstat(const char *name, struct sce_stat *stat) {
    fs::path    root;
    std::string relative;
    fs::path    path;
    if (stat == nullptr || !SplitPath(name, root, relative) || !Resolve(root, relative, path)) {
        return -ENOENT;
    }
    std::error_code error;
    bool            directory = fs::is_directory(path, error);
    std::uintmax_t  size = 0;
    if (!directory) {
        size = fs::file_size(path, error);
        if (error) {
            return -EIO;
        }
    }
    *stat = {};
    stat->st_mode = directory ? 0x1000u : 0x2000u;
    stat->st_size = static_cast<unsigned int>(size);
    stat->st_hisize = static_cast<unsigned int>(size >> 32);
    return 0;
}
