#include "moonmod/audio_api.hpp"
#include "moonmod/symlinks.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <ranges>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "audio/mixer.hpp"
#include "audio/pcm.hpp"
#include "audio/stream.hpp"

namespace moonmod {
namespace {

constexpr std::size_t kAudioTrackFileLimit = 16 * 1024 * 1024;
constexpr std::size_t kAudioTrackDecodedLimit = 32 * 1024 * 1024;
constexpr std::size_t kAudioOwnerDecodedLimit = 64 * 1024 * 1024;
constexpr std::size_t kAudioGlobalDecodedLimit = 256 * 1024 * 1024;
constexpr std::size_t kAudioOwnerSoundLimit = 32;
constexpr std::size_t kAudioGlobalSoundLimit = 512;
constexpr std::size_t kAudioOwnerVoiceLimit = 8;
constexpr std::size_t kAudioGlobalVoiceLimit = 128;

ApiCallResult Invalid(std::string message) {
    return {ApiCallStatus::InvalidArgument, nullptr, std::move(message)};
}

ApiCallResult Failed(std::string message) {
    return {ApiCallStatus::Failed, nullptr, std::move(message)};
}

bool ValidId(std::string_view value) {
    return !value.empty() && value.size() <= 64 &&
           std::ranges::all_of(value, [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-' || c == '.';
           });
}

bool ReadObjectArgument(const Json &arguments, Json &object, std::string &error) {
    if (!arguments.is_array() || arguments.size() != 1 || !arguments[0].is_object()) {
        error = "audio operation requires one object argument";
        return false;
    }
    object = arguments[0];
    return true;
}

bool HasOnlyFields(const Json &object, std::initializer_list<std::string_view> allowed, std::string &error) {
    for (auto field = object.begin(); field != object.end(); ++field) {
        if (std::ranges::find(allowed, field.key()) == allowed.end()) {
            error = "unknown field '" + field.key() + "'";
            return false;
        }
    }
    return true;
}

std::string QualifiedId(std::string_view owner, std::string_view local_id) {
    return std::string(owner) + ":" + std::string(local_id);
}

bool ResolveAudioPath(const std::filesystem::path &package_root, std::string_view relative,
                      std::filesystem::path &resolved, std::size_t &file_size, std::string &error) {
    if (relative.empty() || relative.size() > 240 || relative.find('\\') != std::string_view::npos ||
        relative.find(':') != std::string_view::npos) {
        error = "audio path must be a package-relative forward-slash path";
        return false;
    }
    const std::filesystem::path relative_path{std::string(relative)};
    if (relative_path.is_absolute() || relative_path.has_root_name() || relative_path.has_root_directory()) {
        error = "audio path must be relative to the mod package";
        return false;
    }
    std::vector<std::string> parts;
    for (const auto &part : relative_path) {
        const std::string component = part.generic_string();
        if (component.empty() || component == "." || component == "..") {
            error = "audio path contains an invalid path component";
            return false;
        }
        parts.push_back(component);
    }
    if (parts.size() < 3 || parts[0] != "resources" || parts[1] != "audio") {
        error = "audio files must be stored under resources/audio/";
        return false;
    }
    std::string extension = relative_path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension != ".wav" && extension != ".flac" && extension != ".ogg") {
        error = "audio path must use a .wav, .flac, or .ogg file";
        return false;
    }
    if (moonmod::ContainsSymlinkBelow(package_root, package_root.parent_path())) {
        error = "mod package path contains a symbolic link";
        return false;
    }
    std::error_code root_error;
    const std::filesystem::path root = std::filesystem::canonical(package_root, root_error);
    if (root_error || root.empty()) {
        error = "mod package directory is unavailable";
        return false;
    }
    const std::filesystem::path candidate = package_root / relative_path;
    if (moonmod::ContainsSymlinkBelow(candidate, package_root)) {
        error = "audio path contains a symbolic link";
        return false;
    }
    std::error_code filesystem_error;
    resolved = std::filesystem::canonical(candidate, filesystem_error);
    if (filesystem_error || !std::filesystem::is_regular_file(resolved, filesystem_error) || filesystem_error) {
        error = "audio resource does not name a readable file";
        return false;
    }
    const std::filesystem::path within_package = resolved.lexically_relative(root);
    if (within_package.empty() || within_package.is_absolute() ||
        (within_package.begin() != within_package.end() && *within_package.begin() == "..")) {
        error = "audio path resolves outside the mod package";
        return false;
    }
    const std::uintmax_t size = std::filesystem::file_size(resolved, filesystem_error);
    if (filesystem_error || size == 0 || size > kAudioTrackFileLimit) {
        error = "audio resource must be non-empty and no larger than 16 MiB";
        return false;
    }
    file_size = static_cast<std::size_t>(size);
    return true;
}

} // namespace

struct AudioApi::Impl {
    struct Sound {
        std::string owner;
        std::string local_id;
        std::filesystem::path path;
        std::shared_ptr<const audio::PcmTrack> track;
        std::size_t decoded_bytes = 0;
    };

    struct Voice {
        std::string owner;
        std::string sound_id;
        std::unique_ptr<audio::StreamChannel> channel;
    };

    explicit Impl(audio::Mixer &game_mixer) : mixer(game_mixer) {}

    void RemoveVoice(std::unordered_map<std::string, Voice>::iterator voice) {
        mixer.RemoveStreamChannel(voice->second.channel.get());
        voice->second.channel->Stop();
        voice->second.channel->Close();
        voices.erase(voice);
    }

    void RemoveEndedVoices() {
        for (auto voice = voices.begin(); voice != voices.end();) {
            if (voice->second.channel->State() == audio::StreamState::Ended) {
                auto ended = voice++;
                RemoveVoice(ended);
            } else {
                ++voice;
            }
        }
    }

    std::size_t OwnerSoundCount(std::string_view owner) const {
        return static_cast<std::size_t>(std::ranges::count_if(sounds, [owner](const auto &entry) {
            return entry.second.owner == owner;
        }));
    }

    std::size_t OwnerVoiceCount(std::string_view owner) const {
        return static_cast<std::size_t>(std::ranges::count_if(voices, [owner](const auto &entry) {
            return entry.second.owner == owner;
        }));
    }

    std::size_t TotalDecodedBytes() const {
        std::size_t total = 0;
        for (const auto &[id, sound] : sounds) {
            (void)id;
            total += sound.decoded_bytes;
        }
        return total;
    }

    audio::Mixer &mixer;
    mutable std::mutex mutex;
    std::map<std::string, Sound, std::less<>> sounds;
    std::unordered_map<std::string, Voice> voices;
    std::unordered_map<std::string, std::size_t> owner_bytes;
    std::uint64_t next_handle = 1;
};

AudioApi::AudioApi(audio::Mixer &mixer) : impl_(std::make_unique<Impl>(mixer)) {}

AudioApi::~AudioApi() {
    if (!impl_) return;
    std::lock_guard lock(impl_->mutex);
    while (!impl_->voices.empty()) impl_->RemoveVoice(impl_->voices.begin());
}

ApiCallResult AudioApi::Call(std::string_view mod_id, const std::filesystem::path &package_root,
                             const ApiOperationSpec &operation, const Json &arguments) {
    if (operation.path == "audio.register") {
        Json definition;
        std::string error;
        if (!ReadObjectArgument(arguments, definition, error)) return Invalid(std::move(error));
        if (!HasOnlyFields(definition, {"id", "path"}, error)) return Invalid(std::move(error));
        const auto id_field = definition.find("id");
        const auto path_field = definition.find("path");
        if (id_field == definition.end() || !id_field->is_string() || path_field == definition.end() ||
            !path_field->is_string()) {
            return Invalid("audio.register requires string fields 'id' and 'path'");
        }
        const std::string local_id = id_field->get<std::string>();
        const std::string relative_path = path_field->get<std::string>();
        if (!ValidId(local_id)) return Invalid("audio id must use 1-64 letters, digits, '.', '_' or '-'");

        std::filesystem::path resolved;
        std::size_t file_size = 0;
        if (!ResolveAudioPath(package_root, relative_path, resolved, file_size, error)) return Invalid(std::move(error));
        (void)file_size;
        std::string decode_error;
        std::shared_ptr<audio::PcmTrack> track = audio::LoadPcmTrack(resolved, decode_error);
        if (!track) return Invalid("audio resource could not be decoded: " + decode_error);
        const std::size_t decoded_bytes = track->samples.size() * sizeof(std::int16_t);
        if (track->rate < 8000 || track->rate > 192000 || track->Frames() <= 0 ||
            decoded_bytes > kAudioTrackDecodedLimit) {
            return Invalid("decoded audio must be 8-192 kHz and no larger than 32 MiB");
        }

        std::lock_guard lock(impl_->mutex);
        const std::string key = QualifiedId(mod_id, local_id);
        if (impl_->sounds.contains(key)) return Invalid("audio id is already registered by this mod");
        if (impl_->sounds.size() >= kAudioGlobalSoundLimit || impl_->OwnerSoundCount(mod_id) >= kAudioOwnerSoundLimit) {
            return Invalid("audio registration limit reached");
        }
        std::size_t current_owner_bytes = 0;
        if (const auto found = impl_->owner_bytes.find(std::string(mod_id)); found != impl_->owner_bytes.end()) {
            current_owner_bytes = found->second;
        }
        if (decoded_bytes > kAudioOwnerDecodedLimit - std::min(current_owner_bytes, kAudioOwnerDecodedLimit) ||
            decoded_bytes > kAudioGlobalDecodedLimit - std::min(impl_->TotalDecodedBytes(), kAudioGlobalDecodedLimit)) {
            return Invalid("decoded audio memory limit reached");
        }
        impl_->owner_bytes[std::string(mod_id)] = current_owner_bytes + decoded_bytes;
        impl_->sounds.emplace(key, Impl::Sound{std::string(mod_id), local_id, resolved, std::move(track), decoded_bytes});
        return {ApiCallStatus::Ok,
                {{"id", local_id}, {"qualified_id", key}, {"path", relative_path},
                 {"rate", impl_->sounds.at(key).track->rate}, {"frames", impl_->sounds.at(key).track->Frames()}},
                {}};
    }

    if (operation.path == "audio.play") {
        Json request;
        std::string error;
        if (!ReadObjectArgument(arguments, request, error)) return Invalid(std::move(error));
        if (!HasOnlyFields(request, {"id", "loop", "volume", "pan"}, error)) return Invalid(std::move(error));
        const auto id_field = request.find("id");
        if (id_field == request.end() || !id_field->is_string() || !ValidId(id_field->get_ref<const std::string &>())) {
            return Invalid("audio.play requires a valid local audio id");
        }
        bool loop = false;
        if (const auto field = request.find("loop"); field != request.end()) {
            if (!field->is_boolean()) return Invalid("audio.play field 'loop' must be boolean");
            loop = field->get<bool>();
        }
        double volume = 1.0;
        if (const auto field = request.find("volume"); field != request.end()) {
            if (!field->is_number()) return Invalid("audio.play field 'volume' must be a number in [0, 1]");
            volume = field->get<double>();
            if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0) {
                return Invalid("audio.play field 'volume' must be in [0, 1]");
            }
        }
        double pan = 0.0;
        if (const auto field = request.find("pan"); field != request.end()) {
            if (!field->is_number()) return Invalid("audio.play field 'pan' must be a number in [-1, 1]");
            pan = field->get<double>();
            if (!std::isfinite(pan) || pan < -1.0 || pan > 1.0) {
                return Invalid("audio.play field 'pan' must be in [-1, 1]");
            }
        }
        std::lock_guard lock(impl_->mutex);
        impl_->RemoveEndedVoices();
        const std::string key = QualifiedId(mod_id, id_field->get_ref<const std::string &>());
        const auto sound = impl_->sounds.find(key);
        if (sound == impl_->sounds.end()) return Invalid("audio id is not registered by this mod");
        if (impl_->voices.size() >= kAudioGlobalVoiceLimit || impl_->OwnerVoiceCount(mod_id) >= kAudioOwnerVoiceLimit) {
            return Invalid("audio playback voice limit reached");
        }
        auto channel = std::make_unique<audio::StreamChannel>();
        channel->Open(sound->second.track);
        channel->SetLoop(loop);
        const double angle = (pan + 1.0) * std::numbers::pi / 4.0;
        const float left = static_cast<float>(volume * std::cos(angle) * std::numbers::sqrt2);
        const float right = static_cast<float>(volume * std::sin(angle) * std::numbers::sqrt2);
        channel->SetVolume(left, right);
        channel->Play();
        const std::string handle = "moonmod-audio-" + std::to_string(impl_->next_handle++);
        impl_->mixer.AddStreamChannel(channel.get());
        impl_->voices.emplace(handle, Impl::Voice{std::string(mod_id), key, std::move(channel)});
        return {ApiCallStatus::Ok, {{"handle", handle}, {"id", id_field->get<std::string>()}}, {}};
    }

    if (operation.path == "audio.stop") {
        Json request;
        std::string error;
        if (!ReadObjectArgument(arguments, request, error)) return Invalid(std::move(error));
        if (!HasOnlyFields(request, {"handle"}, error)) return Invalid(std::move(error));
        const auto handle_field = request.find("handle");
        if (handle_field == request.end() || !handle_field->is_string() ||
            handle_field->get_ref<const std::string &>().empty() || handle_field->get_ref<const std::string &>().size() > 64) {
            return Invalid("audio.stop requires a playback handle");
        }
        std::lock_guard lock(impl_->mutex);
        const auto voice = impl_->voices.find(handle_field->get<std::string>());
        if (voice == impl_->voices.end()) return Invalid("audio playback handle is no longer active");
        if (voice->second.owner != mod_id) {
            return {ApiCallStatus::PermissionDenied, nullptr, "a mod may stop only its own playback"};
        }
        impl_->RemoveVoice(voice);
        return {ApiCallStatus::Ok, {{"stopped", true}, {"handle", handle_field->get<std::string>()}}, {}};
    }

    return {ApiCallStatus::Unsupported, nullptr, "unknown audio API operation"};
}

void AudioApi::UnloadOwner(std::string_view mod_id) {
    std::lock_guard lock(impl_->mutex);
    for (auto voice = impl_->voices.begin(); voice != impl_->voices.end();) {
        if (voice->second.owner == mod_id) {
            auto owned = voice++;
            impl_->RemoveVoice(owned);
        } else {
            ++voice;
        }
    }
    for (auto sound = impl_->sounds.begin(); sound != impl_->sounds.end();) {
        if (sound->second.owner == mod_id) sound = impl_->sounds.erase(sound);
        else ++sound;
    }
    impl_->owner_bytes.erase(std::string(mod_id));
}

void ConfigureAudioApi(RuntimeConfig &config) {
    static AudioApi manager_instance(audio::DefaultMixer());
    AudioApi *manager = &manager_instance;
    config.package_api_handler = [manager](std::string_view mod_id, const std::filesystem::path &package_root,
                                           const ApiOperationSpec &operation, const Json &arguments) {
        return manager->Call(mod_id, package_root, operation, arguments);
    };
    for (const char *path : {"audio.register", "audio.play", "audio.stop"}) {
        auto capability = std::ranges::find(config.api_capabilities, path, &ApiCapability::path);
        if (capability == config.api_capabilities.end()) config.api_capabilities.push_back({path, true, {}});
        else {
            capability->supported = true;
            capability->reason.clear();
        }
    }
    auto previous_cleanup = std::move(config.api_owner_unloaded);
    config.api_owner_unloaded = [manager, previous_cleanup = std::move(previous_cleanup)](std::string_view mod_id) {
        manager->UnloadOwner(mod_id);
        if (previous_cleanup) previous_cleanup(mod_id);
    };
}

} // namespace moonmod
