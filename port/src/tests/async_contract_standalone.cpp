#include "dataread.hpp"
#include "platform/paths.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;

namespace {
void Put32(Bytes &out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(value >> (8 * i)));
}

void Record(Bytes &out, std::uint32_t name, std::uint32_t size, std::uint32_t sector) {
    Put32(out, name); Put32(out, size); Put32(out, sector);
}

void Write(const fs::path &path, const Bytes &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(stream.good());
}

struct MutableName {
    std::string value;
    explicit MutableName(const char *name) : value(name) {}
    operator char *() { return value.data(); }
};

void InstallFixture(const fs::path &root) {
    // Two indexed files: one complete 32-byte extent and one deliberately truncated extent.
    constexpr std::uint32_t table = 36;
    Bytes hd4;
    Record(hd4, table, 32, 0);
    Record(hd4, table + 9, 32, 1);
    Record(hd4, 0, 0, 0);
    const char names[] = "good.bin\0short.bin\0";
    hd4.insert(hd4.end(), names, names + sizeof(names));
    Write(root / "DATA.HD4", hd4);
    Bytes dat(2048, 0xA7); // Sector 1 is intentionally absent.
    Write(root / "DATA.DAT", dat);
}
} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "ct_async_contract_standalone";
    fs::remove_all(root);
    fs::create_directories(root);
    InstallFixture(root);
    PathsSetDataRoot(root);
    InitCDFile();
    InitReadBG();

    alignas(16) unsigned char bytes[2048 + 16]{}; // Disc callers reserve whole sectors.
    for (int i = 2048; i < 2064; ++i) bytes[i] = 0x5A;
    auto *buffer = reinterpret_cast<u_long128 *>(bytes);
    int size = -1;

    MutableName missing("missing.bin");
    assert(!LoadFileBG(missing, buffer, &size) && size == 0);

    MutableName short_read("short.bin");
    assert(!LoadFileBG(short_read, buffer, &size) && size == 0);
    assert(GetReadBGFile(0) == nullptr); // failed physical read must not leave a busy slot

    MutableName good("good.bin");
    assert(LoadFileBG(good, buffer, &size) && size == 32);
    BG_READ_INFO *queued = GetReadBGFile(0);
    assert(queued && queued->busy && !queued->done && queued->size == 32);
    assert(ReadBGSync() == 1); // queued host read remains pending for the first poll
    assert(GetReadBGFile(0) == queued && !queued->done);
    assert(ReadBGSync() == 0);
    assert(queued->done && queued->busy);
    for (int i = 0; i < 32; ++i) assert(bytes[i] == 0xA7);
    for (int i = 32; i < 2048; ++i) assert(bytes[i] == 0);
    for (int i = 2048; i < 2064; ++i) assert(bytes[i] == 0x5A);

    // Queue a second real file and cancel through the public production API while pending.
    assert(LoadFileBG(good, buffer, &size));
    assert(GetReadBGFile(0) != nullptr);
    BreakReadBG();
    assert(GetReadBGFile(0) == nullptr && ReadBGSync() == 0);

    fs::remove_all(root);
    return 0;
}
