#pragma once

// The extractor's core, header-only so the port's tests can use it without linking the tool.
// It includes no game header.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dc2data {

namespace fs = std::filesystem;

inline constexpr std::uint64_t kSector = 2048;
inline constexpr std::size_t   kIndexRecordSize = 12;
inline constexpr std::size_t   kSoundRecordSize = 16;
inline constexpr std::size_t   kChunkSize = std::size_t{4} << 20;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <class... Args>
[[noreturn]] void Fail(std::format_string<Args...> format, Args &&...args) {
    throw Error(std::format(format, std::forward<Args>(args)...));
}

inline char FoldChar(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool EqualsFolded(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return FoldChar(x) == FoldChar(y);
           });
}

inline bool EndsWithFolded(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && EqualsFolded(text.substr(text.size() - suffix.size()), suffix);
}

// The key the game matches a path by: forward slashes, no leading separator, ASCII case folded.
inline std::string FoldPath(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        out.push_back(c == '\\' ? '/' : FoldChar(c));
    }
    out.erase(0, out.find_first_not_of('/'));
    return out;
}

// Bytes >= 0x80 (Shift-JIS) and '%' are percent-encoded so every on-disk name is ASCII on every platform
// and stays reversible.
inline std::string DiskName(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (unsigned char c : path) {
        if (c == '\\') {
            out.push_back('/');
        } else if (c >= 0x80 || c == '%') {
            out += std::format("%{:02X}", c);
        } else {
            out.push_back(FoldChar(static_cast<char>(c)));
        }
    }
    return out;
}

inline bool IsSafeRelative(std::string_view path) {
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

inline std::uint32_t Le32(const unsigned char *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

class Reader {
public:
    explicit Reader(const fs::path &path)
        : path_(path), stream_(path, std::ios::binary) {
        if (!stream_) {
            Fail("cannot open {}", path.string());
        }
        std::error_code error;
        size_ = fs::file_size(path, error);
        if (error) {
            Fail("cannot size {}: {}", path.string(), error.message());
        }
    }

    std::uint64_t Size() const {
        return size_;
    }

    const fs::path &Path() const {
        return path_;
    }

    void Read(std::uint64_t offset, void *destination, std::size_t size) {
        if (offset > size_ || size > size_ - offset) {
            Fail("{} is truncated: {} bytes at offset {} lie past its end ({} bytes)", path_.string(), size,
                 offset, size_);
        }
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset));
        stream_.read(static_cast<char *>(destination), static_cast<std::streamsize>(size));
        if (static_cast<std::size_t>(stream_.gcount()) != size) {
            Fail("read error in {} at offset {}", path_.string(), offset);
        }
    }

private:
    fs::path      path_;
    std::ifstream stream_;
    std::uint64_t size_ = 0;
};

struct Extent {
    fs::path      file;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct IsoEntry {
    std::string   name;
    std::uint32_t extent = 0;
    std::uint32_t size = 0;
    bool          directory = false;
};

// ISO 9660 alone: the disc's UDF bridge is not parsed, and every file the game reads is reachable
// through the ISO 9660 tree.
class Iso9660 {
public:
    explicit Iso9660(const fs::path &image)
        : reader_(image) {
        unsigned char descriptor[kSector];
        for (std::uint64_t sector = 16;; sector++) {
            if ((sector + 1) * kSector > reader_.Size()) {
                Fail("{} has no ISO 9660 primary volume descriptor", image.string());
            }
            reader_.Read(sector * kSector, descriptor, sizeof descriptor);
            if (std::memcmp(descriptor + 1, "CD001", 5) != 0) {
                Fail("{} is not an ISO 9660 image", image.string());
            }
            if (descriptor[0] == 1) {
                break;
            }
            if (descriptor[0] == 255) {
                Fail("{} has no ISO 9660 primary volume descriptor", image.string());
            }
        }
        root_ = Parse(descriptor + 156, 34);
        if (!root_.directory) {
            Fail("{}: the root directory record is not a directory", image.string());
        }
    }

    const IsoEntry &Root() const {
        return root_;
    }

    std::vector<IsoEntry> List(const IsoEntry &directory) {
        std::vector<unsigned char> data(directory.size);
        reader_.Read(directory.extent * kSector, data.data(), data.size());
        std::vector<IsoEntry> entries;
        std::size_t           offset = 0;
        while (offset < data.size()) {
            std::size_t length = data[offset];
            // A record never crosses a sector; the rest of a sector after the last one is zero.
            if (length == 0) {
                offset = (offset / kSector + 1) * kSector;
                continue;
            }
            if (length < 34 || offset + length > data.size()) {
                Fail("{}: bad directory record at sector {} offset {}", reader_.Path().string(),
                     directory.extent, offset);
            }
            IsoEntry entry = Parse(&data[offset], length);
            offset += length;
            if (entry.name != std::string_view("\0", 1) && entry.name != "\1") {
                entries.push_back(std::move(entry));
            }
        }
        return entries;
    }

    std::optional<IsoEntry> Find(const IsoEntry &directory, std::string_view name) {
        for (IsoEntry &entry : List(directory)) {
            if (EqualsFolded(StripVersion(entry.name), name)) {
                return entry;
            }
        }
        return std::nullopt;
    }

    static std::string_view StripVersion(std::string_view identifier) {
        identifier = identifier.substr(0, identifier.find(';'));
        if (!identifier.empty() && identifier.back() == '.') {
            identifier.remove_suffix(1);
        }
        return identifier;
    }

private:
    static IsoEntry Parse(const unsigned char *record, std::size_t length) {
        std::size_t name_length = record[32];
        if (33 + name_length > length) {
            Fail("bad ISO 9660 directory record: name runs past the record");
        }
        IsoEntry entry;
        entry.extent = Le32(record + 2);
        entry.size = Le32(record + 10);
        entry.directory = (record[25] & 2) != 0;
        entry.name.assign(reinterpret_cast<const char *>(record + 33), name_length);
        return entry;
    }

    Reader   reader_;
    IsoEntry root_;
};

struct NamedExtent {
    std::string name;
    Extent      extent;
};

struct Archive {
    fs::path                 source;
    bool                     image = false;
    Extent                   dat;
    Extent                   hd4;
    Extent                   sound_dat;
    Extent                   sound_hd3;
    std::vector<NamedExtent> movies;
};

inline fs::path FindInDirectory(const fs::path &directory, std::string_view name, bool want_directory) {
    for (const fs::directory_entry &entry : fs::directory_iterator(directory)) {
        if (entry.is_directory() == want_directory && EqualsFolded(entry.path().filename().string(), name)) {
            return entry.path();
        }
    }
    return {};
}

inline Extent DirectoryFile(const fs::path &root, std::string_view name) {
    fs::path path = FindInDirectory(root, name, false);
    if (path.empty()) {
        Fail("{} holds no {}", root.string(), name);
    }
    return {path, 0, static_cast<std::uint64_t>(fs::file_size(path))};
}

inline Archive OpenDirectory(const fs::path &root) {
    Archive archive;
    archive.source = root;
    archive.dat = DirectoryFile(root, "DATA.DAT");
    archive.hd4 = DirectoryFile(root, "DATA.HD4");
    archive.sound_dat = DirectoryFile(root, "SOUND.DAT");
    archive.sound_hd3 = DirectoryFile(root, "SOUND.HD3");
    fs::path movies = FindInDirectory(root, "MOVIE", true);
    if (movies.empty()) {
        Fail("{} holds no MOVIE directory", root.string());
    }
    for (const fs::directory_entry &entry : fs::directory_iterator(movies)) {
        std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && EndsWithFolded(name, ".pss")) {
            archive.movies.push_back({name, {entry.path(), 0, static_cast<std::uint64_t>(entry.file_size())}});
        }
    }
    std::sort(archive.movies.begin(), archive.movies.end(),
              [](const NamedExtent &a, const NamedExtent &b) { return a.name < b.name; });
    return archive;
}

inline Extent ImageFile(const fs::path &image, Iso9660 &iso, std::string_view name) {
    std::optional<IsoEntry> entry = iso.Find(iso.Root(), name);
    if (!entry || entry->directory) {
        Fail("{} has no {} in its root directory", image.string(), name);
    }
    return {image, entry->extent * kSector, entry->size};
}

inline void CheckWithin(const Extent &extent, std::uint64_t image_size, const fs::path &image) {
    if (extent.offset + extent.size > image_size) {
        Fail("{} is truncated: it holds {} bytes, its directory places a file up to byte {}", image.string(),
             image_size, extent.offset + extent.size);
    }
}

inline Archive OpenImage(const fs::path &image) {
    Iso9660 iso(image);
    Archive archive;
    archive.source = image;
    archive.image = true;
    archive.dat = ImageFile(image, iso, "DATA.DAT");
    archive.hd4 = ImageFile(image, iso, "DATA.HD4");
    archive.sound_dat = ImageFile(image, iso, "SOUND.DAT");
    archive.sound_hd3 = ImageFile(image, iso, "SOUND.HD3");
    std::optional<IsoEntry> movies = iso.Find(iso.Root(), "MOVIE");
    if (!movies || !movies->directory) {
        Fail("{} has no MOVIE directory in its root", image.string());
    }
    for (IsoEntry &entry : iso.List(*movies)) {
        std::string_view name = Iso9660::StripVersion(entry.name);
        if (!entry.directory && EndsWithFolded(name, ".pss")) {
            archive.movies.push_back({std::string(name), {image, entry.extent * kSector, entry.size}});
        }
    }
    std::sort(archive.movies.begin(), archive.movies.end(),
              [](const NamedExtent &a, const NamedExtent &b) { return a.name < b.name; });
    std::uint64_t image_size = fs::file_size(image);
    for (const Extent *extent : {&archive.dat, &archive.hd4, &archive.sound_dat, &archive.sound_hd3}) {
        CheckWithin(*extent, image_size, image);
    }
    for (const NamedExtent &movie : archive.movies) {
        CheckWithin(movie.extent, image_size, image);
    }
    return archive;
}

inline Archive OpenArchive(const fs::path &source) {
    std::error_code error;
    if (fs::is_directory(source, error)) {
        return OpenDirectory(source);
    }
    if (!fs::is_regular_file(source, error)) {
        Fail("{} is neither a disc image nor a directory", source.string());
    }
    return OpenImage(source);
}

inline std::vector<unsigned char> ReadExtent(const Extent &extent) {
    Reader                     reader(extent.file);
    std::vector<unsigned char> data(extent.size);
    reader.Read(extent.offset, data.data(), data.size());
    return data;
}

struct Record {
    std::string   name;
    std::string   path;
    std::uint32_t size = 0;
    std::uint32_t sector = 0;
};

// DATA.HD4: 12-byte records of name offset, size and sector, then the NUL-terminated names. The game
// takes the record count from the first name offset, which is where the names begin; the last
// record has no name and names no file, but it is part of that count.
inline std::vector<Record> ParseIndex(std::span<const unsigned char> hd4) {
    if (hd4.size() < kIndexRecordSize) {
        Fail("DATA.HD4 is {} bytes, too short for one record", hd4.size());
    }
    std::uint32_t names_start = Le32(hd4.data());
    if (names_start == 0 || names_start % kIndexRecordSize != 0 || names_start > hd4.size()) {
        Fail("DATA.HD4 begins with name offset {}, not a whole number of 12-byte records inside the file", names_start);
    }
    std::size_t         count = names_start / kIndexRecordSize;
    std::vector<Record> records;
    records.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        const unsigned char *raw = hd4.data() + i * kIndexRecordSize;
        std::uint32_t        name_offset = Le32(raw);
        if (name_offset < names_start || name_offset >= hd4.size()) {
            Fail("DATA.HD4 record {}: name offset {} lies outside the names", i, name_offset);
        }
        const void *end = std::memchr(hd4.data() + name_offset, 0, hd4.size() - name_offset);
        if (!end) {
            Fail("DATA.HD4 record {}: name at {} is not terminated", i, name_offset);
        }
        const char *begin = reinterpret_cast<const char *>(hd4.data() + name_offset);
        Record      record;
        record.name.assign(begin, static_cast<const char *>(end));
        std::replace(record.name.begin(), record.name.end(), '\\', '/');
        record.path = FoldPath(record.name);
        if (!record.path.empty() && !IsSafeRelative(record.path)) {
            Fail("DATA.HD4 record {}: unusable path \"{}\"", i, record.name);
        }
        record.size = Le32(raw + 4);
        record.sector = Le32(raw + 8);
        records.push_back(std::move(record));
    }
    return records;
}

inline void CheckFits(const std::vector<Record> &records, std::uint64_t dat_size) {
    for (const Record &record : records) {
        std::uint64_t end = record.sector * kSector + record.size;
        if (end > dat_size) {
            Fail("DATA.DAT is truncated: {} needs bytes up to {}, DATA.DAT holds {}", record.name, end, dat_size);
        }
    }
}

inline std::vector<Record> ReadIndex(const Archive &archive) {
    std::vector<unsigned char> hd4 = ReadExtent(archive.hd4);
    std::vector<Record>        records = ParseIndex(hd4);
    CheckFits(records, archive.dat.size);
    return records;
}

struct SoundEntry {
    std::string   name;
    std::uint32_t size = 0;
    std::uint32_t sector = 0;
    std::uint32_t sectors = 0;
};

struct SoundIndex {
    std::vector<SoundEntry> entries;
    std::vector<SoundEntry> shadowed;
};

// SOUND.HD3: 16-byte records of name offset, size, sector and sector count, then the NUL-terminated names.
// The game takes the record count from the first name offset. The last record within that count is the
// terminator: zero size, sector and sector count, and a name offset of 0 or of an empty string in the pool.
// A name is found by exact, case-sensitive strcmp, first match wins.
inline SoundIndex ParseSoundIndex(std::span<const unsigned char> hd3, std::uint64_t dat_size) {
    if (hd3.size() < kSoundRecordSize) {
        Fail("SOUND.HD3 is {} bytes, too short for one record", hd3.size());
    }
    std::uint32_t names_start = Le32(hd3.data());
    if (names_start == 0 || names_start % kSoundRecordSize != 0 || names_start > hd3.size()) {
        Fail("SOUND.HD3 begins with name offset {}, not a whole number of 16-byte records inside the file",
             names_start);
    }
    std::size_t          count = names_start / kSoundRecordSize;
    const unsigned char *terminator = hd3.data() + (count - 1) * kSoundRecordSize;
    std::uint32_t        terminator_name = Le32(terminator);
    bool                 empty_name = terminator_name == 0 ||
                      (terminator_name >= names_start && terminator_name < hd3.size() && hd3[terminator_name] == 0);
    if (!empty_name || Le32(terminator + 4) != 0 || Le32(terminator + 8) != 0 || Le32(terminator + 12) != 0) {
        Fail("SOUND.HD3 has no terminator as its last record");
    }

    SoundIndex                      index;
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i + 1 < count; i++) {
        const unsigned char *raw = hd3.data() + i * kSoundRecordSize;
        std::uint32_t        name_offset = Le32(raw);
        SoundEntry           entry;
        entry.size = Le32(raw + 4);
        entry.sector = Le32(raw + 8);
        entry.sectors = Le32(raw + 12);
        if (entry.size == 0) {
            Fail("SOUND.HD3 record {} is empty", i);
        }
        if (name_offset < names_start || name_offset >= hd3.size()) {
            Fail("SOUND.HD3 record {}: name offset {} lies outside the names", i, name_offset);
        }
        const void *nul = std::memchr(hd3.data() + name_offset, 0, hd3.size() - name_offset);
        if (!nul) {
            Fail("SOUND.HD3 record {}: name at {} is not terminated", i, name_offset);
        }
        entry.name.assign(reinterpret_cast<const char *>(hd3.data() + name_offset),
                          static_cast<const char *>(nul));
        std::replace(entry.name.begin(), entry.name.end(), '\\', '/');
        if (entry.name.find('/') != std::string::npos || !IsSafeRelative(entry.name)) {
            Fail("SOUND.HD3 record {}: unusable name \"{}\"", i, entry.name);
        }
        std::uint64_t needed = (std::uint64_t{entry.size} + kSector - 1) / kSector;
        if (needed != entry.sectors) {
            Fail("SOUND.HD3 record {} ({}): {} bytes take {} sectors, not {}", i, entry.name, entry.size, needed,
                 entry.sectors);
        }
        if (std::uint64_t{entry.sector} * kSector + entry.size > dat_size) {
            Fail("SOUND.HD3 record {} ({}): entry ends past SOUND.DAT's {} bytes", i, entry.name, dat_size);
        }
        // Mirrors the driver's first-match lookup: a later copy of a name can never be read.
        if (seen.insert(entry.name).second) {
            index.entries.push_back(std::move(entry));
        } else {
            index.shadowed.push_back(std::move(entry));
        }
    }
    return index;
}

inline SoundIndex ReadSoundIndex(const Archive &archive) {
    std::vector<unsigned char> hd3 = ReadExtent(archive.sound_hd3);
    return ParseSoundIndex(hd3, archive.sound_dat.size);
}

inline const Record *FindRecord(const std::vector<Record> &records, std::string_view name) {
    std::string key = FoldPath(name);
    for (const Record &record : records) {
        if (record.path == key) {
            return &record;
        }
    }
    return nullptr;
}

template <class... Args>
void Log(std::FILE *log, std::format_string<Args...> format, Args &&...args) {
    if (log) {
        std::fputs(std::format(format, std::forward<Args>(args)...).c_str(), log);
    }
}

// The records the game can reach. Its linear search returns the first match, so a later duplicate
// is never read; an unnamed record is never reached at all.
inline std::vector<const Record *> Reachable(const std::vector<Record> &records, std::FILE *log,
                                             std::size_t *duplicates) {
    std::vector<const Record *>     reachable;
    std::unordered_set<std::string> seen;
    for (const Record &record : records) {
        if (record.path.empty()) {
            continue;
        }
        if (seen.insert(record.path).second) {
            reachable.push_back(&record);
        } else {
            ++*duplicates;
            Log(log, "warning: {} is listed again at sector {}; the game finds only the first\n", record.name,
                record.sector);
        }
    }
    return reachable;
}

struct Summary {
    std::size_t   records = 0;
    std::size_t   files = 0;
    std::size_t   duplicates = 0;
    std::uint64_t bytes = 0;
    std::size_t   written = 0;
    std::size_t   kept = 0;
    std::uint64_t written_bytes = 0;
    std::size_t   removed = 0;
    std::size_t   verified = 0;
    bool          manifest_changed = false;
};

inline bool IsCurrent(const fs::path &path, std::uint64_t size) {
    std::error_code error;
    return fs::is_regular_file(path, error) && fs::file_size(path, error) == size && !error;
}

inline std::string ReadText(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

inline void WriteText(const fs::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file) {
        Fail("cannot write {}", path.string());
    }
}

// Writes through a temporary name so that a file of the right size is always a complete one. The
// temporary is removed on any failure, the rename included.
template <class Write>
void Publish(const fs::path &target, Write &&write) {
    fs::path temp = target;
    temp += ".dc2tmp";
    try {
        write(temp);
        fs::rename(temp, target);
    } catch (...) {
        std::error_code error;
        fs::remove(temp, error);
        throw;
    }
}

inline void CopyExtent(const Extent &source, const fs::path &target, std::vector<unsigned char> &chunk) {
    fs::create_directories(target.parent_path());
    Publish(target, [&](const fs::path &temp) {
        Reader        reader(source.file);
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            Fail("cannot create {}", temp.string());
        }
        for (std::uint64_t done = 0; done < source.size;) {
            std::size_t step = static_cast<std::size_t>(std::min<std::uint64_t>(source.size - done, chunk.size()));
            reader.Read(source.offset + done, chunk.data(), step);
            file.write(reinterpret_cast<const char *>(chunk.data()), static_cast<std::streamsize>(step));
            if (!file) {
                Fail("write error on {}", temp.string());
            }
            done += step;
        }
        file.close();
        if (!file) {
            Fail("write error on {}", temp.string());
        }
    });
}

struct Planned {
    std::string rel;
    Extent      source;
};

inline void Place(const Planned &entry, const fs::path &out, std::vector<unsigned char> &chunk, Summary &summary) {
    fs::path target = out / fs::path(DiskName(entry.rel));
    if (IsCurrent(target, entry.source.size)) {
        summary.kept++;
        return;
    }
    CopyExtent(entry.source, target, chunk);
    if (!IsCurrent(target, entry.source.size)) {
        Fail("{} is not {} bytes after writing", target.string(), entry.source.size);
    }
    summary.written++;
    summary.written_bytes += entry.source.size;
}

struct ManifestEntry {
    std::uint64_t size = 0;
    std::string   path;
};

inline std::vector<ManifestEntry> ReadManifest(const fs::path &out) {
    std::vector<ManifestEntry> entries;
    fs::path                   path = out / "manifest.txt";
    std::error_code            error;
    if (!fs::is_regular_file(path, error)) {
        return entries;
    }
    std::string text = ReadText(path);
    std::size_t start = 0;
    for (std::size_t number = 1; start < text.size(); number++) {
        std::size_t      end = std::min(text.find('\n', start), text.size());
        std::string_view line(text.data() + start, end - start);
        std::size_t      tab = line.find('\t');
        if (tab == std::string_view::npos) {
            Fail("{}: manifest line {} has no tab: \"{}\"", path.string(), number, line);
        }
        std::string_view      digits = line.substr(0, tab);
        std::string_view      rel = line.substr(tab + 1);
        std::uint64_t         size = 0;
        std::from_chars_result parsed = std::from_chars(digits.data(), digits.data() + digits.size(), size);
        if (parsed.ec != std::errc() || parsed.ptr != digits.data() + digits.size() || !IsSafeRelative(rel)) {
            Fail("{}: manifest line {} is malformed: \"{}\"", path.string(), number, line);
        }
        entries.push_back({size, std::string(rel)});
        start = end + 1;
    }
    return entries;
}

// Removes what the previous manifest listed and this extraction no longer writes. Only listed paths are
// touched; the current names are never removed. Each listed path is tried as written and as its disk
// name, because manifests from earlier builds list raw Shift-JIS names.
inline std::size_t RemoveStale(const fs::path &out, const std::vector<ManifestEntry> &previous,
                               const std::vector<Planned> &plan) {
    std::unordered_set<std::string> current_disk;
    for (const Planned &entry : plan) {
        current_disk.insert(DiskName(entry.rel));
    }
    std::size_t removed = 0;
    for (const ManifestEntry &old : previous) {
        for (const std::string &name : {DiskName(old.path), old.path}) {
            if (current_disk.contains(name)) {
                continue;
            }
            std::error_code error;
            if (fs::remove(out / fs::path(name), error)) {
                removed++;
            } else if (error) {
                Fail("cannot remove {}: {}", (out / fs::path(name)).string(), error.message());
            }
        }
    }
    return removed;
}

inline bool WriteManifest(const fs::path &out, const std::vector<Planned> &plan) {
    std::string text;
    for (const Planned &entry : plan) {
        text += std::format("{}\t{}\n", entry.source.size, DiskName(entry.rel));
    }
    fs::path path = out / "manifest.txt";
    std::error_code error;
    if (fs::is_regular_file(path, error) && ReadText(path) == text) {
        return false;
    }
    Publish(path, [&](const fs::path &temp) { WriteText(temp, text); });
    return true;
}

struct CheckReport {
    std::size_t              checked = 0;
    std::vector<std::string> problems;
};

inline CheckReport CheckManifest(const fs::path &out) {
    std::error_code error;
    if (!fs::is_regular_file(out / "manifest.txt", error)) {
        Fail("{} holds no manifest.txt", out.string());
    }
    CheckReport report;
    for (const ManifestEntry &entry : ReadManifest(out)) {
        report.checked++;
        fs::path      file = out / fs::path(entry.path);
        std::error_code status;
        bool          regular = fs::is_regular_file(file, status);
        std::uint64_t found = regular ? fs::file_size(file, status) : 0;
        if (!regular || status) {
            report.problems.push_back(std::format("missing {}", entry.path));
        } else if (found != entry.size) {
            report.problems.push_back(std::format("size {}: expected {}, found {}", entry.path, entry.size, found));
        }
    }
    return report;
}

inline Summary Extract(const Archive &archive, const fs::path &out, std::FILE *log) {
    std::vector<Record> records = ReadIndex(archive);
    Summary             summary;
    summary.records = records.size();

    std::vector<const Record *> reachable = Reachable(records, log, &summary.duplicates);
    summary.files = reachable.size();

    std::vector<Planned>            plan;
    std::unordered_set<std::string> targets;
    auto add = [&](std::string rel, Extent source) {
        std::string disk = DiskName(rel);
        if (disk == "manifest.txt" || disk.ends_with(".dc2tmp")) {
            Fail("{} would be written as {}, which the extractor reserves", rel, disk);
        }
        if (!targets.insert(rel).second) {
            Fail("{} would be written twice", rel);
        }
        plan.push_back({std::move(rel), std::move(source)});
    };
    for (const Record *record : reachable) {
        add(record->path, {archive.dat.file, archive.dat.offset + record->sector * kSector, record->size});
        summary.bytes += record->size;
    }
    for (const NamedExtent &movie : archive.movies) {
        add("movie/" + FoldPath(movie.name), movie.extent);
    }
    add("sound/sound.dat", archive.sound_dat);
    add("sound/sound.hd3", archive.sound_hd3);
    SoundIndex sound = ReadSoundIndex(archive);
    for (const SoundEntry &entry : sound.entries) {
        add("sound/" + FoldPath(entry.name),
            {archive.sound_dat.file, archive.sound_dat.offset + entry.sector * kSector, entry.size});
    }
    Log(log, "sound: {} entries, {} shadowed\n", sound.entries.size(), sound.shadowed.size());

    std::vector<ManifestEntry> previous = ReadManifest(out);
    fs::create_directories(out);
    std::vector<unsigned char> chunk(kChunkSize);
    for (const Planned &entry : plan) {
        Place(entry, out, chunk, summary);
    }
    summary.removed = RemoveStale(out, previous, plan);
    summary.manifest_changed = WriteManifest(out, plan);

    CheckReport report = CheckManifest(out);
    if (!report.problems.empty()) {
        Fail("verification failed: {}", report.problems.front());
    }
    summary.verified = report.checked;
    return summary;
}

} // namespace dc2data
