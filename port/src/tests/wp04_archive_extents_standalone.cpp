// Archive read-failure fixture for port/src/dataread.cpp.
//
//   wp04_archive_extents_standalone <pal-iso-dir>
//
// <pal-iso-dir> holds the retail DATA.HD4 / DATA.DAT; exit 77 (skip) when it is absent. Synthetic cases build
// small HD4/DAT pairs in a scratch directory: a truncated DAT, a DAT shortened or deleted after indexing, a
// range outside an extent, sector padding and host0: fallback. Failures must name the asset, extent and cause
// and must not publish a size, a busy background slot or buffer padding.
//
// Built with -DWP04_BASELINE against the previous dataread.cpp, the same file reports the old behaviour: it
// has no failure accessors, so the stand-ins below return nothing and the named-failure checks fail.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cdvd_file.hpp"
#include "dataread.hpp"
#include "mg_memory.hpp"
#include "dataread_port.hpp"
#include "platform/paths.hpp"

#ifdef WP04_BASELINE
std::string DataReadLastFailure() { return {}; }
unsigned DataReadFailureCount() { return 0; }
#else
std::string DataReadLastFailure();
unsigned DataReadFailureCount();
#endif

extern "C" int sceOpen(const char *, int, ...) { return -1; }
extern "C" int sceLseek(int, int, int) { return 0; }
extern "C" int sceRead(int, void *, int) { return 0; }
extern "C" int sceClose(int) { return 0; }
extern "C" void Exit__2(int) { std::abort(); }
int size_to_sector(int size) { return (size + 2047) / 2048; }

namespace fs = std::filesystem;

namespace {

int failures = 0;

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
    return condition;
}

bool Contains(const std::string &text, const char *needle) {
    return text.find(needle) != std::string::npos;
}

void Put32(std::vector<unsigned char> &out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(value >> (8 * i)));
}

struct Entry {
    std::string   name;
    std::uint32_t size;
    std::uint32_t sector;
};

// PAL layout: 12-byte records, then the names; the record table ends with a record naming nothing.
void WriteArchive(const fs::path &dir, const std::vector<Entry> &entries, std::uint64_t dat_bytes) {
    fs::create_directories(dir);
    const std::uint32_t table = static_cast<std::uint32_t>((entries.size() + 1) * 12);
    std::vector<unsigned char> hd4;
    std::uint32_t name_offset = table;
    for (const Entry &e : entries) {
        Put32(hd4, name_offset);
        Put32(hd4, e.size);
        Put32(hd4, e.sector);
        name_offset += static_cast<std::uint32_t>(e.name.size() + 1);
    }
    Put32(hd4, name_offset); // terminator: points at an empty name
    Put32(hd4, 0);
    Put32(hd4, 0);
    for (const Entry &e : entries) {
        hd4.insert(hd4.end(), e.name.begin(), e.name.end());
        hd4.push_back(0);
    }
    hd4.push_back(0);
    // the table length is the first word of the first record's name offset; PAL's first name follows the table
    std::ofstream(dir / "DATA.HD4", std::ios::binary).write(reinterpret_cast<char *>(hd4.data()), hd4.size());
    std::vector<unsigned char> dat(dat_bytes, 0);
    for (const Entry &e : entries) {
        for (std::uint32_t i = 0; i < e.size && std::uint64_t(e.sector) * 2048 + i < dat_bytes; ++i)
            dat[std::uint64_t(e.sector) * 2048 + i] = static_cast<unsigned char>('A' + (e.sector + i) % 23);
    }
    std::ofstream(dir / "DATA.DAT", std::ios::binary).write(reinterpret_cast<char *>(dat.data()), dat.size());
}

char *Mut(std::string &s) { return s.data(); }

void Synthetic(const fs::path &scratch) {
    const fs::path dir = scratch / "archive";
    fs::remove_all(dir);
    // good.bin: 3000 bytes at sector 0 (padded to 4096). edge.bin: 100 bytes at the last sector, so its
    // sector-padded end lies past the DAT. long.bin: 5000 bytes, extent ends past the DAT.
    WriteArchive(dir, {{"good.bin", 3000, 0}, {"edge.bin", 100, 2}, {"long.bin", 5000, 2}}, 2 * 2048 + 1000);
    DataReadIndex(dir);

    // truncated extent: named at index time and never published
    Check(DataReadFailureCount() >= 1 && Contains(DataReadLastFailure(), "long.bin") &&
              Contains(DataReadLastFailure(), "not indexed"),
          "an extent past the end of DATA.DAT is named and left out of the index");
    cdvd::File file;
    Check(!cdvd::Find("long.bin", file), "long.bin is not published");
    std::string long_name = "long.bin";
    std::vector<unsigned char> big(16384, 0xCD);
    int size = 77;
    Check(LoadFile2(Mut(long_name), big.data(), &size, 0) == 0 && size == 0,
          "LoadFile2 of an unpublished extent fails with size 0");
    int bg_size = 55;
    InitReadBG();
    Check(!LoadFileBG(Mut(long_name), reinterpret_cast<u_long128 *>(big.data()), &bg_size) && bg_size == 0,
          "LoadFileBG of an unpublished extent fails with size 0");
    Check(ReadBGSync() == 0, "a failed background load leaves no busy slot");

    // sector padding: the logical bytes exist, so the file loads and its padding is zeroed
    std::string good = "good.bin";
    std::fill(big.begin(), big.end(), 0xCD);
    Check(LoadFile2(Mut(good), big.data(), &size, 0) == 1 && size == 3000, "good.bin loads with its logical size");
    bool padded_zero = true, tail_untouched = true;
    for (int i = 3000; i < 4096; ++i) padded_zero &= big[i] == 0;
    for (int i = 4096; i < 8192; ++i) tail_untouched &= big[i] == 0xCD;
    Check(padded_zero, "the sector padding after good.bin is zeroed");
    Check(tail_untouched, "nothing past the padded sector is written");
    std::string edge = "edge.bin";
    std::fill(big.begin(), big.end(), 0xCD);
    Check(LoadFile2(Mut(edge), big.data(), &size, 0) == 1 && size == 100,
          "edge.bin loads although its padded sector would pass the end of DATA.DAT");
    padded_zero = true;
    for (int i = 100; i < 2048; ++i) padded_zero &= big[i] == 0;
    Check(padded_zero, "edge.bin's padding is zero");

    // background: first sync pending, second complete
    InitReadBG();
    Check(LoadFileBG(Mut(good), reinterpret_cast<u_long128 *>(big.data()), &bg_size) && bg_size == 3000,
          "LoadFileBG loads good.bin");
    Check(ReadBGSync() == 1, "the first ReadBGSync still reports the queued read pending");
    Check(ReadBGSync() == 0, "the second ReadBGSync reports it complete");

    // out-of-range sub-read
    Check(cdvd::Find("good.bin", file), "good.bin is published");
    unsigned before = DataReadFailureCount();
    Check(!cdvd::ReadAt(file, 2999, big.data(), 2), "a range ending past the extent is refused");
    Check(DataReadFailureCount() == before + 1 && Contains(DataReadLastFailure(), "range outside"),
          "the refusal names the range fault");

    // DATA.DAT shortened after indexing: a read comes up short and says how short
    DataReadIndex(dir);
    WriteArchive(dir, {{"good.bin", 3000, 0}, {"edge.bin", 100, 2}, {"long.bin", 5000, 2}}, 2 * 2048 + 1000);
    DataReadIndex(dir);
    std::string edge2 = "edge.bin";
    Check(cdvd::Find("edge.bin", file), "edge.bin is published for the truncation case");
    fs::resize_file(dir / "DATA.DAT", 2 * 2048 + 50); // 50 of edge.bin's 100 bytes remain
    std::fill(big.begin(), big.end(), 0xCD);
    size = 9;
    Check(LoadFile2(Mut(edge2), big.data(), &size, 0) == 0 && size == 0, "a DAT cut inside the extent fails with size 0");
    const std::string cut = DataReadLastFailure();
    Check(Contains(cut, "edge.bin") && Contains(cut, "DATA.DAT") && Contains(cut, "backing file ends inside") &&
              Contains(cut, "physical 50 bytes") && Contains(cut, "extent 100"),
          "the failure names the asset, the DAT, the extent and the physical count");
    bool untouched = true;
    for (int i = 100; i < 2048; ++i) untouched &= big[i] == 0xCD;
    Check(untouched, "a failed read does not write the padding");
    InitReadBG();
    bg_size = 11;
    Check(!LoadFileBG(Mut(edge2), reinterpret_cast<u_long128 *>(big.data()), &bg_size) && bg_size == 0 && ReadBGSync() == 0,
          "a failed background read publishes neither size nor busy slot");

    // DATA.DAT removed: the open fault is reported
    fs::remove(dir / "DATA.DAT");
    size = 5;
    std::string good2 = "good.bin";
    Check(LoadFile2(Mut(good2), big.data(), &size, 0) == 0 && size == 0, "a missing DAT fails with size 0");
    Check(Contains(DataReadLastFailure(), "good.bin") && Contains(DataReadLastFailure(), "would not open"),
          "the missing-file failure names the asset and the open fault");

    // bounded: a failing loop is counted, not printed forever (the index still holds good.bin)
    for (int i = 0; i < 40; ++i) LoadFile2(Mut(good2), big.data(), &size, 0);
    Check(DataReadFailureCount() >= 30, "repeated failures are all counted");
}

// The logged failure `cannot read ...\img\2\loading.img` of the 568d125a playtest: CreateNowLoading
// (ps2/src/nowload.cpp:330) copies LoadInfo.memory through a 48-byte block, but the host mgCMemory is 72
// bytes and `stack` sits at offset 48, so LoadInfo.memory.stGetTop() is NULL and the file is read into NULL.
void NullDestination(const fs::path &scratch) {
    static_assert(sizeof(mgCMemory) > 48 && offsetof(mgCMemory, stack) >= 48,
                  "the 48-byte LoadingMemoryWords copy drops mgCMemory::stack on this host");
    const fs::path dir = scratch / "plain_null";
    fs::remove_all(dir);
    fs::create_directories(dir / "img" / "2");
    {
        std::ofstream out(dir / "img" / "2" / "loading.img", std::ios::binary);
        std::vector<char> bytes(50320, 'i');
        out.write(bytes.data(), bytes.size());
    }
    DataReadIndex(dir);
    alignas(mgCMemory) unsigned char storage[sizeof(mgCMemory)] = {}; // LoadInfo is static: zero until copied
    mgCMemory &lost = *reinterpret_cast<mgCMemory *>(storage);
    std::string name = "img/2/loading.img";
    int size = 99;
    Check(LoadFile2(Mut(name), lost.stGetTop(), &size, 0) == 0 && size == 0,
          "reading loading.img into a NULL stGetTop() fails and publishes no size");
    Check(Contains(DataReadLastFailure(), "img/2/loading.img") && Contains(DataReadLastFailure(), "no destination buffer") &&
              Contains(DataReadLastFailure(), "size 50320"),
          "the failure names loading.img, its size and the missing destination, not the disc");
    std::vector<unsigned char> good(50320 + 2048, 0xCD);
    Check(LoadFile2(Mut(name), good.data(), &size, 0) == 1 && size == 50320, "the same read into a real buffer succeeds");
    // size queries need no buffer
    Check(LoadFile2(Mut(name), nullptr, &size, LOAD_FILE_SIZE) == 1 && size == 50320, "a size query accepts a NULL buffer");
}

void HostZero(const fs::path &scratch) {
    // host0: names fall back to the indexed tree; a short plain file must not publish its size.
    const fs::path dir = scratch / "plain";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "save.bin", std::ios::binary);
        std::vector<char> bytes(3000, 'x');
        out.write(bytes.data(), bytes.size());
    }
    DataReadIndex(dir);
    std::string name = "host0:save.bin";
    std::vector<unsigned char> buffer(8192, 0xCD);
    int size = 0;
    Check(LoadFile2(Mut(name), buffer.data(), &size, 0) == 1 && size == 3000, "host0 fallback reads a plain file");
    fs::resize_file(dir / "save.bin", 1000); // shrinks behind the index
    std::fill(buffer.begin(), buffer.end(), 0xCD);
    size = 123;
    Check(LoadFile2(Mut(name), buffer.data(), &size, 0) == 0, "host0 fallback of a shrunken file fails");
    Check(size == 0, "host0 fallback does not publish a size for a read that failed");
    Check(Contains(DataReadLastFailure(), "host0:save.bin") && Contains(DataReadLastFailure(), "ends inside") &&
              Contains(DataReadLastFailure(), "physical 1000 bytes"),
          "the host0 failure names the request, the cause and the physical count");
}

// Retail PAL: every record lies inside DATA.DAT and reads whole through the production path.
void RetailPal(const fs::path &iso) {
    DataReadIndex(iso);
    Check(DataReadFailureCount() == 0, "the retail PAL index has no rejected extent");

    std::ifstream hd4(iso / "DATA.HD4", std::ios::binary);
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(hd4)), std::istreambuf_iterator<char>());
    auto le = [&](size_t at) {
        return std::uint32_t(raw[at]) | std::uint32_t(raw[at + 1]) << 8 | std::uint32_t(raw[at + 2]) << 16 |
               std::uint32_t(raw[at + 3]) << 24;
    };
    const std::uint32_t table = le(0);
    size_t indexed = 0, unreadable = 0, mismatched = 0;
    std::vector<unsigned char> buffer;
    std::vector<std::string> seen; // the first record of a name wins, as the game's linear search finds it
    for (size_t i = 0; i < table / 12; ++i) {
        const std::uint32_t name_at = le(i * 12), sz = le(i * 12 + 4), sector = le(i * 12 + 8);
        if (name_at == 0 && sz == 0 && sector == 0) break;
        const char *name = reinterpret_cast<const char *>(raw.data() + name_at);
        if (*name == 0) break; // PAL's terminator record
        cdvd::File file;
        if (!cdvd::Find(name, file)) {
            ++unreadable;
            continue;
        }
        ++indexed;
        const std::string folded = cdvd::Fold(name);
        const bool first = std::find(seen.begin(), seen.end(), folded) == seen.end();
        seen.push_back(folded);
        if (first) mismatched += file.size != sz || file.offset != std::uint64_t(sector) * 2048;
        buffer.assign(sz + 2048, 0xCD);
        std::string request = name;
        int out = -1;
        if (LoadFile2(Mut(request), buffer.data(), &out, 0) != 1 || out != int(sz)) ++unreadable;
        const size_t padded = (std::uint64_t(sz) + 2047) / 2048 * 2048;
        for (size_t b = sz; b < padded; ++b) {
            if (buffer[b] != 0) {
                ++mismatched;
                break;
            }
        }
    }
    std::printf("retail PAL: %zu indexed, %zu unreadable, %zu mismatched\n", indexed, unreadable, mismatched);
    Check(indexed >= 8750, "all retail PAL records are indexed");
    Check(unreadable == 0, "every retail PAL record reads whole through LoadFile2");
    Check(mismatched == 0, "retail extents and zeroed padding match the HD4 records");
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <pal-iso-dir>\n", argv[0]);
        return 2;
    }
    const fs::path scratch = fs::temp_directory_path() / "wp04_archive_extents";
    fs::create_directories(scratch);
    Synthetic(scratch);
    HostZero(scratch);
    NullDestination(scratch);
    if (fs::exists(fs::path(argv[1]) / "DATA.HD4") && fs::exists(fs::path(argv[1]) / "DATA.DAT")) {
        RetailPal(argv[1]);
    } else {
        std::puts("SKIP retail: DATA.HD4/DATA.DAT not found");
        fs::remove_all(scratch);
        return failures ? 1 : 77;
    }
    fs::remove_all(scratch);
    if (failures) {
        std::fprintf(stderr, "%d wp04 check(s) failed\n", failures);
        return 1;
    }
    std::puts("wp04 archive extent fixture passed");
    return 0;
}
