#include "platform/progressive_mount.hpp"

#ifdef __SWITCH__

std::shared_ptr<const vfs::Mount> ProgressiveMountOpen() {
    return nullptr;
}

#else

#include "kazarov_bridge/rust/lib.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "kazarov/data_version.hpp"
#include "profile.hpp"

namespace {

/**
 * Serves the entries of the archive a progressive extraction is writing. The names are fixed when the session
 * becomes ready; what a name holds is known once its conversion has run, so the questions that need it (its
 * size, its bytes, whether an optional name exists at all) wait for that conversion.
 */
class ProgressiveMount final : public vfs::Mount {
public:
    ProgressiveMount() {
        CT_PROFILE_ZONE("Open growing asset archive");
        const auto count = kazarov::progressive_count();
        names_.resize(count);
        index_.reserve(count);
        present_ = std::make_unique<std::atomic<std::uint8_t>[]>(count);
        for (std::size_t index = 0; index < count; ++index) {
            try {
                const auto name = kazarov::progressive_name(index);
                names_[index].assign(name.data(), name.size());
            } catch (const std::exception &error) {
                throw std::runtime_error(error.what());
            }
            if (!index_.emplace(vfs::FoldKey(names_[index]), index).second) {
                throw std::runtime_error("paths differ only by ASCII case: " + names_[index]);
            }
        }
    }

    std::size_t Count() const override { return names_.size(); }

    std::string Name(std::size_t index) const override { return names_.at(index); }

    std::string Display(std::size_t index) const override { return "data.zip!" + names_.at(index); }

    std::optional<std::size_t> Find(std::string_view relative) const override {
        const auto found = index_.find(vfs::FoldKey(relative));
        if (found == index_.end() || !Present(found->second)) {
            return std::nullopt;
        }
        return found->second;
    }

    std::optional<std::uint64_t> Size(std::size_t index) const override {
        if (index >= names_.size()) {
            return std::nullopt;
        }
        try {
            return kazarov::progressive_size(index);
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }

    bool Read(std::size_t index, std::vector<std::uint8_t> &bytes, std::uint64_t max_bytes) const override {
        bytes.clear();
        const auto size = Size(index);
        if (!size || *size > max_bytes || *size > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        bytes.resize(static_cast<std::size_t>(*size));
        if (!ReadInto(index, bytes.data(), bytes.size())) {
            bytes.clear();
            return false;
        }
        return true;
    }

    bool ReadInto(std::size_t index, void *buffer, std::size_t size) const override {
        CT_PROFILE_ZONE("Read growing archive entry");
        if (index >= names_.size() || (buffer == nullptr && size != 0)) {
            return false;
        }
        CT_PROFILE_TEXT(names_[index].data(), names_[index].size());
        try {
            // The session refuses a size other than the entry's.
            kazarov::progressive_read(index, rust::Slice<std::uint8_t>(static_cast<std::uint8_t *>(buffer), size));
            return true;
        } catch (const std::exception &) {
            return false;
        }
    }

    bool ReadPrefix(std::size_t index, std::span<std::uint8_t> out) const override {
        CT_PROFILE_ZONE("Read growing archive header");
        if (index >= names_.size()) {
            return false;
        }
        try {
            // A picture not converted yet answers with the header its conversion will write, so registering an
            // area's textures does not wait for each of them.
            return kazarov::progressive_prefix(index, rust::Slice<std::uint8_t>(out.data(), out.size())) == out.size();
        } catch (const std::exception &) {
            return false;
        }
    }

    // The same for an entry before and after its conversion, so nothing that keeps a stamp waits for one.
    std::string Stamp(std::size_t index) const override {
        return "progressive|" + std::to_string(KAZAROV_DATA_VERSION) + '|' + names_.at(index);
    }

    std::vector<vfs::Member> List(std::string_view directory) const override {
        std::call_once(listing_once_, [this] { BuildListing(); });
        const auto found = listing_.find(vfs::FoldKey(directory));
        return found == listing_.end() ? std::vector<vfs::Member>{} : found->second;
    }

private:
    /**
     * Whether the entry exists: at once for all but a name its conversion may leave out, which waits for it.
     */
    bool Present(std::size_t index) const {
        enum : std::uint8_t { Unknown, Yes, No };
        auto &known = present_[index];
        if (const auto state = known.load(std::memory_order_relaxed); state != Unknown) {
            return state == Yes;
        }
        bool present = false;
        try {
            present = kazarov::progressive_present(index);
        } catch (const std::exception &) {
            return false;
        }
        known.store(present ? Yes : No, std::memory_order_relaxed);
        return present;
    }

    /**
     * Groups every entry and implied directory under its parent directory's folded name.
     */
    void BuildListing() const {
        CT_PROFILE_ZONE("List growing archive directories");
        std::unordered_map<std::string, std::unordered_map<std::string, vfs::Member>> members;
        for (const auto &entry : names_) {
            std::size_t start = 0;
            while (true) {
                const auto slash = entry.find('/', start);
                const auto parent = vfs::FoldKey(std::string_view(entry).substr(0, start ? start - 1 : 0));
                const auto name = entry.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                auto      &children = members[parent];
                if (slash == std::string::npos) {
                    children.insert_or_assign(vfs::FoldKey(name), vfs::Member{.name = name, .directory = false});
                    break;
                }
                children.try_emplace(vfs::FoldKey(name), vfs::Member{.name = name, .directory = true});
                start = slash + 1;
            }
        }
        for (auto &[parent, children] : members) {
            auto &list = listing_[parent];
            for (auto &[key, member] : children) {
                list.push_back(std::move(member));
            }
        }
    }

    std::vector<std::string>                                          names_;        /**< Entry names, sorted. */
    std::unordered_map<std::string, std::size_t>                      index_;        /**< Folded names mapped to entries. */
    std::unique_ptr<std::atomic<std::uint8_t>[]>                      present_;      /**< What Present found, per entry. */
    mutable std::once_flag                                            listing_once_; /**< Builds the directory listing on first use. */
    mutable std::unordered_map<std::string, std::vector<vfs::Member>> listing_;      /**< Direct members per folded directory. */
};

} // namespace

std::shared_ptr<const vfs::Mount> ProgressiveMountOpen() {
    const auto phase = kazarov::progressive_status().phase;
    // Ready or published: before that there are no names, and after a failure nothing to read.
    if (phase != 2 && phase != 3) {
        return nullptr;
    }
    return std::make_shared<ProgressiveMount>();
}

#endif
