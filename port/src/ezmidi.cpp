#include "common.h"

#include "ezmidi.hpp"
#include "audio/mixer.hpp"
#include "audio/trace.hpp"

// Only the scalar commands reach here: the address-carrying ones are served by the host's own paths.
PORT_OVERRIDE int ezMidiInit() {
    return 1;
}

PORT_OVERRIDE int ezMidi(int command, int argument) {
    audio::Mixer &mixer = audio::DefaultMixer();
    const int     port = command & 0x0F;

    switch (command & ~0x0F) {
        case 0x00:
            mixer.Play(port);
            return 0;
        case 0x20:
            mixer.Stop(port);
            return 0;
        case 0x30:
            mixer.Rewind(port, argument);
            return 0;
        case 0xA0:
            mixer.SetAttribute(port, argument);
            return 0;
        case 0xB0:
            mixer.SetVolume(port, argument);
            return 0;
        case 0xC0:
            mixer.SetStereo(argument != 0);
            return 0;
        case 0x8090:
            return mixer.IsPlaying(port) ? 1 : 0;
        case 0x80E0:
            return mixer.Volume(port);
        default:
            audio::Trace("ezmidi: command 0x%x argument 0x%x has no host equivalent", command, argument);
            return 0;
    }
}
