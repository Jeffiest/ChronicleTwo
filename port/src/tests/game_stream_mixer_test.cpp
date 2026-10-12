#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "audio/mixer.hpp"

namespace {

TEST(GamePcmStream, IndependentBusRendersPausesResumesAndEndsAtItsFrames) {
    audio::Mixer mixer(48000);
    auto midi_track = std::make_shared<audio::PcmTrack>();
    midi_track->rate = 48000;
    midi_track->loop = true;
    midi_track->samples = {8192, 8192, 8192, 8192};
    mixer.SetStream(0, midi_track);
    mixer.SetVolume(0, audio::kFullPortVolume);
    mixer.Play(0);

    auto track = std::make_shared<audio::PcmTrack>();
    track->rate = 48000;
    track->loop = false;
    track->samples = {16384, -16384, 8192, -8192};
    mixer.SetGameStream(0, track);
    mixer.SetGameStreamVolume(0, 16384, 32767);
    mixer.PlayGameStream(0);

    float out[4]{};
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 0.25f + 8192.0f / 32768.0f, 0.002f);
    EXPECT_NEAR(out[1], -0.5f + 8192.0f / 32768.0f, 0.002f);
    EXPECT_GT(mixer.GameStreamLevel(0), 16000);

    mixer.PauseGameStream(0);
    EXPECT_FALSE(mixer.IsGameStreamPlaying(0));
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 8192.0f / 32768.0f, 0.002f);

    mixer.PlayGameStream(0);
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 8192.0f / 32768.0f + 4096.0f / 32768.0f, 0.002f);
    mixer.Render(out, 1);
    EXPECT_FALSE(mixer.IsGameStreamPlaying(0));
    EXPECT_EQ(mixer.GameStreamLevel(0), 0);
    EXPECT_TRUE(mixer.IsPlaying(0));  // The original port stream remains independent.
}

TEST(GamePcmStream, MasterVolumeStopCloseAndChannelIsolation) {
    audio::Mixer mixer(48000);
    auto track = std::make_shared<audio::PcmTrack>();
    track->rate = 48000;
    track->loop = false;
    track->samples = {16384, 16384, 16384, 16384};
    mixer.SetGameStream(1, track);
    mixer.SetMasterGain(0.25f);
    mixer.PlayGameStream(1);
    float out[4]{};
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 0.125f, 0.002f);
    EXPECT_FALSE(mixer.IsGameStreamPlaying(0));
    mixer.StopGameStream(1);
    mixer.PlayGameStream(1);
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 0.125f, 0.002f);
    mixer.StopGameStream(1, true);
    mixer.PlayGameStream(1);
    mixer.Render(out, 1);
    EXPECT_FLOAT_EQ(out[0], 0.0f);
}

} // namespace
