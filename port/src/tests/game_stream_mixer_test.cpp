#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "audio/mixer.hpp"
#include "audio/stream.hpp"

namespace {

TEST(GamePcmStream, IndependentBusRendersPausesResumesAndEndsAtItsFrames) {
    audio::StreamChannel game_stream;
    audio::Mixer mixer(48000);
    mixer.AddStreamChannel(&game_stream);
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
    game_stream.Open(track);
    game_stream.SetVolume(16384.0f / 32767.0f, 1.0f);
    game_stream.Play();

    float out[4]{};
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 0.25f + 8192.0f / 32768.0f, 0.002f);
    EXPECT_NEAR(out[1], -0.5f + 8192.0f / 32768.0f, 0.002f);
    EXPECT_GT(game_stream.Level(), 16000u);

    game_stream.Stop();
    EXPECT_NE(game_stream.State(), audio::StreamState::Playing);
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 8192.0f / 32768.0f, 0.002f);

    game_stream.Play();
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 8192.0f / 32768.0f + 4096.0f / 32768.0f, 0.002f);
    mixer.Render(out, 1);
    EXPECT_EQ(game_stream.State(), audio::StreamState::Ended);
    EXPECT_EQ(game_stream.Level(), 0u);
    EXPECT_TRUE(mixer.IsPlaying(0));  // The original port stream remains independent.
}

TEST(GamePcmStream, MasterVolumeStopCloseAndChannelIsolation) {
    audio::StreamChannel channel0;
    audio::StreamChannel channel1;
    audio::Mixer mixer(48000);
    mixer.AddStreamChannel(&channel0);
    mixer.AddStreamChannel(&channel1);
    auto track = std::make_shared<audio::PcmTrack>();
    track->rate = 48000;
    track->loop = false;
    track->samples = {16384, 16384, 16384, 16384};
    channel1.Open(track);
    mixer.SetMasterGain(0.25f);
    channel1.Play();
    float out[4]{};
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 0.125f, 0.002f);
    EXPECT_EQ(channel0.State(), audio::StreamState::Idle);
    EXPECT_EQ(channel1.State(), audio::StreamState::Playing);
    channel1.Stop();
    channel1.Play();
    mixer.Render(out, 1);
    EXPECT_NEAR(out[0], 0.125f, 0.002f);
    channel1.Close();
    channel1.Play();
    mixer.Render(out, 1);
    EXPECT_FLOAT_EQ(out[0], 0.0f);
    EXPECT_EQ(channel1.State(), audio::StreamState::Idle);
}

} // namespace
