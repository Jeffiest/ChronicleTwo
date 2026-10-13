#pragma once

// False only when a device was requested and could not be opened. True when output runs, when a WAV
// capture is open, or when <prefix>_AUDIO=off.
bool AudioInit();

void AudioShutdown();
