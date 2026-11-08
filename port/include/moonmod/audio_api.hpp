#pragma once

#include <filesystem>
#include <memory>
#include "moonmod/runtime.hpp"

namespace audio {
class Mixer;
class PcmTrack;
class StreamChannel;
} // namespace audio

namespace moonmod {

class AudioApi {
public:
    explicit AudioApi(audio::Mixer &mixer);
    ~AudioApi();
    AudioApi(const AudioApi &) = delete;
    AudioApi &operator=(const AudioApi &) = delete;

    ApiCallResult Call(std::string_view mod_id, const std::filesystem::path &package_root,
                       const ApiOperationSpec &operation, const Json &arguments);
    void UnloadOwner(std::string_view mod_id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Binds the package-scoped audio API to the game's mixer and advertises its
// catalogued operations. It composes owner cleanup with any adapter cleanup.
void ConfigureAudioApi(RuntimeConfig &config);

} // namespace moonmod
