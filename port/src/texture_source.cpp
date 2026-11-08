#include "texture_source.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string_view>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace {
/**
 * Identifies the native resource represented by a registered buffer range.
 */
struct SourceRange {
    std::string   source;       /**< Actual root-relative filename of the loaded resource. */
    std::uint64_t identity = 0; /**< Registration identity preserved across address reuse. */
    std::size_t   size = 0;     /**< Byte count associated with the registered source. */
};

/**
 *
 * Owns source metadata until both its public registry and registration tokens are released.
 *
 */
struct SourceRegistry {
    std::mutex                                    mutex;
    std::unordered_map<const void *, SourceRange> sources;
    std::uint64_t                                 next_identity = 1;
};

std::shared_ptr<SourceRegistry> Sources() {
    static auto registry = std::make_shared<SourceRegistry>();
    return registry;
}

/**
 *
 * Removes its source entry only if no newer registration replaced the same address.
 *
 */
struct SourceRegistration {
    std::shared_ptr<SourceRegistry> registry;
    const void                     *buffer;
    std::uint64_t                   identity;

    SourceRegistration(std::shared_ptr<SourceRegistry> owner, const void *address, std::uint64_t id)
        : registry(std::move(owner)), buffer(address), identity(id) {}

    SourceRegistration(const SourceRegistration &) = delete;
    SourceRegistration &operator=(const SourceRegistration &) = delete;

    ~SourceRegistration() {
        const std::lock_guard lock(registry->mutex);
        const auto            found = registry->sources.find(buffer);
        if (found != registry->sources.end() && found->second.identity == identity) {
            registry->sources.erase(found);
        }
    }
};

/**
 * MoonMod resource-path aliases: a mod's directory prefix mapped onto the native directory whose converted
 * textures it reuses. Accessed on the main thread only, as the loaders that register sources are.
 */
std::map<std::string, std::string, std::less<>> source_aliases;

std::string NormalizeAlias(std::string value) {
    std::ranges::replace(value, '\\', '/');
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character);
    });
    while (!value.empty() && value.front() == '/') {
        value.erase(value.begin());
    }
    return value;
}

std::string ResolveAlias(std::string source) {
    if (source_aliases.empty()) {
        return source;
    }
    const std::string  normalized = NormalizeAlias(source);
    const std::string *best_source = nullptr;
    const std::string *best_target = nullptr;
    for (const auto &[alias, target] : source_aliases) {
        if (normalized != alias &&
            !(normalized.size() > alias.size() && normalized.starts_with(alias) && normalized[alias.size()] == '/')) {
            continue;
        }
        if (best_source == nullptr || alias.size() > best_source->size()) {
            best_source = &alias;
            best_target = &target;
        }
    }
    if (best_source == nullptr) {
        return source;
    }
    return *best_target + normalized.substr(best_source->size());
}

std::uint64_t Register(const std::shared_ptr<SourceRegistry> &registry, const void *buffer,
                       std::string source, std::size_t size) {
    source = ResolveAlias(std::move(source));
    const std::lock_guard lock(registry->mutex);
    if (registry->next_identity == std::numeric_limits<std::uint64_t>::max()) {
        std::abort();
    }
    const auto id = registry->next_identity++;
    registry->sources.insert_or_assign(buffer, SourceRange{.source = std::move(source), .identity = id, .size = size});
    return id;
}
} // namespace

void TextureSourceRegister(const void *buffer, std::string source, std::size_t size) {
    if (buffer != nullptr) {
        static_cast<void>(Register(Sources(), buffer, std::move(source), size));
    }
}

bool TextureSourceAliasRegister(std::string source, std::string catalog_source) {
    source = NormalizeAlias(std::move(source));
    catalog_source = NormalizeAlias(std::move(catalog_source));
    if (source.empty() || catalog_source.empty() || source == catalog_source) {
        return false;
    }
    const auto existing = source_aliases.find(source);
    if (existing != source_aliases.end()) {
        return existing->second == catalog_source;
    }
    source_aliases.emplace(std::move(source), std::move(catalog_source));
    return true;
}

std::string TextureSourceResolveAlias(std::string source) {
    return ResolveAlias(std::move(source));
}

void TextureSourceAliasesClear() {
    source_aliases.clear();
}

void TextureSourceForget(const void *buffer) {
    const auto            registry = Sources();
    const std::lock_guard lock(registry->mutex);
    registry->sources.erase(buffer);
}

void TextureSourceClear() {
    const auto            registry = Sources();
    const std::lock_guard lock(registry->mutex);
    registry->sources.clear();
}

std::optional<std::string> TextureSourceLookup(const void *buffer) {
    const auto            registry = Sources();
    const std::lock_guard lock(registry->mutex);
    const auto            found = registry->sources.find(buffer);
    if (found != registry->sources.end()) {
        return found->second.source;
    }
    return std::nullopt;
}

void TextureSourceCopy(const void *from, const void *to, std::size_t size) {
    auto source = TextureSourceLookup(from);
    if (source) {
        TextureSourceRegister(to, std::move(*source), size);
    } else {
        TextureSourceForget(to);
    }
}

std::shared_ptr<const void> dc2::assets::RegisterOwnedSource(const void *buffer, std::string source) {
    if (buffer == nullptr) {
        return {};
    }
    const auto registry = Sources();
    auto       registration = std::make_shared<SourceRegistration>(registry, buffer, 0);
    registration->identity = Register(registry, buffer, std::move(source), 0);
    return registration;
}
