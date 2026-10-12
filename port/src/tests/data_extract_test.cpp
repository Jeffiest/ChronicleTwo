#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
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

void PutBe32(std::string &out, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; i++) {
        out[offset + i] = static_cast<char>(value >> (8 * (3 - i)));
    }
}

std::string Pattern(unsigned seed, std::size_t size) {
    std::string out(size, '\0');
    for (std::size_t i = 0; i < size; i++) {
        out[i] = static_cast<char>((seed * 131 + i * 7 + i / 256) & 0xFF);
    }
    return out;
}

std::string BuildIndex(const std::vector<Row> &rows) {
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
        path_ = fs::temp_directory_path() /
                std::format("dc2data-{}-{}", info->name(), std::chrono::steady_clock::now().time_since_epoch().count());
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

const std::string kSoundDat = Pattern(9, 3000);
const std::string kSoundHd3 = Pattern(10, 700);
const std::string kEnding = Pattern(11, 300);
const std::string kIntro = Pattern(12, 40);
const std::string kMovie = Pattern(13, 500);

fs::path WriteDisc(const fs::path &root, const std::string &index, const std::string &dat) {
    fs::path disc = root / "disc";
    WriteBytes(disc / "DATA.HD4", index);
    WriteBytes(disc / "DATA.DAT", dat);
    WriteBytes(disc / "SOUND.DAT", kSoundDat);
    WriteBytes(disc / "SOUND.HD3", kSoundHd3);
    WriteBytes(disc / "MOVIE" / "intro.PSS", kIntro);
    WriteBytes(disc / "MOVIE" / "Ending.PSS", kEnding);
    WriteBytes(disc / "MOVIE" / "readme.txt", "not a movie");
    return disc;
}

std::vector<Row> SyntheticRows() {
    return {
        {"Alpha\\Beta.BIN", 0, Pattern(1, 100)},
        {"gamma\\Delta.dat", 1, Pattern(2, 5000)},
        {"MIXED\\case\\File.Txt", 4, ""},
    };
}

std::string DirectoryRecord(const std::string &identifier, std::uint32_t extent, std::uint32_t size,
                            bool directory) {
    std::size_t length = 33 + identifier.size() + (identifier.size() % 2 == 0 ? 1 : 0);
    std::string record(length, '\0');
    record[0] = static_cast<char>(length);
    SetLe32(record, 2, extent);
    PutBe32(record, 6, extent);
    SetLe32(record, 10, size);
    PutBe32(record, 14, size);
    record[25] = directory ? static_cast<char>(0x02) : '\0';
    record[28] = 1;
    record[31] = 1;
    record[32] = static_cast<char>(identifier.size());
    record.replace(33, identifier.size(), identifier);
    return record;
}

std::string Sector(const std::string &content) {
    std::string sector(dc::kSector, '\0');
    sector.replace(0, content.size(), content);
    return sector;
}

// The Iso9660 reader's view of a disc: system area, primary volume descriptor at 16, terminator at 17,
// the root directory at 18, MOVIE at 19, the files from 20 on.
std::string BuildIso(const std::string &index, const std::string &dat) {
    constexpr std::uint32_t kRootSector = 18;
    constexpr std::uint32_t kMovieSector = 19;
    const std::string       self(1, '\0');
    const std::string       parent(1, '\1');

    std::string image(20 * dc::kSector, '\0');
    auto        place = [&image](const std::string &data) {
        image.resize((image.size() + dc::kSector - 1) / dc::kSector * dc::kSector, '\0');
        auto sector = static_cast<std::uint32_t>(image.size() / dc::kSector);
        image += data;
        return sector;
    };
    auto index_sector = place(index);
    auto dat_sector = place(dat);
    auto sound_dat_sector = place(kSoundDat);
    auto sound_hd3_sector = place(kSoundHd3);
    auto movie_sector = place(kMovie);
    image.resize((image.size() + dc::kSector - 1) / dc::kSector * dc::kSector, '\0');

    std::string pvd(dc::kSector, '\0');
    pvd[0] = 1;
    pvd.replace(1, 5, "CD001");
    pvd[6] = 1;
    pvd.replace(156, 34, DirectoryRecord(self, kRootSector, dc::kSector, true));
    image.replace(16 * dc::kSector, dc::kSector, pvd);

    std::string terminator(dc::kSector, '\0');
    terminator[0] = static_cast<char>(255);
    terminator.replace(1, 5, "CD001");
    terminator[6] = 1;
    image.replace(17 * dc::kSector, dc::kSector, terminator);

    image.replace(kRootSector * dc::kSector, dc::kSector,
                  Sector(DirectoryRecord(self, kRootSector, dc::kSector, true) +
                         DirectoryRecord(parent, kRootSector, dc::kSector, true) +
                         DirectoryRecord("DATA.HD4;1", index_sector, static_cast<std::uint32_t>(index.size()), false) +
                         DirectoryRecord("DATA.DAT;1", dat_sector, static_cast<std::uint32_t>(dat.size()), false) +
                         DirectoryRecord("SOUND.DAT;1", sound_dat_sector,
                                         static_cast<std::uint32_t>(kSoundDat.size()), false) +
                         DirectoryRecord("SOUND.HD3;1", sound_hd3_sector,
                                         static_cast<std::uint32_t>(kSoundHd3.size()), false) +
                         DirectoryRecord("MOVIE", kMovieSector, dc::kSector, true)));
    image.replace(kMovieSector * dc::kSector, dc::kSector,
                  Sector(DirectoryRecord(self, kMovieSector, dc::kSector, true) +
                         DirectoryRecord(parent, kRootSector, dc::kSector, true) +
                         DirectoryRecord("A.PSS;1", movie_sector, static_cast<std::uint32_t>(kMovie.size()), false)));
    return image;
}

void ExpectGameIndex(const std::vector<dc::Record> &records) {
    EXPECT_EQ(records.size(), 8753u);

    const dc::Record *edit = dc::FindRecord(records, "EDIT.CFG");
    ASSERT_NE(edit, nullptr);
    EXPECT_EQ(edit->path, "edit.cfg");
    EXPECT_EQ(edit->size, 1133u);

    std::size_t duplicates = 0;
    EXPECT_EQ(dc::Reachable(records, nullptr, &duplicates).size(), 8751u);
    EXPECT_EQ(duplicates, 1u);
}

} // namespace

TEST(Dc2Data, ExtractsEntriesMoviesAndSound) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    fs::path         disc = WriteDisc(scratch.path(), BuildIndex(rows), BuildDat(rows));
    fs::path         out = scratch.path() / "data";

    dc::Archive archive = dc::OpenArchive(disc);
    dc::Summary summary = dc::Extract(archive, out, nullptr);

    EXPECT_EQ(summary.records, 4u);
    EXPECT_EQ(summary.files, 3u);
    EXPECT_EQ(summary.duplicates, 0u);
    EXPECT_EQ(summary.written, 7u);
    EXPECT_EQ(summary.kept, 0u);
    EXPECT_EQ(summary.verified, 7u);
    EXPECT_TRUE(summary.manifest_changed);

    EXPECT_EQ(ReadBytes(out / "alpha" / "beta.bin"), Pattern(1, 100));
    EXPECT_EQ(ReadBytes(out / "gamma" / "delta.dat"), Pattern(2, 5000));
    EXPECT_TRUE(fs::is_regular_file(out / "mixed" / "case" / "file.txt"));
    EXPECT_EQ(fs::file_size(out / "mixed" / "case" / "file.txt"), 0u);
    EXPECT_EQ(ReadBytes(out / "movie" / "intro.pss"), kIntro);
    EXPECT_EQ(ReadBytes(out / "movie" / "ending.pss"), kEnding);
    EXPECT_FALSE(fs::exists(out / "movie" / "readme.txt"));
    EXPECT_EQ(ReadBytes(out / "sound" / "sound.dat"), kSoundDat);
    EXPECT_EQ(ReadBytes(out / "sound" / "sound.hd3"), kSoundHd3);

    std::string manifest = std::format("100\talpha/beta.bin\n5000\tgamma/delta.dat\n0\tmixed/case/file.txt\n"
                                       "{}\tmovie/ending.pss\n{}\tmovie/intro.pss\n{}\tsound/sound.dat\n{}\tsound/sound.hd3\n",
                                       kEnding.size(), kIntro.size(), kSoundDat.size(), kSoundHd3.size());
    EXPECT_EQ(ReadBytes(out / "manifest.txt"), manifest);

    std::vector<dc::Record> records = dc::ReadIndex(archive);
    const dc::Record       *beta = dc::FindRecord(records, "ALPHA\\BETA.BIN");
    ASSERT_NE(beta, nullptr);
    EXPECT_EQ(beta->path, "alpha/beta.bin");
}

TEST(Dc2Data, RerunRewritesNothingAndRestoresWrongSizes) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    fs::path         disc = WriteDisc(scratch.path(), BuildIndex(rows), BuildDat(rows));
    fs::path         out = scratch.path() / "data";
    dc::Archive      archive = dc::OpenArchive(disc);
    dc::Extract(archive, out, nullptr);
    fs::file_time_type stamp = fs::last_write_time(out / "gamma" / "delta.dat");

    dc::Summary again = dc::Extract(archive, out, nullptr);
    EXPECT_EQ(again.written, 0u);
    EXPECT_EQ(again.kept, 7u);
    EXPECT_FALSE(again.manifest_changed);
    EXPECT_EQ(fs::last_write_time(out / "gamma" / "delta.dat"), stamp);

    WriteBytes(out / "alpha" / "beta.bin", "short");
    dc::Summary repaired = dc::Extract(archive, out, nullptr);
    EXPECT_EQ(repaired.written, 1u);
    EXPECT_EQ(repaired.kept, 6u);
    EXPECT_EQ(ReadBytes(out / "alpha" / "beta.bin"), Pattern(1, 100));
}

TEST(Dc2Data, DuplicateNameIsReadFromItsFirstRecord) {
    Scratch          scratch;
    std::vector<Row> rows = {
        {"dir\\Same.bin", 0, Pattern(3, 64)},
        {"DIR\\same.BIN", 1, Pattern(4, 64)},
    };
    fs::path    disc = WriteDisc(scratch.path(), BuildIndex(rows), BuildDat(rows));
    fs::path    out = scratch.path() / "data";
    dc::Summary summary = dc::Extract(dc::OpenArchive(disc), out, nullptr);

    EXPECT_EQ(summary.duplicates, 1u);
    EXPECT_EQ(ReadBytes(out / "dir" / "same.bin"), Pattern(3, 64));
}

TEST(Dc2Data, DiskNameEncodesShiftJisAndPercentOnEveryPlatform) {
    const std::string game = std::string("DIR\\") + '\x83' + 'A' + "%A.BIN";
    EXPECT_EQ(dc::DiskName(game), "dir/%83a%25a.bin");
    EXPECT_EQ(dc::DiskName("Alpha\\Beta.BIN"), "alpha/beta.bin");
}

TEST(Dc2Data, ShiftJisNameIsWrittenPercentEncodedInFilesAndManifest) {
    Scratch          scratch;
    const std::string name = std::string("DIR\\") + '\x83' + 'A' + "%A.BIN";
    std::vector<Row> rows = {{name, 0, Pattern(5, 20)}};
    fs::path         disc = WriteDisc(scratch.path(), BuildIndex(rows), BuildDat(rows));
    fs::path         out = scratch.path() / "data";

    dc::Extract(dc::OpenArchive(disc), out, nullptr);

    EXPECT_EQ(ReadBytes(out / "dir" / "%83a%25a.bin"), Pattern(5, 20));
    EXPECT_FALSE(fs::exists(out / "dir" / name));

    const std::string manifest = ReadBytes(out / "manifest.txt");
    EXPECT_NE(manifest.find("20\tdir/%83a%25a.bin\n"), std::string::npos);
    EXPECT_EQ(manifest.find(name), std::string::npos);
}

TEST(Dc2Data, ExtractRemovesOnlyFilesTheOldManifestListed) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    fs::path         disc = WriteDisc(scratch.path(), BuildIndex(rows), BuildDat(rows));
    fs::path         out = scratch.path() / "data";
    dc::Archive      archive = dc::OpenArchive(disc);
    dc::Extract(archive, out, nullptr);

    const std::string raw_rel = std::string("old/s") + '\x83' + ".bin";
    WriteBytes(out / "manifest.txt", ReadBytes(out / "manifest.txt") + "3\told/gone.bin\n5\t" + raw_rel + "\n");
    WriteBytes(out / "old" / "gone.bin", "abc");
    WriteBytes(out / "old" / (std::string("s") + '\x83' + ".bin"), "hello");
    WriteBytes(out / "keep" / "mine.bin", "mine");

    dc::Summary summary = dc::Extract(archive, out, nullptr);

    EXPECT_EQ(summary.removed, 2u);
    EXPECT_FALSE(fs::exists(out / "old" / "gone.bin"));
    EXPECT_FALSE(fs::exists(out / "old" / (std::string("s") + '\x83' + ".bin")));
    EXPECT_EQ(ReadBytes(out / "keep" / "mine.bin"), "mine");
    EXPECT_EQ(ReadBytes(out / "alpha" / "beta.bin"), Pattern(1, 100));
    EXPECT_EQ(ReadBytes(out / "manifest.txt").find("gone.bin"), std::string::npos);
}

TEST(Dc2Data, MalformedManifestFailsBeforeWriting) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    fs::path         disc = WriteDisc(scratch.path(), BuildIndex(rows), BuildDat(rows));
    fs::path         out = scratch.path() / "data";
    dc::Archive      archive = dc::OpenArchive(disc);
    dc::Extract(archive, out, nullptr);

    WriteBytes(out / "manifest.txt", "100 alpha/beta.bin\n");
    fs::remove(out / "alpha" / "beta.bin");

    EXPECT_THROW(dc::Extract(archive, out, nullptr), dc::Error);
    EXPECT_FALSE(fs::exists(out / "alpha" / "beta.bin"));
}

TEST(Dc2Data, PlannedEntryNamedLikeAReservedFileFailsBeforeWriting) {
    for (const char *name : {"Manifest.TXT", "Data.DC2TMP"}) {
        Scratch          scratch;
        std::vector<Row> rows = {{name, 0, Pattern(6, 10)}};
        fs::path         disc = WriteDisc(scratch.path(), BuildIndex(rows), BuildDat(rows));
        fs::path         out = scratch.path() / "data";

        EXPECT_THROW(dc::Extract(dc::OpenArchive(disc), out, nullptr), dc::Error) << name;
        EXPECT_FALSE(fs::exists(out)) << name;
    }
}

TEST(Dc2Data, CheckReportsMissingAndWrongSizeFiles) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    fs::path         disc = WriteDisc(scratch.path(), BuildIndex(rows), BuildDat(rows));
    fs::path         out = scratch.path() / "data";
    dc::Extract(dc::OpenArchive(disc), out, nullptr);

    dc::CheckReport clean = dc::CheckManifest(out);
    EXPECT_TRUE(clean.problems.empty());
    EXPECT_EQ(clean.checked, 7u);

    fs::remove(out / "gamma" / "delta.dat");
    WriteBytes(out / "alpha" / "beta.bin", "short");

    dc::CheckReport broken = dc::CheckManifest(out);
    EXPECT_EQ(broken.checked, 7u);
    EXPECT_EQ(broken.problems, (std::vector<std::string>{"size alpha/beta.bin: expected 100, found 5",
                                                         "missing gamma/delta.dat"}));
    EXPECT_THROW(dc::CheckManifest(scratch.path() / "absent"), dc::Error);
}

TEST(Dc2Data, ExtractsFromAnIso9660Image) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    fs::path         iso = scratch.path() / "disc.iso";
    WriteBytes(iso, BuildIso(BuildIndex(rows), BuildDat(rows)));
    fs::path out = scratch.path() / "data";

    dc::Summary summary = dc::Extract(dc::OpenArchive(iso), out, nullptr);

    EXPECT_EQ(summary.records, 4u);
    EXPECT_EQ(summary.files, 3u);
    EXPECT_EQ(summary.written, 6u);
    EXPECT_EQ(summary.verified, 6u);
    EXPECT_EQ(ReadBytes(out / "alpha" / "beta.bin"), Pattern(1, 100));
    EXPECT_EQ(ReadBytes(out / "gamma" / "delta.dat"), Pattern(2, 5000));
    EXPECT_EQ(fs::file_size(out / "mixed" / "case" / "file.txt"), 0u);
    EXPECT_EQ(ReadBytes(out / "movie" / "a.pss"), kMovie);
    EXPECT_EQ(ReadBytes(out / "sound" / "sound.dat"), kSoundDat);
    EXPECT_EQ(ReadBytes(out / "sound" / "sound.hd3"), kSoundHd3);
}

TEST(Dc2Data, IndexWithBadRecordTableFailsBeforeWriting) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    std::string      index = BuildIndex(rows);
    SetLe32(index, 0, 13);
    fs::path disc = WriteDisc(scratch.path(), index, BuildDat(rows));
    fs::path out = scratch.path() / "data";

    EXPECT_THROW(dc::Extract(dc::OpenArchive(disc), out, nullptr), dc::Error);
    EXPECT_FALSE(fs::exists(out));
}

TEST(Dc2Data, IndexNameOffsetPastTheFileFailsBeforeWriting) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    std::string      index = BuildIndex(rows);
    SetLe32(index, dc::kIndexRecordSize, 0x00FFFFFF);
    fs::path disc = WriteDisc(scratch.path(), index, BuildDat(rows));
    fs::path out = scratch.path() / "data";

    EXPECT_THROW(dc::Extract(dc::OpenArchive(disc), out, nullptr), dc::Error);
    EXPECT_FALSE(fs::exists(out));
}

TEST(Dc2Data, EntryPastTheEndOfDatFailsBeforeWriting) {
    Scratch          scratch;
    std::vector<Row> rows = SyntheticRows();
    std::string      index = BuildIndex(rows);
    SetLe32(index, dc::kIndexRecordSize + 4, 0x7FFFFFFF);
    fs::path disc = WriteDisc(scratch.path(), index, BuildDat(rows));
    fs::path out = scratch.path() / "data";

    EXPECT_THROW(dc::Extract(dc::OpenArchive(disc), out, nullptr), dc::Error);
    EXPECT_FALSE(fs::exists(out));
}

TEST(Dc2Data, RealDiscIndexHasTheGamesEntries) {
    fs::path disc = DC2_DISC_DIR;
    for (const char *name : {"DATA.DAT", "DATA.HD4", "SOUND.DAT", "SOUND.HD3", "MOVIE"}) {
        if (!fs::exists(disc / name)) {
            GTEST_SKIP() << "no disc files in " << disc.string() << " (" << name << " is missing)";
        }
    }

    ExpectGameIndex(dc::ReadIndex(dc::OpenArchive(disc)));
}

TEST(Dc2Data, RealIsoIndexHasTheGamesEntries) {
    fs::path        iso = DC2_ISO_PATH;
    std::error_code error;
    if (!fs::is_regular_file(iso, error)) {
        GTEST_SKIP() << "no disc image at " << iso.string();
    }

    ExpectGameIndex(dc::ReadIndex(dc::OpenArchive(iso)));
}
