#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "hdbank.hpp"
#include "reverb.hpp"
#include "sequencer.hpp"
#include "synth.hpp"

namespace audio {

constexpr int kPorts = 16;
constexpr int kChannels = 16;
constexpr int kFullPortVolume = 256;

// The whole MIDI player the IOP ran: sixteen ports, each with a bank, a sequence and sixteen
// channels, playing into one 48-voice synth and two effect units. Every method is safe to call
// from the game thread while the audio thread renders.
class Mixer {
public:
    explicit Mixer(int rate = kSpuRate);

    int Rate() const { return rate_; }

    void Reset();

    // Makes bank the port's selected bank, adding it to the port's list if new. Voices already
    // sounding keep playing: each holds the bank it started in.
    void BindBank(int port, std::shared_ptr<const Bank> bank);

    void SetSequence(int port, std::shared_ptr<const SqFile> file);

    void Rewind(int port, int song);

    void Play(int port);

    // Stops the sequence where it stands and releases every voice the port sounds.
    void Stop(int port);

    bool IsPlaying(int port) const;

    // 0 to 256, 256 at full scale.
    void SetVolume(int port, int volume);

    int Volume(int port) const;

    void SetAttribute(int port, int attribute);

    int Attribute(int port) const;

    void SetStereo(bool stereo);

    bool Stereo() const;

    void SetReverb(int core, int mode, int depth);

    void SetReverbEnabled(bool enabled);

    void SetMasterGain(float gain);

    void KeyOffCore(int core);

    // A short MIDI message packed status first, little-endian, as libmodmsin queues them.
    void ShortMessage(int port, std::uint32_t message);

    // A hardware-synth extended message: F9 sets the effect volume, pan or pitch the next key-on
    // takes, FD keys an effect on or off or changes the volume, pan or pitch of one already
    // sounding.
    void HsMessage(int port, std::span<const std::uint8_t> message);

    int ActiveVoices() const;

    // Renders frames of interleaved float stereo, advancing every sequence by as much.
    void Render(float *out, int frames);

private:
    struct Channel {
        int  program = 0;
        int  volume = 100;
        int  expression = 127;
        int  pan = 64;
        int  bend = 8192;
        int  bend_range = 2;
        bool sustain = false;
        int  rpn = 0x3FFF;
    };

    struct Port final : MidiSink {
        Mixer                                   *mixer = nullptr;
        int                                      index = 0;
        // Every bank bound to the port since its last Stop, the selected one named by bank_index.
        // A voice holds its own bank reference, so switching the selection never stops it.
        std::vector<std::shared_ptr<const Bank>> banks;
        int                                      bank_index = -1;
        Sequencer                                sequencer;
        std::array<Channel, kChannels>           channels;
        int                                      volume = 0;
        int                                      attribute = 0;
        int                                      effect_volume = 127;
        int                                      effect_pan = 64;
        int                                      effect_pitch = 8192;

        void ChannelMessage(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) override;
    };

    static constexpr int kMaxLayers = 8;
    static constexpr int kBlock = 256;

    void Message(Port &port, std::uint8_t status, std::uint8_t data1, std::uint8_t data2);

    const std::shared_ptr<const Bank> &SelectedBank(const Port &port) const;

    void Controller(Port &port, int channel, int controller, int value);

    void NoteOn(Port &port, int channel, int note, int velocity);

    void EffectOn(Port &port, int id, int number, int velocity);

    void StartLayer(Port &port, const Layer &layer, int note, int velocity, Voice &prototype);

    template <class Match, class Action>
    void ForVoices(Match match, Action action);

    // Releases the bank reference of every voice that ended, so a bank the game dropped does not
    // stay pinned until the voice is reused.
    void DropIdleBanks();

    void ReleaseVoice(Voice &voice);

    void UpdateVoices();

    int                        rate_;
    mutable std::mutex         mutex_;
    Synth                      synth_;
    std::array<Port, kPorts>   ports_;
    std::array<Reverb, kCores> reverbs_;
    bool                       stereo_ = true;
    bool                       reverb_enabled_ = true;
    float                      master_ = 1.0f;
    std::vector<float>         core_buffers_[kCores];
    std::uint64_t              rendered_ = 0;
};

// The mixer the game's sound driver plays through.
Mixer &DefaultMixer();

} // namespace audio
