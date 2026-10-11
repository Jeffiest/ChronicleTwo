#include "dataread.hpp"

#include <eekernel.h>
#include <sifdev.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cdvd_file.hpp"
#include "platform/paths.hpp"

// The disc is the extracted tree on the host. DATA.DAT holds the files DATA.HD4 indexes, and the
// names beside it (MOVIE/*.PSS above all) are plain files. This unit serves that tree through the
// same calls the drive had, so the rest of the game sees no change.
//
// Host reads are synchronous: a background read is finished before LoadFileBG returns, so the
// queue only ever holds completed entries and ReadBG has nothing left to do per vertical sync.

namespace fs = std::filesystem;

namespace {

char TopDir[256] = "";
char CurrentDir[256] = "";
int  DefaultFileDev = FILE_DEV_CDROM;

BG_READ_INFO bg_read_info[BG_READ_MAX];

FILE_CACHE file_cache[FILE_CACHE_MAX];
u_long128 *CacheAddress;
u_long128 *NowCacheAddress;
int        FileCacheType;

// Folded name -> extent of DATA.DAT and folded name -> tree file; the first record of a name
// wins, as the game's linear search finds it.
std::unordered_map<std::string, cdvd::File> packed_files;
std::unordered_map<std::string, cdvd::File> plain_files;
bool                                       index_loaded;

u_int Le32(const unsigned char *bytes) {
    return (u_int) bytes[0] | (u_int) bytes[1] << 8 | (u_int) bytes[2] << 16 | (u_int) bytes[3] << 24;
}

u_int align_size(u_int size, u_int alignment) {
    u_int rest = size % alignment;

    if (rest != 0) {
        size += alignment - rest;
    }

    return size;
}

// A path the game names for a write must stay under the save directory: a component that climbs,
// is empty, or holds a device colon is refused.
bool SafeRelative(std::string_view path) {
    if (path.empty() || path.back() == '/') {
        return false;
    }
    std::size_t start = 0;

    while (start <= path.size()) {
        std::size_t      end = std::min(path.find('/', start), path.size());
        std::string_view component = path.substr(start, end - start);

        if (component.empty() || component == "." || component == ".." ||
            component.find(':') != std::string_view::npos) {
            return false;
        }

        start = end + 1;
    }

    return true;
}

[[noreturn]] void NoData(const fs::path &root, const char *why) {
    std::fprintf(stderr,
                 "no game data: %s %s; point --data (or DC_DATA) at the extracted disc, the "
                 "directory holding DATA.HD4 and DATA.DAT\n",
                 root.c_str(), why);
    std::abort();
}

// The drive read whole sectors into the caller's buffer, so the bytes between a file's end and
// the sector boundary were overwritten too; they are zeroed rather than left as the caller's.
bool ReadWhole(const cdvd::File &file, void *buffer) {
    if (!cdvd::ReadAt(file, 0, buffer, file.size)) {
        std::fprintf(stderr, "cannot read %s\n", file.path.c_str());
        return false;
    }

    if (file.packed) {
        std::uint64_t padded = cdvd::kSector * ((file.size + cdvd::kSector - 1) / cdvd::kSector);

        if (padded > file.size) {
            std::memset(static_cast<char *>(buffer) + file.size, 0, padded - file.size);
        }
    }

    return true;
}

FILE_CACHE *CacheFind(char *path) {
    FILE_CACHE *entry;

    if (CacheAddress == NULL) {
        return NULL;
    }

    entry = file_cache;

    for (int i = 0; i < FILE_CACHE_MAX; i++, entry++) {
        if (entry->address != NULL && strcasecmp(entry->name, path) == 0) {
            return entry;
        }
    }

    return NULL;
}

int EntryFileCache(char *path, u_long128 *address, int size) {
    FILE_CACHE *entry = file_cache;

    for (int i = 0; i < FILE_CACHE_MAX; i++, entry++) {
        if (!entry->address) {
            entry->address = address;
            entry->size = size;
            entry->ref_count = 1;
            std::snprintf(entry->name, sizeof entry->name, "%s", path);

            return true;
        }
    }

    return false;
}

// DATA.HD4 as PAL's InitCDFile reads it: 12-byte records (name offset, size, sector) followed by
// the names, which the first record's name offset counts. The sectors are relative to DATA.DAT,
// and the index ends with one all-zero record.
void ReadIndex(const fs::path &hd4_path, const fs::path &dat_path, const fs::path &root) {
    std::error_code error;
    std::uintmax_t  hd4_size = fs::file_size(hd4_path, error);
    std::uintmax_t  dat_size = fs::file_size(dat_path, error);

    if (error || hd4_size < 12) {
        NoData(root, "has no usable DATA.HD4");
    }

    if (error || dat_size == 0) {
        NoData(root, "has no readable DATA.DAT");
    }

    std::vector<unsigned char> raw(static_cast<std::size_t>(hd4_size));
    std::ifstream stream(hd4_path, std::ios::binary);
    stream.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size()));

    if (static_cast<std::size_t>(stream.gcount()) != raw.size()) {
        NoData(root, "has a short DATA.HD4");
    }

    u_int table = Le32(raw.data());

    if (table % 12 != 0 || table > raw.size() || table < 12) {
        NoData(root, "has a malformed DATA.HD4 record table");
    }

    std::size_t   count = table / 12;
    std::uint32_t past_end = 0;

    for (std::size_t i = 0; i < count; i++) {
        const unsigned char *record = raw.data() + i * 12;
        u_int                name_offset = Le32(record);
        u_int                file_size = Le32(record + 4);
        u_int                sector = Le32(record + 8);

        // The index ends with an all-zero record; an empty name ends it too.
        if (name_offset == 0 && file_size == 0 && sector == 0) {
            break;
        }

        if (name_offset < table || name_offset >= raw.size()) {
            NoData(root, "has a DATA.HD4 record that points outside its name table");
        }

        const char *name = reinterpret_cast<const char *>(raw.data() + name_offset);
        const void *end = std::memchr(name, 0, raw.size() - name_offset);

        if (end == nullptr || end == name) {
            break;
        }

        if (static_cast<std::uint64_t>(sector) * cdvd::kSector + file_size > dat_size) {
            past_end++;
        }

        cdvd::File file{dat_path, static_cast<std::uint64_t>(sector) * cdvd::kSector, file_size, true};
        packed_files.emplace(cdvd::Fold(std::string_view(name, static_cast<const char *>(end) - name)), file);
    }

    if (past_end != 0) {
        std::fprintf(stderr, "%s: DATA.HD4 lists %u files past the end of DATA.DAT\n", root.c_str(),
                     past_end);
    }
}

} // namespace

namespace cdvd {

std::string Fold(std::string_view path) {
    std::string_view body = path;
    std::size_t      colon = body.find(':');
    std::size_t      separator = body.find_first_of("/\\");

    if (colon != std::string_view::npos &&
        (separator == std::string_view::npos || colon < separator)) {
        body = body.substr(colon + 1);
    }

    if (std::size_t version = body.find(';'); version != std::string_view::npos) {
        body = body.substr(0, version);
    }

    std::string folded;
    folded.reserve(body.size());

    for (char c : body) {
        folded.push_back(c == '\\' ? '/' : (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c));
    }

    folded.erase(0, folded.find_first_not_of('/'));
    return folded;
}

void LoadIndex() {
    if (index_loaded) {
        return;
    }

    index_loaded = true;
    const fs::path &root = PathsDataRoot();
    std::error_code error;

    if (!fs::is_directory(root, error)) {
        NoData(root, "is not a directory");
    }

    auto options = fs::directory_options::follow_directory_symlink |
                   fs::directory_options::skip_permission_denied;

    for (fs::recursive_directory_iterator it(root, options, error), end; !error && it != end;
         it.increment(error)) {
        std::error_code entry_error;

        if (it->is_regular_file(entry_error)) {
            std::uintmax_t size = it->file_size(entry_error);

            if (!entry_error) {
                std::string relative = it->path().lexically_relative(root).generic_string();
                plain_files.emplace(Fold(relative), File{it->path(), 0, size, false});
            }
        }
    }

    if (error) {
        std::fprintf(stderr, "%s: %s\n", root.c_str(), error.message().c_str());
    }

    if (plain_files.empty()) {
        NoData(root, "is empty");
    }

    // The index is read when both files are there; a tree whose files were already unpacked only
    // needs the files themselves, which the scan above has collected.
    auto hd4 = plain_files.find("data.hd4");
    auto dat = plain_files.find("data.dat");

    if (hd4 != plain_files.end() && dat != plain_files.end()) {
        ReadIndex(hd4->second.path, dat->second.path, root);
    }
}

bool Find(std::string_view path, File &out) {
    LoadIndex();
    std::string key = Fold(path);

    if (key.empty()) {
        return false;
    }

    auto packed = packed_files.find(key);

    if (packed != packed_files.end()) {
        out = packed->second;
        return true;
    }

    auto plain = plain_files.find(key);

    if (plain != plain_files.end()) {
        out = plain->second;
        return true;
    }

    return false;
}

bool ReadAt(const File &file, std::uint64_t offset, void *buffer, std::uint64_t size) {
    if (offset > file.size || size > file.size - offset) {
        return false;
    }

    std::ifstream stream(file.path, std::ios::binary);

    if (!stream) {
        return false;
    }

    stream.seekg(static_cast<std::streamoff>(file.offset + offset));
    stream.read(static_cast<char *>(buffer), static_cast<std::streamsize>(size));
    return static_cast<std::uint64_t>(stream.gcount()) == size;
}

} // namespace cdvd

/**
 *
 * Identifies the device a path names with its prefix and copies the path
 * without the prefix; a single-letter drive is left to the default device.
 *
 */
static int GetDevType(char *path, char *out_name) {
    char  device[0x40];
    char *scan;
    char *out;

    if (path[1] == ':') {
        strcpy(out_name, path);
        return FILE_DEV_DEFAULT;
    }

    scan = path;
    out = device;

    for (;;) {
        if (*scan == '\0') {
            break;
        }

        *out++ = *scan;

        if (*scan == ':') {
            break;
        }

        scan++;
    }

    *out = '\0';

    if (*scan != '\0') {
        strcpy(out_name, &scan[1]);
    } else {
        strcpy(out_name, path);
    }

    if (strcmp(device, "host:") == 0) {
        return FILE_DEV_HOST;
    }

    if (strcmp(device, "host0:") == 0) {
        return FILE_DEV_HOST;
    }

    if (strcmp(device, "cdrom:") == 0) {
        return FILE_DEV_CDROM;
    }

    if (strcmp(device, "net:") == 0) {
        return FILE_DEV_NET;
    }

    return strcmp(device, "psf0:") == 0 ? FILE_DEV_HDD : FILE_DEV_DEFAULT;
}

/**
 *
 * Converts the upper-case letters of a string
 * to lower case in place.
 *
 */
static void ConvStr(char *text) {
    char ch;

    while ((ch = *text) != '\0') {
        if (ch >= 'A' && ch <= 'Z') {
            *text += 'a' - 'A';
        }

        text++;
    }
}

/**
 *
 * Builds the path a file is opened by on its device, with the device's
 * prefix and the current directory; returns the device.
 *
 */
static int GetFullPath(char *path, char *out_path) {
    char rest[256];
    int  device = GetDevType(path, rest);
    int  has_device = false;

    if (device == FILE_DEV_DEFAULT) {
        device = DefaultFileDev;
    } else {
        has_device = true;
    }

    char prefix[16] = "";

    if (device == FILE_DEV_HOST) {
        strcpy(prefix, "host:");
    }

    if (device == FILE_DEV_HDD) {
        strcpy(prefix, "pfs0:");
    }

    strcpy(out_path, prefix);

    if (!has_device) {
        strcat(out_path, CurrentDir);
    }

    strcat(out_path, rest);

    if (device == FILE_DEV_HDD) {
        ConvStr(out_path);
    }

    return device;
}

PORT_OVERRIDE int GetMainFileDev() {
    return DefaultFileDev;
}

PORT_OVERRIDE int ChangeHddFile() {
    // The host keeps no hard-drive installation: every file is on the extracted disc, which is
    // already the default device. Failing the mount is how a console without an HDD answers.
    return 0;
}

PORT_OVERRIDE int ChangeDefaultFile() {
    // Nothing to undo: the disc never stopped being the default device.
    return 0;
}

PORT_OVERRIDE void SetCurrentDir(char *dir) {
    if (dir) {
        if (*dir == '/') {
            dir++;
        }

        strcpy(CurrentDir, dir);
        return;
    }

    strcpy(CurrentDir, TopDir);
}

PORT_OVERRIDE void GetCurrentDir(char *out_dir) {
    strcpy(out_dir, CurrentDir);
}

PORT_OVERRIDE void ChangeDir(char *dir) {
    strcpy(CurrentDir, TopDir);

    if (dir) {
        if (*dir == '/') {
            dir++;
        }

        strcat(CurrentDir, dir);
    }
}

PORT_OVERRIDE void InitReadBG() {
    for (int i = 0; i < BG_READ_MAX; i++) {
        bg_read_info[i].busy = 0;
    }
}

PORT_OVERRIDE int LoadFileBG(char *name, u_long128 *buffer, int *out_size) {
    BG_READ_INFO *info;

    if (out_size != NULL) {
        *out_size = 0;
    }

    if (name == NULL || *name == '\0') {
        return false;
    }

    char full_path[256] = "";
    char rest[256];

    strcpy(full_path, CurrentDir);
    strcat(full_path, name);
    int device = GetDevType(name, rest);

    if (device == FILE_DEV_DEFAULT) {
        device = DefaultFileDev;
    }

    info = bg_read_info;

    for (int i = 0; i < BG_READ_MAX; i++, info++) {
        if (!info->busy) {
            break;
        }
    }

    if (info == bg_read_info + BG_READ_MAX) {
        return false;
    }

    if (device == FILE_DEV_CDROM) {
        cdvd::File file;

        if (!cdvd::Find(full_path, file)) {
            return false;
        }

        std::snprintf(info->name, sizeof info->name, "%s", full_path);
        info->busy = true;
        info->dev = FILE_DEV_CDROM;
        info->issued = true;
        info->done = true;
        info->buffer = buffer;
        info->size = static_cast<int>(file.size);
        info->sector = static_cast<int>(file.offset / cdvd::kSector);
        info->sectors = size_to_sector(static_cast<int>(file.size));

        if (out_size != NULL) {
            *out_size = static_cast<int>(file.size);
        }

        if (!ReadWhole(file, buffer)) {
            info->busy = false;
            return false;
        }

        return true;
    }

    // Files on the other devices are read whole here as well; there is no read left to do a
    // vertical sync later.
    int loaded_size = 0;

    if (!LoadFile2(name, buffer, &loaded_size, LOAD_FILE_READ)) {
        return false;
    }

    std::snprintf(info->name, sizeof info->name, "%s", full_path);
    info->busy = true;
    info->dev = device;
    info->issued = true;
    info->done = true;
    info->buffer = buffer;
    info->size = loaded_size;
    info->fd = loaded_size;

    if (out_size != NULL) {
        *out_size = loaded_size;
    }

    return true;
}

PORT_OVERRIDE BG_READ_INFO *GetReadBGFile(char *name) {
    BG_READ_INFO *info = bg_read_info;

    for (int i = 0; i < BG_READ_MAX; i++, info++) {
        if (info->busy && strcasecmp(name, info->name) == 0) {
            return info;
        }
    }

    return NULL;
}

PORT_OVERRIDE BG_READ_INFO *GetReadBGFile(int index) {
    if (index < 0 || index >= BG_READ_MAX) {
        return NULL;
    }

    return bg_read_info[index].busy ? &bg_read_info[index] : NULL;
}

PORT_OVERRIDE void StartReadBG() {
    InitReadBG();
}

PORT_OVERRIDE void ReadBG() {
    // Every host read finishes as it is queued, so there is never a sector left to fetch.
}

PORT_OVERRIDE int ReadBGSync() {
    ReadBG();
    int i = 0;

    for (BG_READ_INFO *info = bg_read_info; i < BG_READ_MAX; i++, info++) {
        if (info->busy && (!info->issued || !info->done)) {
            break;
        }
    }

    return (i == BG_READ_MAX) ^ 1;
}

PORT_OVERRIDE void BreakReadBG() {
    if (!ReadBGSync()) {
        return; // nothing is still running; the finished buffers stay readable
    }

    for (int i = 0; i < BG_READ_MAX; i++) {
        BG_READ_INFO *info = &bg_read_info[i];

        if (info->busy && info->dev != FILE_DEV_CDROM) {
            sceClose(info->fd);
            info->done = true;
        }
    }

    InitReadBG();
}

PORT_OVERRIDE void InitCDFile() {
    cdvd::LoadIndex();
}

PORT_OVERRIDE int LoadFile(char *path, void *buffer, int *out_size) {
    if (!LoadFile2(path, buffer, out_size, LOAD_FILE_READ)) {
        printf("File open error \"%s\"\n \n \n", path);
        Exit__2(0);
    }

    return true;
}

PORT_OVERRIDE int LoadFile2(char *path, void *buffer, int *out_size, int mode) {
    if (out_size) {
        *out_size = 0;
    }

    FILE_CACHE *cache = CacheFind(path);

    if (cache) {
        if (mode == LOAD_FILE_READ) {
            memcpy(buffer, cache->address, cache->size);
            cache->ref_count--;
        }

        if (out_size) {
            *out_size = cache->size;
        }

        return 1;
    }

    char full_path[256] = "";

    int dev = GetFullPath(path, full_path);

    if (dev == FILE_DEV_DEFAULT) {
        dev = DefaultFileDev;
    }

    if (dev == FILE_DEV_NET) {
        // PAL's LoadFileSocket never loads anything.
        return 0;
    }

    if (dev == FILE_DEV_CDROM) {
        cdvd::File file;

        if (!cdvd::Find(full_path, file)) {
            return 0;
        }

        if (mode == LOAD_FILE_SIZE) {
            if (out_size) {
                *out_size = static_cast<int>(file.size);
            }

            return 1;
        }

        if (!ReadWhole(file, buffer)) {
            return 0;
        }

        if (out_size) {
            *out_size = static_cast<int>(file.size);
        }

        return 1;
    }

    if (dev == FILE_DEV_HDD) {
        // The host keeps no hard-drive installation; the disc tree serves every file.
        return 0;
    }

    // host: paths go to the host file server under save/host0/, as the kit's server did.
    int fd = sceOpen(full_path, SCE_RDONLY);

    if (fd < 0) {
        return mode == LOAD_FILE_OPEN ? -1 : 0;
    }

    int size = sceLseek(fd, 0, SCE_SEEK_END);

    if (out_size) {
        *out_size = size;
    }

    if (mode != LOAD_FILE_SIZE) {
        sceLseek(fd, 0, SCE_SEEK_SET);

        if (mode == LOAD_FILE_OPEN) {
            sceClose(fd);
            return sceOpen(full_path, SCE_RDONLY | SCE_NOWAIT);
        }

        sceRead(fd, buffer, size);
    }

    sceClose(fd);
    return 1;
}

PORT_OVERRIDE void InitFileCache(u_long128 *address, int type) {
    CacheAddress = NULL;

    for (int i = 0; i < FILE_CACHE_MAX; i++) {
        file_cache[i].address = NULL;
    }

    if (type == FILE_CACHE_DOWN || type == FILE_CACHE_UP) {
        // The PS2 rounded a pointer up through a 32-bit truncation; the host keeps all of it.
        CacheAddress = reinterpret_cast<u_long128 *>((reinterpret_cast<std::uintptr_t>(address) + 63) &
                                                     ~static_cast<std::uintptr_t>(63));
        NowCacheAddress = CacheAddress;

        if (type == FILE_CACHE_DOWN) {
            NowCacheAddress -= 4;
        }

        FileCacheType = type;
    }
}

PORT_OVERRIDE void DeleteFileCache() {
    InitFileCache(NULL, FILE_CACHE_NONE);
}

PORT_OVERRIDE int LoadFileCacheBG(char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }

    if (CacheAddress == NULL) {
        return false;
    }

    FILE_CACHE *entry = CacheFind(path);

    if (entry != NULL) {
        entry->ref_count += 1;
        return true;
    }

    int size = 0;

    if (LoadFile2(path, NULL, &size, LOAD_FILE_SIZE) == 0) {
        return false;
    }

    u_int      aligned = align_size(static_cast<u_int>(size), DATAREAD_SECTOR_SIZE);
    u_long128 *buffer = NowCacheAddress;

    if (FileCacheType == FILE_CACHE_DOWN) {
        NowCacheAddress -= aligned / 16;
        buffer = NowCacheAddress;
    }

    if (FileCacheType == FILE_CACHE_UP) {
        NowCacheAddress += aligned / 16;
    }

    if (!LoadFileBG(path, buffer, NULL)) {
        return false;
    }

    return EntryFileCache(path, buffer, size);
}

PORT_OVERRIDE u_long128 *SearchFileCache(char *path, int *out_size) {
    if (out_size) {
        *out_size = 0;
    }

    FILE_CACHE *entry = CacheFind(path);

    if (!entry) {
        return NULL;
    }

    if (out_size) {
        *out_size = entry->size;
    }

    return entry->address;
}

PORT_OVERRIDE int WriteFile(char *path, void *buffer, int size) {
    // Only development builds wrote files, through the host server; a colon device is dropped and
    // the rest lands under save/host0/.
    std::string_view name = path;

    if (std::size_t colon = name.rfind(':'); colon != std::string_view::npos) {
        name = name.substr(colon + 1);
    }

    std::string relative(name);
    std::replace(relative.begin(), relative.end(), '\\', '/');
    relative.erase(0, relative.find_first_not_of('/'));

    if (!SafeRelative(relative) || size < 0 || buffer == NULL) {
        return false;
    }

    fs::path        target = PathsSaveRoot() / "host0" / fs::path(relative);
    std::error_code error;

    fs::create_directories(target.parent_path(), error);

    std::ofstream stream(target, std::ios::binary | std::ios::trunc);

    if (!stream) {
        return false;
    }

    stream.write(static_cast<const char *>(buffer), size);
    return stream ? true : false;
}
