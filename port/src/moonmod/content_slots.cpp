#include "moonmod/content_slots.hpp"

#include <fstream>
#include <set>
#include <system_error>

#include "moonmod/api.hpp"
#include "moonmod/symlinks.hpp"
#include "platform/paths.hpp"

namespace moonmod {
namespace {

std::filesystem::path g_test_save_root;

const char *KindKey(ContentSlotKind kind) {
    switch (kind) {
        case ContentSlotKind::Item: return "items";
        case ContentSlotKind::Weapon: return "weapons";
        case ContentSlotKind::Georama: return "georama";
    }
    return "items";
}

const char *KindFile(ContentSlotKind kind) {
    switch (kind) {
        case ContentSlotKind::Item: return "chronicle-two-item-slots.json";
        case ContentSlotKind::Weapon: return "chronicle-two-weapon-slots.json";
        case ContentSlotKind::Georama: return "chronicle-two-georama-slots.json";
    }
    return "chronicle-two-item-slots.json";
}

bool ReadRegistry(const std::filesystem::path &path, const std::filesystem::path &root, ContentSlotKind kind,
                  std::map<std::string, int> &assignments, std::string &error) {
    assignments.clear();
    if (ContainsSymlinkBelow(path, root)) {
        error = "Chronicle Two content slot registry path contains a symbolic link";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) error = "Chronicle Two content slot registry could not be inspected: " + ec.message();
        return !ec;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > 256 * 1024) {
        error = "Chronicle Two content slot registry is unreadable or larger than 256 KiB";
        return false;
    }
    try {
        std::ifstream input(path, std::ios::binary);
        Json loaded;
        input >> loaded;
        if (!loaded.is_object() || loaded.value("format_version", 0) != 1 || !loaded.contains(KindKey(kind)) ||
            !loaded[KindKey(kind)].is_object()) {
            error = "Chronicle Two content slot registry has an unsupported format";
            return false;
        }
        std::set<int> used;
        for (auto it = loaded[KindKey(kind)].begin(); it != loaded[KindKey(kind)].end(); ++it) {
            if (!it.value().is_number_integer()) {
                error = "Chronicle Two content slot registry contains an invalid assignment";
                return false;
            }
            const int number = it.value().get<int>();
            if (number <= 0 || !used.insert(number).second) {
                error = "Chronicle Two content slot registry contains an out-of-range or duplicate number";
                return false;
            }
            assignments[it.key()] = number;
        }
        return true;
    } catch (const std::exception &) {
        error = "Chronicle Two content slot registry is malformed";
        return false;
    }
}

bool WriteRegistry(const std::filesystem::path &path, const std::filesystem::path &root, ContentSlotKind kind,
                   const std::map<std::string, int> &assignments, std::string &error) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    if (ContainsSymlinkBelow(path, root) || ContainsSymlinkBelow(temporary, root)) {
        error = "Chronicle Two content slot registry path contains a symbolic link";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "could not create the Chronicle Two content slot registry directory: " + ec.message();
        return false;
    }
    Json table = Json::object();
    for (const auto &[id, number] : assignments) table[id] = number;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "could not write the Chronicle Two content slot registry";
            return false;
        }
        const std::string document = Json{{"format_version", 1}, {KindKey(kind), table}}.dump(2);
        output.write(document.data(), static_cast<std::streamsize>(document.size()));
        output.flush();
        if (!output.good()) {
            error = "could not finish writing the Chronicle Two content slot registry";
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = "could not replace the Chronicle Two content slot registry: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

std::filesystem::path ChronicleTwoContentSlotFile(ContentSlotKind kind, const std::filesystem::path &save_root) {
    return save_root / "moonmod" / "game-content" / KindFile(kind);
}

std::filesystem::path ChronicleTwoContentSaveRoot() {
    return g_test_save_root.empty() ? PathsSaveRoot() : g_test_save_root;
}

void SetChronicleTwoContentSaveRootForTests(const std::filesystem::path &root) { g_test_save_root = root; }

bool LoadChronicleTwoContentSlots(ContentSlotKind kind, std::map<std::string, int> &assignments, std::string &error) {
    error.clear();
    const std::filesystem::path root = ChronicleTwoContentSaveRoot();
    if (root.empty()) {
        error = "Chronicle Two mod content needs a save directory to persist its numbers";
        return false;
    }
    return ReadRegistry(ChronicleTwoContentSlotFile(kind, root), root, kind, assignments, error);
}

bool AllocateChronicleTwoContentSlot(ContentSlotKind kind, std::string_view stable_id, int first, int last,
                                     const std::function<bool(int)> &is_free, int &number, std::string &error) {
    std::map<std::string, int> assignments;
    if (!LoadChronicleTwoContentSlots(kind, assignments, error)) return false;
    const std::string key(stable_id);
    if (const auto found = assignments.find(key); found != assignments.end()) {
        number = found->second;
        return true;
    }
    std::set<int> occupied;
    for (const auto &[id, taken] : assignments) occupied.insert(taken);
    if (kind == ContentSlotKind::Item || kind == ContentSlotKind::Weapon) {
        // Items and weapons share the game's item numbers: one file must not hand out the other's number.
        std::map<std::string, int> sibling;
        std::string sibling_error;
        if (!LoadChronicleTwoContentSlots(kind == ContentSlotKind::Item ? ContentSlotKind::Weapon : ContentSlotKind::Item,
                                          sibling, sibling_error)) {
            error = sibling_error;
            return false;
        }
        for (const auto &[id, taken] : sibling) occupied.insert(taken);
    }
    const int step = first <= last ? 1 : -1;
    for (int candidate = first;; candidate += step) {
        if (!occupied.contains(candidate) && is_free(candidate)) {
            assignments[key] = candidate;
            const std::filesystem::path root = ChronicleTwoContentSaveRoot();
            if (!WriteRegistry(ChronicleTwoContentSlotFile(kind, root), root, kind, assignments, error)) return false;
            number = candidate;
            return true;
        }
        if (candidate == last) break;
    }
    error = "Chronicle Two has no free number left for this content";
    return false;
}

} // namespace moonmod
