#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "dc2data.hpp"

namespace {

namespace dc = dc2data;
namespace fs = std::filesystem;

struct Row {
    std::string   name;
    std::uint32_t sector;
    std::string   data;
};

void PutLe32(std::string &out, std::uint32_t value) {
    for (int i = 0; i < 4; i++) {
        out.push_back(static_cast<char>(value >> (8 * i)));
    }
}

void SetLe32(std::string &out, std::size_t offset, std::uint32_t value) {
    std::string bytes;
    PutLe32(bytes, value);
    out.replace(offset, 4, bytes);
}

std::string Pattern(unsigned seed, std::size_t size) {
    std::string out(size, '\0');
    for (std::size_t i = 0; i < size; i++) {
        out[i] = static_cast<char>((seed * 131 + i * 7 + i / 256) & 0xFF);
    }
    return out;
}

std::span<const unsigned char> Bytes(const std::string &text) {
    return std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(text.data()), text.size());
}

std::uint32_t SectorsFor(std::size_t size) {
    return static_cast<std::uint32_t>((size + dc::kSector - 1) / dc::kSector);
}

std::string BuildSoundIndex(const std::vector<Row> &rows) {
    auto        names_start = static_cast<std::uint32_t>((rows.size() + 1) * dc::kSoundRecordSize);
    std::string records;
    std::string names;
    for (const Row &row : rows) {
        PutLe32(records, names_start + static_cast<std::uint32_t>(names.size()));
        PutLe32(records, static_cast<std::uint32_t>(row.data.size()));
        PutLe32(records, row.sector);
        PutLe32(records, SectorsFor(row.data.size()));
        names += row.name;
        names.push_back('\0');
    }
    PutLe32(records, names_start + static_cast<std::uint32_t>(names.size()));
    PutLe32(records, 0);
    PutLe32(records, 0);
    PutLe32(records, 0);
    names.push_back('\0');
    return records + names;
}

std::string BuildDataIndex(const std::vector<Row> &rows) {
    auto        names_start = static_cast<std::uint32_t>((rows.size() + 1) * dc::kIndexRecordSize);
    std::string records;
    std::string names;
    for (const Row &row : rows) {
        PutLe32(records, names_start + static_cast<std::uint32_t>(names.size()));
        PutLe32(records, static_cast<std::uint32_t>(row.data.size()));
        PutLe32(records, row.sector);
        names += row.name;
        names.push_back('\0');
    }
    PutLe32(records, names_start + static_cast<std::uint32_t>(names.size()));
    PutLe32(records, 0);
    PutLe32(records, 0);
    names.push_back('\0');
    return records + names;
}

std::string BuildDat(const std::vector<Row> &rows) {
    std::uint64_t end = 0;
    for (const Row &row : rows) {
        end = std::max<std::uint64_t>(end, row.sector * dc::kSector + row.data.size());
    }
    std::string dat(end, '\0');
    for (const Row &row : rows) {
        std::copy(row.data.begin(), row.data.end(), dat.begin() + row.sector * dc::kSector);
    }
    return dat;
}

void WriteBytes(const fs::path &path, const std::string &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string ReadBytes(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

class Scratch {
public:
    Scratch() {
        const testing::TestInfo *info = testing::UnitTest::GetInstance()->current_test_info();
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / std::format("sound-dat-{}-{}", info->name(), stamp);
        fs::create_directories(path_);
    }

    ~Scratch() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path &path() const {
        return path_;
    }

private:
    fs::path path_;
};

constexpr std::uint64_t kDatSize = 4 * dc::kSector;

std::vector<Row> SoundRows() {
    return {
        {"0010010.wav", 0, Pattern(1, 3000)},
        {"0010011.wav", 2, Pattern(2, 2048)},
        {"0010012.wav", 3, Pattern(3, 1)},
    };
}

std::string IndexWithName(const std::string &name) {
    return BuildSoundIndex(std::vector<Row>{Row{name, 0, Pattern(4, 10)}});
}

void ExpectRejected(const std::string &hd3, std::uint64_t dat_size) {
    EXPECT_THROW(dc::ParseSoundIndex(Bytes(hd3), dat_size), dc::Error);
}

} // namespace

TEST(SoundDat, AcceptsAnAllZeroTerminator) {
    std::string index = BuildSoundIndex(SoundRows());
    SetLe32(index, 3 * dc::kSoundRecordSize, 0);
    dc::SoundIndex parsed = dc::ParseSoundIndex(Bytes(index), kDatSize);

    EXPECT_EQ(parsed.entries.size(), 3u);
    EXPECT_TRUE(parsed.shadowed.empty());
}

TEST(SoundDat, ParsesASyntheticIndex) {
    std::string       index = BuildSoundIndex(SoundRows());
    dc::SoundIndex    parsed = dc::ParseSoundIndex(Bytes(index), kDatSize);

    ASSERT_EQ(parsed.entries.size(), 3u);
    EXPECT_TRUE(parsed.shadowed.empty());
    EXPECT_EQ(parsed.entries[0].name, "0010010.wav");
    EXPECT_EQ(parsed.entries[0].size, 3000u);
    EXPECT_EQ(parsed.entries[0].sector, 0u);
    EXPECT_EQ(parsed.entries[0].sectors, 2u);
    EXPECT_EQ(parsed.entries[1].name, "0010011.wav");
    EXPECT_EQ(parsed.entries[1].size, 2048u);
    EXPECT_EQ(parsed.entries[1].sector, 2u);
    EXPECT_EQ(parsed.entries[1].sectors, 1u);
    EXPECT_EQ(parsed.entries[2].name, "0010012.wav");
    EXPECT_EQ(parsed.entries[2].size, 1u);
    EXPECT_EQ(parsed.entries[2].sector, 3u);
    EXPECT_EQ(parsed.entries[2].sectors, 1u);
}

TEST(SoundDat, ShadowsADuplicateName) {
    std::vector<Row> rows = {
        {"8640040.wav", 0, Pattern(1, 100)},
        {"8640040.WAV", 1, Pattern(2, 100)},
        {"8640040.wav", 2, Pattern(3, 100)},
    };
    dc::SoundIndex parsed = dc::ParseSoundIndex(Bytes(BuildSoundIndex(rows)), kDatSize);

    ASSERT_EQ(parsed.entries.size(), 2u);
    EXPECT_EQ(parsed.entries[0].name, "8640040.wav");
    EXPECT_EQ(parsed.entries[0].sector, 0u);
    EXPECT_EQ(parsed.entries[1].name, "8640040.WAV");
    ASSERT_EQ(parsed.shadowed.size(), 1u);
    EXPECT_EQ(parsed.shadowed[0].name, "8640040.wav");
    EXPECT_EQ(parsed.shadowed[0].sector, 2u);
}

TEST(SoundDat, RejectsTableNotAMultipleOf16) {
    std::string index = BuildSoundIndex(SoundRows());
    SetLe32(index, 0, 13);
    ExpectRejected(index, kDatSize);
}

TEST(SoundDat, RejectsFirstOffsetBeyondTheFile) {
    std::string index = BuildSoundIndex(SoundRows());
    SetLe32(index, 0, 0x00100000);
    ExpectRejected(index, kDatSize);
}

TEST(SoundDat, RejectsMissingTerminator) {
    std::string index = BuildSoundIndex(SoundRows());
    SetLe32(index, 3 * dc::kSoundRecordSize + 4, 7);
    ExpectRejected(index, kDatSize);
}

TEST(SoundDat, RejectsZeroSizeRecordBeforeTheTerminator) {
    std::string index = BuildSoundIndex(SoundRows());
    SetLe32(index, 4, 0);
    ExpectRejected(index, kDatSize);
}

TEST(SoundDat, RejectsSectorCountThatDisagreesWithSize) {
    std::string index = BuildSoundIndex(SoundRows());
    SetLe32(index, 12, 9);
    ExpectRejected(index, kDatSize);
}

TEST(SoundDat, RejectsEntryPastTheEndOfSoundDat) {
    std::string index = BuildSoundIndex(SoundRows());
    ExpectRejected(index, 1000);
}

TEST(SoundDat, RejectsNameOffsetOutsideTheNames) {
    std::string index = BuildSoundIndex(SoundRows());
    SetLe32(index, dc::kSoundRecordSize, 0x00FFFFFF);
    ExpectRejected(index, kDatSize);
}

TEST(SoundDat, RejectsUnterminatedName) {
    std::string index = BuildSoundIndex(SoundRows());
    SetLe32(index, 3 * dc::kSoundRecordSize, 0);
    index.pop_back();
    index.pop_back();
    ExpectRejected(index, kDatSize);
}

TEST(SoundDat, RejectsNameWithSlash) {
    ExpectRejected(IndexWithName("dir/0010010.wav"), kDatSize);
}

TEST(SoundDat, RejectsNameWithBackslash) {
    ExpectRejected(IndexWithName("dir\\0010010.wav"), kDatSize);
}

TEST(SoundDat, RejectsEmptyName) {
    ExpectRejected(IndexWithName(""), kDatSize);
}

TEST(SoundDat, ExtractsSoundEntriesAndKeepsTheWholeFiles) {
    Scratch          scratch;
    std::vector<Row> sound_rows = {
        {"0010010.wav", 0, Pattern(21, 3000)},
        {"0010011.wav", 2, Pattern(22, 100)},
        {"0010010.wav", 3, Pattern(23, 10)},
    };
    const std::string sound_dat = BuildDat(sound_rows);
    const std::string sound_hd3 = BuildSoundIndex(sound_rows);
    std::vector<Row>  data_rows = {{"Readme.txt", 0, "hello"}};

    fs::path disc = scratch.path() / "disc";
    WriteBytes(disc / "DATA.HD4", BuildDataIndex(data_rows));
    WriteBytes(disc / "DATA.DAT", BuildDat(data_rows));
    WriteBytes(disc / "SOUND.DAT", sound_dat);
    WriteBytes(disc / "SOUND.HD3", sound_hd3);
    fs::create_directories(disc / "MOVIE");
    fs::path out = scratch.path() / "data";

    dc::Archive archive = dc::OpenArchive(disc);
    dc::Summary summary = dc::Extract(archive, out, nullptr);

    EXPECT_EQ(summary.written, 5u);
    EXPECT_EQ(ReadBytes(out / "sound" / "0010010.wav"), Pattern(21, 3000));
    EXPECT_EQ(ReadBytes(out / "sound" / "0010011.wav"), Pattern(22, 100));
    EXPECT_EQ(ReadBytes(out / "sound" / "sound.dat"), sound_dat);
    EXPECT_EQ(ReadBytes(out / "sound" / "sound.hd3"), sound_hd3);
    EXPECT_EQ(ReadBytes(out / "readme.txt"), "hello");

    dc::CheckReport report = dc::CheckManifest(out);
    EXPECT_TRUE(report.problems.empty());
    EXPECT_EQ(report.checked, 5u);

    fs::file_time_type stamp = fs::last_write_time(out / "sound" / "0010011.wav");
    dc::Summary        again = dc::Extract(archive, out, nullptr);
    EXPECT_EQ(again.written, 0u);
    EXPECT_EQ(again.kept, 5u);
    EXPECT_FALSE(again.manifest_changed);
    EXPECT_EQ(fs::last_write_time(out / "sound" / "0010011.wav"), stamp);
}

TEST(SoundDat, RealDiscSoundIndex) {
    fs::path        disc = DC2_DISC_DIR;
    std::error_code error;
    if (!fs::is_regular_file(disc / "SOUND.HD3", error) || !fs::is_regular_file(disc / "SOUND.DAT", error)) {
        GTEST_SKIP() << "no SOUND.HD3 and SOUND.DAT in " << disc.string();
    }

    dc::Archive archive;
    archive.sound_dat = {disc / "SOUND.DAT", 0, static_cast<std::uint64_t>(fs::file_size(disc / "SOUND.DAT"))};
    archive.sound_hd3 = {disc / "SOUND.HD3", 0, static_cast<std::uint64_t>(fs::file_size(disc / "SOUND.HD3"))};
    dc::SoundIndex index = dc::ReadSoundIndex(archive);

    EXPECT_EQ(index.entries.size() + index.shadowed.size(), 3101u);
    ASSERT_EQ(index.shadowed.size(), 1u);
    EXPECT_EQ(index.shadowed.front().name, "8640040.wav");

    // The table's last record may be the shadowed copy, so the end is taken over both lists.
    std::uint64_t end = 0;
    std::uint64_t bytes = 0;
    for (const std::vector<dc::SoundEntry> *list : {&index.entries, &index.shadowed}) {
        for (const dc::SoundEntry &entry : *list) {
            end = std::max(end, (std::uint64_t{entry.sector} + entry.sectors) * dc::kSector);
            bytes += entry.size;
        }
    }
    EXPECT_EQ(end, archive.sound_dat.size);
    EXPECT_EQ(bytes, 966486464u);
}
