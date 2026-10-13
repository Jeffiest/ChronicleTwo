#include "audio_init.hpp"

#include <cstring>

#include "audio/mixer.hpp"
#include "platform/audio.hpp"
#include "platform/settings.hpp"

namespace {

void RenderMixer(void *user, float *out, int frames) {
    // Mixer::Render takes the mixer's own lock, and every game-thread setter takes it too, so the
    // device thread needs no lock of its own here.
    static_cast<audio::Mixer *>(user)->Render(out, frames);
}

// AudioOutputStart returns false for "off" and for a failed device alike, so "off" is recognised
// here before the platform layer is asked. The WAV capture outranks "off" there and must here too.
bool OutputDisabled() {
    if (const char *wav = SettingsEnv("AUDIO_WAV"); wav != nullptr && *wav != '\0') {
        return false;
    }
    const char *setting = SettingsEnv("AUDIO");
    return setting != nullptr && std::strcmp(setting, "off") == 0;
}

} // namespace

bool AudioInit() {
    if (OutputDisabled()) {
        return true;
    }
    audio::Mixer &mixer = audio::DefaultMixer();
    return AudioOutputStart(mixer.Rate(), RenderMixer, &mixer);
}

void AudioShutdown() {
    AudioOutputStop();
}
