#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "audio/hdbank.hpp"
#include "audio/hsmsg.hpp"
#include "audio/mixer.hpp"
#include "audio/sq.hpp"
#include "audio_bank_fixture.hpp"
#include "dataread.hpp"
#include "ezbgm.hpp"
#include "platform/paths.hpp"
#include "snd_mngr.hpp"
#include "sound.hpp"
#include "sound_port.hpp"

namespace fx = dc::test;
namespace fs = std::filesystem;

namespace {

constexpr int          kSePort = 12;
constexpr int          kFrames = 4800;
constexpr int          kOneSecond = 48000;
constexpr std::uint8_t kKey = 60;
constexpr std::uint8_t kId = 1;
constexpr std::uint8_t kOtherId = 2;

std::vector<std::uint8_t> Hd(const fx::BankSpec &spec = {}) {
    return fx::MakeHd(spec);
}

fx::BankSpec OctaveDown() {
    fx::BankSpec spec;
    spec.base_note = 72;
    return spec;
}

const std::vector<std::uint8_t> &Bd() {
    static const std::vector<std::uint8_t> body = fx::SquareVag(2);
    return body;
}

void LoadBank(int port, const std::vector<std::uint8_t> &hd) {
    SoundLoadHdBd(port, hd.data(), static_cast<int>(hd.size()), Bd().data(), static_cast<int>(Bd().size()));
}

int AddBank(int port, const std::vector<std::uint8_t> &hd) {
    return SoundLoadHdBdAdd(port, hd.data(), static_cast<int>(hd.size()), Bd().data(), static_cast<int>(Bd().size()));
}

void Play(int bank, std::uint8_t id = kId) {
    CSnd.SE_Play(kSePort, bank, 0, kKey, 64, 127, 127, 0x2000, id);
}

void Stop(int bank, std::uint8_t id = kId) {
    CSnd.SE_Stop(kSePort, bank, 0, kKey, id);
}

std::vector<float> Render(int frames) {
    return fx::RenderFrames(audio::DefaultMixer(), frames);
}

int Crossings() {
    return fx::ZeroCrossings(Render(kFrames));
}

// The expected count comes from a standalone mixer: the driver has no bank 0 measurement to compare
// against once the reload has run, and the same key-on on a fresh mixer is the reference bank 0 plays.
int ReferenceCrossings(const std::vector<std::uint8_t> &hd) {
    audio::Mixer mixer(audio::DefaultMixer().Rate());
    mixer.BindBank(kSePort, audio::Bank::Create(hd, Bd()));
    mixer.SetVolume(kSePort, audio::kFullPortVolume);
    mixer.ShortMessage(kSePort, 0xC0);
    const std::array<std::uint8_t, audio::kHsMsgBytes> key_on{0xFD, 0x10, 0x00, kKey, kId, 127, 0x00};
    mixer.HsMessage(kSePort, key_on);
    return fx::ZeroCrossings(fx::RenderFrames(mixer, kFrames));
}

} // namespace

TEST(Sound, SePlaysANoteAfterTheStep) {
    CSnd.Init(4, 0, 40, 0);
    LoadBank(kSePort, Hd());
    CSnd.SetVol(kSePort, 256);

    Play(0);
    EXPECT_EQ(fx::Rms(Render(kFrames)), 0.0);

    CSnd.Step();
    EXPECT_GT(fx::Rms(Render(kFrames)), 0.01);

    Stop(0);
    CSnd.Step();
    Render(kOneSecond);
    EXPECT_LT(fx::Rms(Render(kFrames)), 1e-3);
}

TEST(Sound, SeIsGatedOnALoadedBank) {
    CSnd.Init(4, 0, 40, 0);
    CSnd.SetVol(kSePort, 256);

    Play(0);
    CSnd.Step();
    EXPECT_EQ(fx::Rms(Render(kFrames)), 0.0);
}

TEST(Sound, SePitchRaisesTheVoice) {
    CSnd.Init(4, 0, 40, 0);
    LoadBank(kSePort, Hd());
    CSnd.SetVol(kSePort, 256);

    Play(0);
    CSnd.Step();
    const int before = Crossings();
    ASSERT_GT(before, 100);

    CSnd.SE_SetPitch(kSePort, 0, 0, kKey, 0x3FFF, kId);
    CSnd.Step();
    const int after = Crossings();

    const double expected = std::exp2(2.0 / 12.0);
    EXPECT_NEAR(static_cast<double>(after) / before, expected, 0.05 * expected);
}

TEST(Sound, AddedBankIsSelectedByTheBankNumber) {
    CSnd.Init(4, 0, 40, 0);
    LoadBank(kSePort, Hd());
    ASSERT_EQ(AddBank(kSePort, Hd(OctaveDown())), 0);
    CSnd.SetVol(kSePort, 256);

    Play(0, kId);
    CSnd.Step();
    const int low_count = Crossings();
    ASSERT_GT(low_count, 100);

    // The release tail is counted by ZeroCrossings too, so bank 1 is only measured once it has died away.
    Stop(0, kId);
    CSnd.Step();
    Render(kOneSecond);
    ASSERT_LT(fx::Rms(Render(kFrames)), 1e-3);

    Play(1, kOtherId);
    CSnd.Step();
    const int high_count = Crossings();

    EXPECT_NEAR(static_cast<double>(high_count) / low_count, 0.5, 0.03);
}

TEST(Sound, SequencePlaysThroughLoadSeqAndSqPlay) {
    CSnd.Init(4, 0, 40, 0);
    LoadBank(0, Hd());
    const auto sq = fx::MakeSq(60, 100, 960);
    ASSERT_EQ(SoundLoadSeq(0, sq.data(), static_cast<int>(sq.size())), 0);

    CSnd.SQ_Play(0, 0, 256);
    EXPECT_GT(fx::Rms(Render(kFrames)), 0.01);

    // Retail returns before stopping on a bad index, so the note keeps sounding through the call.
    CSnd.SQ_Play(0, 5, 256);
    EXPECT_GT(fx::Rms(Render(kFrames)), 0.01);
}

TEST(Sound, SecondLoadReplacesTheBanks) {
    CSnd.Init(4, 0, 40, 0);
    const auto first = Hd();
    LoadBank(kSePort, first);
    ASSERT_EQ(AddBank(kSePort, Hd(OctaveDown())), 0);
    LoadBank(kSePort, first);
    CSnd.SetVol(kSePort, 256);

    Play(1);
    CSnd.Step();

    EXPECT_EQ(Crossings(), ReferenceCrossings(first));
}

TEST(Sound, MasterVolumeScalesTheCore) {
    CSnd.Init(4, 0, 40, 0);
    // The reverb tail from the loud window would bleed into the half window and skew the ratio.
    CSnd.SetReverb(0, 0, 0);
    CSnd.SetReverb(1, 0, 0);
    LoadBank(kSePort, Hd());
    CSnd.SetVol(kSePort, 256);

    Play(0);
    CSnd.Step();
    // The exponential attack outlasts the first window, so both measured windows follow it.
    Render(kOneSecond);
    const double full = fx::Rms(Render(kFrames));
    ASSERT_GT(full, 0.01);

    CSnd.SetMasterVol(0, 0x3FFF / 2);
    CSnd.SetMasterVol(1, 0x3FFF / 2);
    const double half = fx::Rms(Render(kFrames));

    EXPECT_NEAR(half / full, 0.5, 0.03);
}

TEST(Sound, IntAddressLoadsAreRefused) {
    CSnd.Init(4, 0, 40, 0);
    CSnd.SetVol(kSePort, 256);

    CSnd.LoadHdBd(kSePort, 0, 0, 0, 0);
    CSnd.LoadHdBd2(kSePort, 0, 0, 0, 0);
    EXPECT_EQ(CSnd.LoadHdBdAdd(kSePort, 0, 0, 0, 0), -1);
    EXPECT_EQ(CSnd.LoadSeq(0, 0, 0), -1);

    // A bank loaded by the refused calls would make this note audible, so silence means nothing got loaded.
    Play(0);
    CSnd.Step();
    EXPECT_EQ(fx::Rms(Render(kFrames)), 0.0);
}

TEST(Sound, TransferIsAlwaysComplete) {
    CSnd.Init(4, 0, 40, 0);
    EXPECT_NE(CSnd.TransBdState(1), 0);
}

TEST(Sound, InitMngrStartsTheDriver) {
    sndInitMngr();
    sndSetMasterVol(0, 0.5f);
    sndSetMasterVol(1, 0.5f);

    sndInitMngr();
    EXPECT_FLOAT_EQ(audio::DefaultMixer().CoreVolume(0), 1.0f);
    EXPECT_FLOAT_EQ(audio::DefaultMixer().CoreVolume(1), 1.0f);
}

namespace {

constexpr int kStreamChannel = 1;
constexpr int kMaxPackFiles = 32;
constexpr int kBgmBlocks = 5 * kOneSecond / kFrames;
constexpr int kPlayingState = SND_STREAM_STATE_PLAYING;

struct alignas(16) Block16 {
    std::uint8_t bytes[16];
};

struct AlignedFile {
    std::vector<Block16> blocks;
    std::size_t          size = 0;

    u_int *words() { return reinterpret_cast<u_int *>(blocks.data()); }
};

struct PackFiles {
    int                                count = 0;
    std::array<u_int *, kMaxPackFiles> data{};
    std::array<int, kMaxPackFiles>     sizes{};
    std::array<char *, kMaxPackFiles>  names{};
};

std::optional<fs::path> ExtractedDataRoot() {
    fs::path dir = PathsExecutable().parent_path();
    for (int up = 0; up < 6 && !dir.empty(); up++, dir = dir.parent_path()) {
        std::error_code error;
        if (fs::is_regular_file(dir / "data" / "snd2" / "SY_000.snd", error)) {
            return dir / "data";
        }
    }
    return std::nullopt;
}

std::optional<AlignedFile> ReadAligned(const fs::path &path) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    std::ifstream        in(path, std::ios::binary);
    if (error || !in) {
        return std::nullopt;
    }

    AlignedFile file;
    file.size = static_cast<std::size_t>(size);
    file.blocks.resize((file.size + sizeof(Block16) - 1) / sizeof(Block16));
    in.read(reinterpret_cast<char *>(file.blocks.data()), static_cast<std::streamsize>(file.size));
    if (!in) {
        return std::nullopt;
    }
    return file;
}

PackFiles FindPackFiles(AlignedFile &pack, char *extension) {
    PackFiles found;
    found.count = GetPackFileExt(pack.words(), extension, found.data.data(), kMaxPackFiles, found.sizes.data(),
                                     found.names.data());
    return found;
}

std::span<const std::uint8_t> FileBytes(const PackFiles &pack, int index) {
    return {reinterpret_cast<const std::uint8_t *>(pack.data[index]), static_cast<std::size_t>(pack.sizes[index])};
}

void LoadSpans(int port, std::span<const std::uint8_t> hd, std::span<const std::uint8_t> bd) {
    SoundLoadHdBd(port, hd.data(), static_cast<int>(hd.size()), bd.data(), static_cast<int>(bd.size()));
}

// Swapped in only for the stream case, which resolves its names against the data root.
class DataRootScope {
public:
    explicit DataRootScope(const fs::path &root) : previous_(PathsDataRoot()) {
        PathsSetDataRoot(root);
    }

    ~DataRootScope() {
        PathsSetDataRoot(previous_);
    }

    DataRootScope(const DataRootScope &) = delete;
    DataRootScope &operator=(const DataRootScope &) = delete;

private:
    fs::path previous_;
};

struct StreamCloser {
    ~StreamCloser() {
        CSnd.StreamClose(kStreamChannel);
    }
};

} // namespace

TEST(SoundRealData, SystemPackBankParses) {
    const std::optional<fs::path> data = ExtractedDataRoot();
    if (!data) {
        GTEST_SKIP() << "no extracted data/ above the test executable";
    }

    std::optional<AlignedFile> pack = ReadAligned(*data / "snd2" / "SY_000.snd");
    ASSERT_TRUE(pack.has_value());

    char      hd_ext[] = "hd";
    char      bd_ext[] = "bd";
    PackFiles hd_files = FindPackFiles(*pack, hd_ext);
    PackFiles bd_files = FindPackFiles(*pack, bd_ext);
    ASSERT_EQ(hd_files.count, 1);
    ASSERT_EQ(bd_files.count, 1);

    const std::span<const std::uint8_t> hd = FileBytes(hd_files, 0);
    const std::span<const std::uint8_t> bd = FileBytes(bd_files, 0);
    const std::optional<audio::HdBank>  header = audio::HdBank::Parse(hd);
    ASSERT_TRUE(header.has_value());

    const std::shared_ptr<audio::Bank> bank = audio::Bank::Create(hd, bd);
    ASSERT_TRUE(bank != nullptr);

    int program = -1;
    int key = -1;
    for (int p = 0; p < static_cast<int>(header->programs.size()); p++) {
        const audio::HdProgram &candidate = header->programs[p];
        if (!candidate.present || candidate.splits.empty()) {
            continue;
        }
        program = p;
        // The split's key_low can sit outside the sample set's velocity range, so take the first key that sounds.
        const audio::HdSplit &split = candidate.splits.front();
        for (int k = split.key_low; k <= split.key_high && key < 0; k++) {
            std::array<audio::Layer, 8> layers{};
            if (bank->Resolve(p, k, 127, layers) > 0) {
                key = k;
            }
        }
        break;
    }
    ASSERT_GE(program, 0);
    ASSERT_GE(key, 0);
    std::printf("SY_000 program=%d key=%d\n", program, key);

    CSnd.Init(4, 0, 40, 0);
    LoadSpans(kSePort, hd, bd);
    CSnd.SE_Play(kSePort, 0, program, key, 64, 127, 127, 0x2000, kId);
    CSnd.SetVol(kSePort, 256);
    CSnd.Step();

    const double rms = fx::Rms(Render(9600));
    std::printf("SY_000 rms=%.6f\n", rms);
    EXPECT_GT(rms, 1e-3);
}

TEST(SoundRealData, EveryBgmPackParsesAndItsSequencePlays) {
    const std::optional<fs::path> data = ExtractedDataRoot();
    if (!data) {
        GTEST_SKIP() << "no extracted data/ above the test executable";
    }

    const fs::path bgm_dir = *data / "snd2" / "bgm";
    std::error_code error;
    if (!fs::is_directory(bgm_dir, error)) {
        GTEST_SKIP() << "no extracted data/snd2/bgm";
    }

    std::vector<fs::path> packs;
    for (const fs::directory_entry &entry : fs::directory_iterator(bgm_dir, error)) {
        if (entry.path().extension() == ".snd") {
            packs.push_back(entry.path());
        }
    }
    std::sort(packs.begin(), packs.end());
    ASSERT_FALSE(packs.empty());

    bool   played = false;
    double first_peak = 0.0;
    for (std::size_t i = 0; i < packs.size(); i++) {
        const std::string name = packs[i].filename().string();

        std::optional<AlignedFile> pack = ReadAligned(packs[i]);
        if (!pack) {
            ADD_FAILURE() << name << ": unreadable";
            continue;
        }

        char      hd_ext[] = "hd";
        char      bd_ext[] = "bd";
        char      sq_ext[] = "sq";
        PackFiles hd_files = FindPackFiles(*pack, hd_ext);
        PackFiles bd_files = FindPackFiles(*pack, bd_ext);
        PackFiles sq_files = FindPackFiles(*pack, sq_ext);
        if (hd_files.count == 0 || bd_files.count == 0 || sq_files.count == 0) {
            ADD_FAILURE() << name << ": hd=" << hd_files.count << " bd=" << bd_files.count
                          << " sq=" << sq_files.count;
            continue;
        }

        const std::span<const std::uint8_t> hd = FileBytes(hd_files, 0);
        const std::span<const std::uint8_t> bd = FileBytes(bd_files, 0);
        EXPECT_TRUE(audio::Bank::Create(hd, bd) != nullptr) << name << ": bank rejected";

        for (int s = 0; s < sq_files.count; s++) {
            const std::shared_ptr<audio::SqFile> sequence = audio::SqFile::Create(FileBytes(sq_files, s));
            EXPECT_TRUE(sequence != nullptr) << name << ": " << sq_files.names[s] << " rejected";
            EXPECT_TRUE(sequence && !sequence->Songs().empty()) << name << ": " << sq_files.names[s] << " has no song";
        }

        if (i != 0) {
            continue;
        }

        played = true;
        CSnd.Init(4, 0, 40, 0);
        LoadSpans(0, hd, bd);
        const std::span<const std::uint8_t> sq = FileBytes(sq_files, 0);
        ASSERT_EQ(SoundLoadSeq(0, sq.data(), static_cast<int>(sq.size())), 0) << name;

        CSnd.SQ_Play(0, 0, 256);
        // A sequence can open on rests, so the loudest block is the one that has to carry sound.
        for (int block = 0; block < kBgmBlocks; block++) {
            first_peak = std::max(first_peak, fx::Rms(Render(kFrames)));
        }
        EXPECT_GT(first_peak, 1e-3) << name;
    }

    std::printf("bgm packs=%zu first=%s max_block_rms=%.6f\n", packs.size(), packs[0].filename().string().c_str(),
                first_peak);
    EXPECT_TRUE(played);
}

TEST(SoundRealData, SoundDatEntryStreams) {
    const std::optional<fs::path> data = ExtractedDataRoot();
    if (!data) {
        GTEST_SKIP() << "no extracted data/ above the test executable";
    }
    std::error_code error;
    if (!fs::is_regular_file(*data / "sound" / "0010010.wav", error)) {
        GTEST_SKIP() << "no extracted data/sound/0010010.wav";
    }

    DataRootScope root(*data);
    StreamCloser  closer;
    CSnd.Init(4, 0, 40, 0);

    char name[] = "0010010.wav";
    CSnd.StreamOpenFast(kStreamChannel, name);
    CSnd.StreamStandBy(kStreamChannel);
    CSnd.StreamSetVol(kStreamChannel, 0x7FFF, 0x7FFF);
    CSnd.StreamPlay(kStreamChannel);

    EXPECT_EQ(CSnd.StreamGetState(kStreamChannel), kPlayingState);
    EXPECT_EQ(ezBgm(kStreamChannel | 0x80D0, 0), 0);
    EXPECT_GT(fx::Rms(Render(kOneSecond)), 1e-3);

    // The voice has silent stretches, so the level is sampled until it moves rather than over a fixed window.
    std::set<int> levels;
    for (int i = 0; i < 400 && levels.size() < 2; i++) {
        Render(kFrames);
        levels.insert(CSnd.StreamGetLevel(kStreamChannel));
    }
    EXPECT_GT(levels.size(), 1u);
}
