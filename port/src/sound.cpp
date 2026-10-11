#include "sound.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "audio/mixer.hpp"
#include "audio/trace.hpp"
#include "platform/audio.hpp"

// The IOP side of the sound driver - EZMIDI's sequencer, libhsyn's bank handling, the SPU2
// behind them and the EZBGM stream server - is the port's audio::Mixer: sixteen driver ports,
// each holding the banks and sequences the game loads, playing into one synth and two effect
// units. The game's own bookkeeping in snd_mngr.cpp is unchanged above this seam.
//
// The load methods below keep retail's int "addresses" (its 32-bit pointers); the host's file
// buffers come from 64-bit heaps and never fit, so snd_mngr.cpp calls the SndLoadHdBdHost /
// SndLoadHdBdAddHost / SndLoadSeqHost entry points with real pointers instead.

namespace {

constexpr int kDriverPorts = MIDI_PORT_COUNT;
constexpr int kStreamChannels = 2;
constexpr int kStreamPlaying = 0x1000;
constexpr int kStreamEnd = 0x8000;

// A streamed request plays for this long rather than for its file: the port has no stream
// decoder, and the game polls StreamGetState only to know that its voice has finished.
constexpr auto kStreamDuration = std::chrono::milliseconds(1500);

audio::Mixer &Player() {
    return audio::DefaultMixer();
}

void RenderOutput(void *, float *out, int frames) {
    audio::DefaultMixer().Render(out, frames);
}

// One sequence as the driver received it, with the bank that was current on the port when it
// arrived: the IOP played a port's sequences against the banks loaded beside them.
struct LoadedSequence {
    std::shared_ptr<const audio::SqFile> file;
    std::shared_ptr<const audio::Bank>   bank;
};

struct DriverPort {
    std::vector<std::shared_ptr<const audio::Bank>> banks;
    std::vector<LoadedSequence>                     sequences;
    std::shared_ptr<const audio::Bank>              last_bank;
    int                                             bound_bank = -1;
};

DriverPort g_ports[kDriverPorts];

// Retail links the event port's play and sequence ports (14 and 2) so a bank loaded into
// either is usable from both; the mixer holds one bank per port, and this link is how an
// event sequence finds the bank its pack loaded.
int g_linked_port[kDriverPorts] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

struct StreamChannel {
    bool                                   opened = false;
    bool                                   ended = false;
    bool                                   playing = false;
    std::chrono::steady_clock::time_point started;
};

StreamChannel g_streams[kStreamChannels];

// Retail's bank and sequence loaders take their buffers as int, as its 32-bit pointers were.
// Nothing in the port calls the CSound methods with real data any more, but they stay correct
// for any address that does fit in 32 bits.
const std::uint8_t *FromInt(int address) {
    return reinterpret_cast<const std::uint8_t *>(
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(address)));
}

std::shared_ptr<const audio::Bank> MakeBank(const void *hd, int hd_size, const void *bd, int bd_size) {
    if (bd_size == 0 || bd == nullptr) {
        std::printf("BD LOAD Err!!!!!!!!!!!!!!!!!!!!!\n");
        return nullptr;
    }
    if (hd_size == 0 || hd == nullptr) {
        std::printf("HD LOAD Err!!!!!!!!!!!!!!!!!!!!!\n");
        return nullptr;
    }
    auto bank = audio::Bank::Create(
        {static_cast<const std::uint8_t *>(hd), static_cast<std::size_t>(hd_size)},
        {static_cast<const std::uint8_t *>(bd), static_cast<std::size_t>(bd_size)});
    if (bank == nullptr) {
        std::printf("sound: unreadable HD bank (%d bytes)\n", hd_size);
        return nullptr;
    }
    const audio::HdBank &header = bank->Header();
    audio::Trace("bank: hd %d bytes, bd %d bytes: %zu programs, %zu sample sets, %zu samples, %zu vags", hd_size,
                 bd_size, header.programs.size(), header.sample_sets.size(), header.samples.size(),
                 header.vags.size());
    return bank;
}

// Makes the bank a message names current on its driver port: the mixer resolves a program
// change against the bank bound to the port, where the IOP held every bank loaded beside each
// other and picked one by bank select.
bool PrepareEffectPort(int port, int bank, int program) {
    if (port < 0 || port >= kDriverPorts) {
        return false;
    }
    DriverPort &state = g_ports[port];
    if (state.banks.empty() || bank < 0 || bank >= static_cast<int>(state.banks.size())) {
        return false;
    }
    if (state.bound_bank != bank) {
        Player().BindBank(port, state.banks[bank]);
        state.bound_bank = bank;
    }
    Player().ShortMessage(port, ((bank & 0x7F) << 16) | 0xB0);
    Player().ShortMessage(port, ((program & 0x7F) << 8) | 0xC0);
    return true;
}

void OpenStream(int channel, const char *name, const char *pack_name) {
    if (channel < 0 || channel >= kStreamChannels) {
        return;
    }
    StreamChannel &state = g_streams[channel];
    state = StreamChannel{};
    state.opened = true;
    audio::Trace("stream %d: open %s%s%s (no host stream decoder: silent)", channel, name != nullptr ? name : "",
                 pack_name != nullptr ? " in pack " : "", pack_name != nullptr ? pack_name : "");
}

} // namespace

// The sound manager's host entry points, declared in snd_mngr.cpp.

void SndLoadHdBdHost(int port, const void *hd, int hd_size, const void *bd, int bd_size) {
    if (port < 0 || port >= kDriverPorts) {
        return;
    }
    const int linked = g_linked_port[port];

    // The first bank of a port replaces every bank and sequence it and its linked port held.
    Player().Stop(port);
    g_ports[port].banks.clear();
    g_ports[port].sequences.clear();
    g_ports[port].bound_bank = -1;
    if (linked >= 0) {
        Player().Stop(linked);
        g_ports[linked].banks.clear();
        g_ports[linked].sequences.clear();
        g_ports[linked].bound_bank = -1;
    }

    auto bank = MakeBank(hd, hd_size, bd, bd_size);
    if (bank == nullptr) {
        return;
    }
    audio::Trace("load bank: port %d, hd %d bytes, bd %d bytes", port, hd_size, bd_size);
    g_ports[port].banks.push_back(bank);
    g_ports[port].last_bank = bank;
    if (linked >= 0) {
        g_ports[linked].banks.push_back(bank);
        g_ports[linked].last_bank = bank;
    }
}

int SndLoadHdBdAddHost(int port, const void *hd, int hd_size, const void *bd, int bd_size) {
    if (port < 0 || port >= kDriverPorts) {
        return 0;
    }
    if (static_cast<int>(g_ports[port].banks.size()) > MIDI_PORT_BANK_MAX - 1) {
        std::printf("############################################################Bank MAX OVER!  \n");
        return 0;
    }
    auto bank = MakeBank(hd, hd_size, bd, bd_size);
    if (bank == nullptr) {
        return 0;
    }
    audio::Trace("add bank: port %d, hd %d bytes, bd %d bytes", port, hd_size, bd_size);
    g_ports[port].banks.push_back(bank);
    g_ports[port].last_bank = bank;
    const int linked = g_linked_port[port];
    if (linked >= 0) {
        g_ports[linked].banks.push_back(bank);
        g_ports[linked].last_bank = bank;
    }
    return 0;
}

int SndLoadSeqHost(int port, const void *sequence, int size) {
    if (port < 0 || port >= kDriverPorts) {
        return -1;
    }
    auto file = sequence != nullptr && size > 0
                    ? audio::SqFile::Create(
                          {static_cast<const std::uint8_t *>(sequence), static_cast<std::size_t>(size)})
                    : nullptr;
    if (file == nullptr) {
        std::printf("sound: unreadable SQ sequence (%d bytes)\n", size);
    } else {
        audio::Trace("sequence: port %d #%zu, %d bytes, %zu songs", port, g_ports[port].sequences.size(), size,
                     file->Songs().size());
    }
    g_ports[port].sequences.push_back({file, g_ports[port].last_bank});
    return 0;
}

// ---------------------------------------------------------------------------------------------

void *iop_bd_addr;

void set_spu(int mode0, int mode1, int depth0, int depth1) {
    Player().SetReverbEnabled(true);
    Player().SetReverb(0, mode0, depth0);
    Player().SetReverb(1, mode1, depth1);
}

int TransHdBd(int hd, int hd_size, int bd, int bd_size) {
    // Retail stages the bank body into iop_bd_addr for its loads; the port's load entry points
    // build the mixer's bank from the buffers directly, and nothing calls this.
    (void) hd;
    (void) hd_size;
    (void) bd;
    (void) bd_size;
    return 0;
}

void CSound::StopVoice(int core) {
    audio::Trace("StopVoice core %d", core);
    Player().KeyOffCore(core);
}

void CSound::SndInReverb(bool enable) {
    audio::Trace("SndInReverb %d", enable);
    Player().SetReverbEnabled(enable);
}

void CSound::SetReverb(int core, int mode, int depth) {
    audio::Trace("SetReverb core %d mode %d depth %d", core, mode, depth);
    Player().SetReverb(core, mode, depth);
}

int CSound::Init(int mode0, int mode1, int depth0, int depth1) {
    static bool output_started = false;
    if (!output_started) {
        output_started = true;
        if (!AudioOutputStart(Player().Rate(), RenderOutput, nullptr)) {
            std::printf("sound: no audio output, playing silently\n");
        }
    }

    Player().Reset();
    for (int port = 0; port < kDriverPorts; port++) {
        g_ports[port] = DriverPort{};
        g_linked_port[port] = -1;
    }
    g_linked_port[2] = 14;
    g_linked_port[14] = 2;
    for (StreamChannel &channel : g_streams) {
        channel = StreamChannel{};
    }
    set_spu(mode0, mode1, depth0, depth1);
    return 0;
}

int CSound::Exit() {
    for (int port = 0; port < kDriverPorts; port++) {
        Player().Stop(port);
        g_ports[port] = DriverPort{};
    }
    return 0;
}

void CSound::DEL_PORT(int port) {
    if (port < 0 || port >= kDriverPorts) {
        return;
    }
    audio::Trace("DEL_PORT %d", port);
    Player().Stop(port);
    g_ports[port] = DriverPort{};
    const int linked = g_linked_port[port];
    if (linked >= 0) {
        Player().Stop(linked);
        g_ports[linked] = DriverPort{};
    }
}

void CSound::SQ_Play(int port, int seq_no, int volume) {
    if (port < 0 || port >= kDriverPorts) {
        return;
    }
    DriverPort &state = g_ports[port];
    if (seq_no < 0 || seq_no >= static_cast<int>(state.sequences.size())) {
        std::printf("###############NOT FOUND SEQ_NO=%d #####################\n", seq_no);
        return;
    }
    if (volume != 256) {
        volume = static_cast<int>(2.015748f * volume);
    }
    const LoadedSequence &sequence = state.sequences[seq_no];
    if (sequence.file == nullptr) {
        return;
    }
    audio::Trace("MIDI start! port=%d seq=%d volume=%d", port, seq_no, volume);
    Player().Stop(port);
    if (sequence.bank != nullptr) {
        Player().BindBank(port, sequence.bank);
        state.bound_bank = -1;
    }
    Player().SetSequence(port, sequence.file);
    Player().SetVolume(port, volume);
    Player().Rewind(port, 0);
    Player().Play(port);
}

void CSound::SQ_RePlay(int port) {
    if (port < 0 || port >= kDriverPorts || g_ports[port].sequences.empty()) {
        return;
    }
    audio::Trace("MIDI restart! port=%d", port);
    Player().Play(port);
}

void CSound::SE_Play(int port, int bank, int program, int key, int pan, int velocity, int volume, int pitch, int id) {
    if (id > 0x7E) {
        std::printf(" ################################SE_ID ERR!! PORT NO=%d !!!\n", port);
        return;
    }
    if (!PrepareEffectPort(port, bank, program)) {
        return;
    }
    audio::Trace("SE_Play port %d bank %d program %d key %d pan %d velocity %d volume %d pitch %d id %d", port, bank,
                 program, key, pan, velocity, volume, pitch, id);

    const std::array<std::uint8_t, 5> volume_message = {0xF9, 0, 0, static_cast<std::uint8_t>(volume), 0};
    Player().HsMessage(port, volume_message);
    // Retail builds the F9 2 pitch message here but never sends it, and the mixer's effects
    // have no pitch stage.
    const std::array<std::uint8_t, 5> pan_message = {0xF9, 1, 0, static_cast<std::uint8_t>(pan), 0};
    Player().HsMessage(port, pan_message);
    const std::array<std::uint8_t, 7> key_on = {0xFD,
                                                0x10,
                                                0,
                                                static_cast<std::uint8_t>(key),
                                                static_cast<std::uint8_t>(id),
                                                static_cast<std::uint8_t>(velocity),
                                                0};
    Player().HsMessage(port, key_on);
}

void CSound::SE_SetVol(int port, int bank, int program, int key, int volume, int id) {
    if (id > 0x7E) {
        std::printf(" ################################SE_ID ERR!! PORT NO=%d !!!\n", port);
        return;
    }
    if (!PrepareEffectPort(port, bank, program)) {
        return;
    }
    const std::array<std::uint8_t, 7> message = {0xFD,
                                                 0,
                                                 0,
                                                 static_cast<std::uint8_t>(key),
                                                 static_cast<std::uint8_t>(id),
                                                 static_cast<std::uint8_t>(volume),
                                                 0};
    Player().HsMessage(port, message);
}

void CSound::SE_SetPan(int port, int bank, int program, int key, int pan, int id) {
    if (id > 0x7E) {
        std::printf(" ################################SE_ID ERR!! PORT NO=%d !!!\n", port);
        return;
    }
    if (!PrepareEffectPort(port, bank, program)) {
        return;
    }
    const std::array<std::uint8_t, 7> message = {0xFD,
                                                 1,
                                                 0,
                                                 static_cast<std::uint8_t>(key),
                                                 static_cast<std::uint8_t>(id),
                                                 static_cast<std::uint8_t>(pan),
                                                 0};
    Player().HsMessage(port, message);
}

void CSound::SE_Stop(int port, int bank, int program, int key, int id) {
    if (id > 0x7E) {
        std::printf(" ################################SE_ID ERR!! PORT NO=%d !!!\n", port);
        return;
    }
    if (!PrepareEffectPort(port, bank, program)) {
        return;
    }
    const std::array<std::uint8_t, 7> message = {0xFD,
                                                 0x10,
                                                 0,
                                                 static_cast<std::uint8_t>(key),
                                                 static_cast<std::uint8_t>(id),
                                                 0,
                                                 0};
    Player().HsMessage(port, message);
}

void CSound::Step() {
    // The IOP's once-a-frame work: the queued MIDI stream buffers went to the IOP and the
    // ports' fades advanced. The mixer takes sequencer events and effect messages as they are
    // posted and no game code sets the retail fade state, so nothing is left to do here.
}

void CSound::Stop(int port) {
    audio::Trace("Stop port %d", port);
    Player().Stop(port);
}

void CSound::SetVol(int port, int volume) {
    if (volume != 256) {
        volume = static_cast<int>(volume * 2.015748f);
    }
    Player().SetVolume(port, volume);
}

void CSound::SetStereoMode(int mode) {
    audio::Trace("SetStereoMode %d", mode);
    Player().SetStereo(mode != 0);
}

void CSound::SetMasterVol(int core, int volume) {
    if (core < 0 || core > 1) {
        return;
    }
    if (volume < 0) {
        volume = 0;
    }
    if (volume > 0x3FFF) {
        volume = 0x3FFF;
    }
    // The synth's two cores share the mixer's one master gain; fades run on both cores with
    // the same value, so the last call is the right one.
    Player().SetMasterGain(volume / 16383.0f);
}

void CSound::LoadHdBd(int port, int hd, int hd_size, int bd, int bd_size) {
    LoadHdBd2(port, hd, hd_size, bd, bd_size);
}

void CSound::LoadHdBd2(int port, int hd, int hd_size, int bd, int bd_size) {
    SndLoadHdBdHost(port, FromInt(hd), hd_size, FromInt(bd), bd_size);
}

int CSound::LoadHdBdAdd(int port, int hd, int hd_size, int bd, int bd_size) {
    return SndLoadHdBdAddHost(port, FromInt(hd), hd_size, FromInt(bd), bd_size);
}

int CSound::LoadSeq(int port, int address, int size) {
    return SndLoadSeqHost(port, FromInt(address), size);
}

void CSound::SE_SetPitch(int port, int bank, int program, int key, int pitch, int id) {
    if (id > 0x7E) {
        std::printf(" ################################SE_ID ERR!! PORT NO=%d !!!\n", port);
        return;
    }
    if (!PrepareEffectPort(port, bank, program)) {
        return;
    }
    // The mixer ignores the FD 2 pitch message; it is sent for the command stream's sake.
    const std::array<std::uint8_t, 7> message = {0xFD,
                                                 2,
                                                 0,
                                                 static_cast<std::uint8_t>(key),
                                                 static_cast<std::uint8_t>(id),
                                                 static_cast<std::uint8_t>(pitch & 0x7F),
                                                 static_cast<std::uint8_t>((pitch >> 7) & 0x7F)};
    Player().HsMessage(port, message);
}

void CSound::StreamOpenFast(int channel, char *name) {
    OpenStream(channel, name, nullptr);
}

void CSound::StreamOpenFromFPLFast(int channel, char *name, char *pack_name) {
    OpenStream(channel, name, pack_name);
}

void CSound::StreamPlay(int channel) {
    if (channel < 0 || channel >= kStreamChannels) {
        return;
    }
    StreamChannel &state = g_streams[channel];
    if (!state.opened) {
        return;
    }
    state.playing = true;
    state.ended = false;
    state.started = std::chrono::steady_clock::now();
    audio::Trace("stream %d: play (silent)", channel);
}

void CSound::StreamStop(int channel) {
    if (channel < 0 || channel >= kStreamChannels) {
        return;
    }
    g_streams[channel].playing = false;
}

void CSound::StreamClose(int channel) {
    if (channel < 0 || channel >= kStreamChannels) {
        return;
    }
    g_streams[channel] = StreamChannel{};
}

void CSound::StreamEND(int channel) {
    if (channel < 0 || channel >= kStreamChannels) {
        return;
    }
    g_streams[channel] = StreamChannel{};
}

void CSound::StreamPause(int channel) {
    if (channel < 0 || channel >= kStreamChannels) {
        return;
    }
    g_streams[channel].playing = false;
}

void CSound::StreamRePlay(int channel) {
    if (channel < 0 || channel >= kStreamChannels) {
        return;
    }
    StreamChannel &state = g_streams[channel];
    if (state.opened && !state.ended) {
        state.playing = true;
        state.started = std::chrono::steady_clock::now();
    }
}

void CSound::StreamSetVol(int channel, int left, int right) {
    audio::Trace("stream %d: set volume %d %d", channel, left, right);
}

int CSound::StreamGetState(int channel) {
    if (channel < 0 || channel >= kStreamChannels) {
        return 0;
    }
    StreamChannel &state = g_streams[channel];
    if (!state.opened || (!state.playing && !state.ended)) {
        return 0;
    }
    if (state.ended) {
        return kStreamEnd;
    }
    if (std::chrono::steady_clock::now() - state.started >= kStreamDuration) {
        state.playing = false;
        state.ended = true;
        return kStreamEnd;
    }
    return kStreamPlaying;
}

int CSound::StreamGetLevel(int channel) {
    (void) channel;
    return 0;
}

void CSound::StreamStandBy(int channel) {
    if (channel < 0 || channel >= kStreamChannels) {
        return;
    }
    StreamChannel &state = g_streams[channel];
    if (state.opened) {
        state.playing = false;
        state.ended = false;
    }
}

int CSound::TransBdState(int channel) {
    // Wave transfers are synchronous here; the game's sndWaitTransBd always sees them done.
    (void) channel;
    return 1;
}

int CSound::StreamOpenState() {
    return 0;
}
