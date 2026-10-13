#include <algorithm>
#include <cassert>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "sound.hpp"
#include "audio/mixer.hpp"
#include "platform/paths.hpp"
#include "sound_stream_data.hpp"

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!std::filesystem::exists(std::filesystem::path(argv[1]) / "sound/sound.hd3")) {
        std::puts("SKIP: private extracted PAL assets are unavailable");
        return 77;
    }
    PathsSetDataRoot(PathsFromUtf8(argv[1]));
    const std::filesystem::path data_root = PathsFromUtf8(argv[1]);
    const auto has_voice_files = [&](const std::filesystem::path &directory) {
        return std::filesystem::exists(directory / "0010461.WAV") &&
               std::filesystem::exists(directory / "VOICE.VPK");
    };
    if (!has_voice_files(data_root / "V") && !has_voice_files(data_root / "v")) {
        std::puts("SKIP: extracted PAL assets do not include the V voice files required by this bridge test");
        return 77;
    }
    sound_stream_data::Index index;
    std::string error;
    assert(index.LoadIndex(PathsDataRoot(), error));
    const std::string clip = "0010461.wav";
    std::vector<int16_t> reference;
    uint32_t rate = 0;
    uint16_t channels = 0;
    if (!index.ReadPcm(clip, reference, rate, channels, error)) {
        if (error == "sound not found") {
            std::puts("SKIP: extracted PAL assets do not include the voice clip required by this bridge test");
            return 77;
        }
        std::fprintf(stderr, "could not read bridge-test voice clip: %s\n", error.c_str());
        return 1;
    }
    assert(rate > 0 && channels > 0 && !reference.empty());
    auto &mixer = audio::DefaultMixer();
    mixer.Reset();
    mixer.SetMasterGain(1.0f);
    CSound sound;
    std::string direct = "cdrom0:\\V\\0010461.WAV;1";
    std::string archive = "cdrom0:\\V\\VOICE.VPK;1";
    std::string member = clip;
    sound.StreamOpenFast(0, direct.data());
    sound.StreamOpenFromFPLFast(1, archive.data(), member.data());
    sound.StreamPlay(0);
    sound.StreamPlay(1);
    assert(sound.StreamGetState(0) == 0x1000 && sound.StreamGetState(1) == 0x1000);
    auto legacy = std::make_shared<audio::PcmTrack>();
    legacy->rate = mixer.Rate();
    legacy->loop = true;
    legacy->samples = {0, 0};
    mixer.SetStream(0, legacy);
    mixer.Play(0);
    const size_t frames = (reference.size() / channels * mixer.Rate() + rate - 1) / rate;
    std::vector<float> out(512 * 2);
    bool nonzero = false;
    for (size_t rendered = 0; rendered < frames + 512; rendered += 512) {
        mixer.Render(out.data(), 512);
        nonzero |= std::any_of(out.begin(), out.end(), [](float v) { return std::fabs(v) > 0.00001f; });
    }
    assert(nonzero);
    assert(sound.StreamGetState(0) == 0x8000 && sound.StreamGetState(1) == 0x8000);
    assert(mixer.IsPlaying(0));
    sound.StreamPlay(0);
    sound.StreamPlay(1);
    sound.StreamStop(0);
    assert(sound.StreamGetState(0) == 0 && sound.StreamGetState(1) == 0x1000);
    sound.StreamPlay(0);
    sound.Exit();
    assert(sound.StreamGetState(0) == 0 && sound.StreamGetState(1) == 0);
    sound.StreamPlay(0);
    sound.StreamPlay(1);
    assert(sound.StreamGetState(0) == 0 && sound.StreamGetState(1) == 0);
    sound.StreamOpenFast(0, direct.data());
    sound.StreamPlay(0);
    assert(sound.StreamGetState(0) == 0x1000);
    sound.StreamClose(0);
    assert(sound.StreamGetState(0) == 0);
    sound.Exit();
    std::printf("actual CSound bridge: %s, two buses, nonzero PCM, EOF/replay/stop/Exit/reopen passed\n", clip.c_str());
}
