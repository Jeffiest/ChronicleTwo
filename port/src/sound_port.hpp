#pragma once

namespace audio {
class StreamChannel;
}

// Retail CSound passes bank and sequence addresses in int parameters; the host loads through these instead.
void SoundLoadHdBd(int port, const void *hd, int hd_size, const void *bd, int bd_size);
int  SoundLoadHdBdAdd(int port, const void *hd, int hd_size, const void *bd, int bd_size);
int  SoundLoadSeq(int port, const void *sq, int size);

// ezBgm's open commands carry the name's address in int; these take it as a pointer.
int EzBgmOpen(int channel, const char *name);
int EzBgmOpenFromPack(int channel, const char *name, const char *pack_name);
void EzBgmExit();

// Moves the stream channels on by one sound step when no audio output renders them.
void EzBgmStep();

audio::StreamChannel &EzBgmChannel(int channel);
