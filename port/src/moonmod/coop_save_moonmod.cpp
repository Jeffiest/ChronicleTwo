// Co-op's split of a save into the host's world and a player's own things (moonmod/coop_save.hpp).

#include "moonmod/coop_save.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include "platform/files.hpp"
#include "platform/paths.hpp"
#include "save_json.hpp"
#include "savedata.hpp"
#include "userdata.hpp"

namespace moonmod::coop {

namespace {

constexpr char kWorldTag[] = "DCW2";
constexpr char kPlayerTag[] = "DCP2";
constexpr char kTownTag[] = "DCT2";

// One field of the save: its number in the text, and where it is. A fixed field is copied whole and its size must
// match this build's; a variable field (a town's layout) has its own writer and reader.
struct Field {
    std::uint16_t                                                       id;
    void                                                               *data = nullptr;
    std::size_t                                                         size = 0;
    std::function<std::string()>                                        write;        // variable: its bytes now
    std::function<bool(std::string_view, std::string &)>                check;        // variable: valid?
    std::function<void(std::string_view)>                               apply;        // variable: take the bytes
    bool                                                                variable() const { return static_cast<bool>(write); }
};

Field Fixed(std::uint16_t id, void *data, std::size_t size) { return {id, data, size, {}, {}, {}}; }

Field Layout(std::uint16_t id, const CEditData &record) {
    Field field;
    field.id = id;
    field.write = [&record] {
        const TownLayout *live = PortFindTownLayout(record);
        return LayoutBytes(live != nullptr ? *live : TownLayoutFromRecord(record));
    };
    field.check = [](std::string_view bytes, std::string &error) {
        TownLayout parsed;
        return LayoutFromBytes(bytes, parsed, error);
    };
    field.apply = [&record](std::string_view bytes) {
        std::string error;
        TownLayout  parsed;
        if (LayoutFromBytes(bytes, parsed, error)) PortTownLayout(record) = std::move(parsed);
    };
    return field;
}

// Numbers are for good: a new field takes a new number, and a field that goes keeps its number unused.
std::vector<Field> WorldFields(CSaveData &save) {
    std::vector<Field> fields = {
        Fixed(1, &save.bit_flag, sizeof(save.bit_flag)),
        Fixed(2, &save.short_flag, sizeof(save.short_flag)),
        Fixed(3, &save.map_flag, sizeof(save.map_flag)),
        Fixed(4, &save.now_time, sizeof(save.now_time)),
        Fixed(5, &save.day, sizeof(save.day)),
        Fixed(6, &save.map_no, sizeof(save.map_no)),
        Fixed(7, &save.sub_map_no, sizeof(save.sub_map_no)),
        Fixed(8, &save.area_no, sizeof(save.area_no)),
        Fixed(9, &save.build_parts_num, sizeof(save.build_parts_num)),
        Fixed(10, &save.game_progress, sizeof(save.game_progress)),
        Fixed(11, &save.time_stop, sizeof(save.time_stop)),
        Fixed(12, &save.prev_map_no, sizeof(save.prev_map_no)),
        Fixed(13, &save.prev_sub_map_no, sizeof(save.prev_sub_map_no)),
        Fixed(20, &save.save_dungeon, sizeof(save.save_dungeon)),
        Fixed(21, &save.quest_data, sizeof(save.quest_data)),
        Fixed(22, &save.monster_book, sizeof(save.monster_book)),
        Fixed(23, &save.tour, sizeof(save.tour)),
    };
    for (int town = 0; town < SAVE_EDIT_DATA_MAX; ++town) {
        fields.push_back(Fixed(static_cast<std::uint16_t>(30 + town), &save.edit_data[town], sizeof(CEditData)));
        fields.push_back(Layout(static_cast<std::uint16_t>(40 + town), save.edit_data[town]));
    }
    return fields;
}

std::vector<Field> PlayerFields(CSaveData &save) {
    CUserDataManager &user = save.user_data;
    return {
        Fixed(1, &user.chara_data, sizeof(user.chara_data)),     // Max and Monica: status, active items, equipment
        Fixed(2, &user.robo_data, sizeof(user.robo_data)),       // the ridepod
        Fixed(3, &user.used_data, sizeof(user.used_data)),       // the bag
        Fixed(4, &user.esa, sizeof(user.esa)),                   // the bait
        Fixed(5, &user.aquarium, sizeof(user.aquarium)),
        Fixed(6, &user.monster_box, sizeof(user.monster_box)),   // the badges
        Fixed(7, &user.party_chara, sizeof(user.party_chara)),   // the townsfolk who can join
        Fixed(8, &user.invent_data, sizeof(user.invent_data)),   // inventions and photos
        Fixed(9, &user.party_member, sizeof(user.party_member)),
        Fixed(10, &user.chara_change, sizeof(user.chara_change)),
        Fixed(11, &user.chara_change_mask, sizeof(user.chara_change_mask)),
        Fixed(12, &user.active_chr_no, sizeof(user.active_chr_no)),
        Fixed(13, &user.monster_id, sizeof(user.monster_id)),
        Fixed(14, &user.money, sizeof(user.money)),              // Gilda
        Fixed(15, &user.yarikomi_medal, sizeof(user.yarikomi_medal)),
        Fixed(16, &user.special_item_bought, sizeof(user.special_item_bought)),
        Fixed(17, &user.photo_subject, sizeof(user.photo_subject)),
        Fixed(18, &user.fish_tournament, sizeof(user.fish_tournament)),
        Fixed(19, &user.fish_record, sizeof(user.fish_record)),
        Fixed(20, &user.costume_bit, sizeof(user.costume_bit)),
        Fixed(21, &save.menu_system_data, sizeof(save.menu_system_data)),
    };
}

std::vector<Field> TownFields(CSaveData &save, int town) {
    return {Fixed(1, &save.edit_data[town], sizeof(CEditData)), Layout(2, save.edit_data[town])};
}

std::string Base64(std::string_view bytes) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto     at = [&bytes](std::size_t k) { return static_cast<unsigned>(static_cast<unsigned char>(bytes[k])); };
        const unsigned v = (at(i) << 16) | ((i + 1 < bytes.size() ? at(i + 1) : 0) << 8) | (i + 2 < bytes.size() ? at(i + 2) : 0);
        out.push_back(kTable[(v >> 18) & 63]);
        out.push_back(kTable[(v >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? kTable[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < bytes.size() ? kTable[v & 63] : '=');
    }
    return out;
}

bool Unbase64(std::string_view text, std::string &out) {
    out.clear();
    unsigned value = 0;
    int      bits = 0;
    for (const char c : text) {
        int digit;
        if (c >= 'A' && c <= 'Z') digit = c - 'A';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 26;
        else if (c >= '0' && c <= '9') digit = c - '0' + 52;
        else if (c == '+') digit = 62;
        else if (c == '/') digit = 63;
        else if (c == '=') break;
        else return false;
        value = (value << 6) | static_cast<unsigned>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
        }
    }
    return true;
}

void PutField(std::string &bytes, std::uint16_t id, std::string_view data) {
    const auto size = static_cast<std::uint32_t>(data.size());
    bytes.push_back(static_cast<char>(id & 0xFF));
    bytes.push_back(static_cast<char>(id >> 8));
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<char>((size >> shift) & 0xFF));
    bytes.append(data);
}

std::string Export(const std::string &tag, const std::vector<Field> &fields) {
    std::string bytes;
    for (const Field &field : fields) {
        if (field.variable()) PutField(bytes, field.id, field.write());
        else PutField(bytes, field.id, std::string_view(static_cast<const char *>(field.data), field.size));
    }
    return tag + Base64(bytes);
}

const char *NotThis(const std::string &tag) {
    if (tag == kWorldTag) return "not a world from save.world_export";
    if (tag == kPlayerTag) return "not a player from save.player_export";
    return "not that town's georama.town_export";
}

// Reads every field first and writes none unless all of them fit: a refused import changes nothing.
bool Import(const std::string &tag, std::string_view text, const std::vector<Field> &fields, std::string &error) {
    if (!text.starts_with(tag)) {
        error = NotThis(tag);
        return false;
    }
    std::string bytes;
    if (!Unbase64(text.substr(tag.size()), bytes)) {
        error = "the data is damaged (not base64)";
        return false;
    }
    std::vector<std::pair<const Field *, std::string_view>> found;
    std::size_t                                             at = 0;
    while (at < bytes.size()) {
        if (bytes.size() - at < 6) {
            error = "the data is damaged (cut short)";
            return false;
        }
        const auto    byte = [&bytes](std::size_t k) { return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[k])); };
        const auto    id = static_cast<std::uint16_t>(byte(at) | (byte(at + 1) << 8));
        std::uint32_t size = 0;
        for (int k = 0; k < 4; ++k) size |= byte(at + 2 + k) << (8 * k);
        at += 6;
        if (bytes.size() - at < size) {
            error = "the data is damaged (cut short)";
            return false;
        }
        const auto field = std::ranges::find(fields, id, &Field::id);
        if (field == fields.end() || (!field->variable() && field->size != size)) {
            error = "the data comes from another build of the game; play on the same build";
            return false;
        }
        const std::string_view data(bytes.data() + at, size);
        if (field->variable() && !field->check(data, error)) return false;
        found.emplace_back(&*field, data);
        at += size;
    }
    if (found.size() != fields.size()) {
        error = "the data comes from another build of the game; play on the same build";
        return false;
    }
    for (const auto &[field, data] : found) {
        if (field->variable()) field->apply(data);
        else std::memcpy(field->data, data.data(), data.size());
    }
    return true;
}

std::string TownTag(int town) { return std::string(kTownTag) + std::to_string(town) + ":"; }

// A layout's bytes: counts, then records, little-endian, fixed widths (no struct is copied whole).
class Writer {
public:
    void U32(std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) out_.push_back(static_cast<char>((v >> shift) & 0xFF));
    }
    void I32(std::int32_t v) { U32(static_cast<std::uint32_t>(v)); }
    void I16(std::int16_t v) {
        const auto u = static_cast<std::uint16_t>(v);
        out_.push_back(static_cast<char>(u & 0xFF));
        out_.push_back(static_cast<char>(u >> 8));
    }
    void I8(std::int8_t v) { out_.push_back(static_cast<char>(v)); }
    void U8(std::uint8_t v) { out_.push_back(static_cast<char>(v)); }
    void Bytes(const void *data, std::size_t size) { out_.append(static_cast<const char *>(data), size); }
    std::string Take() { return std::move(out_); }

private:
    std::string out_;
};

class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}
    bool U32(std::uint32_t &v) {
        if (bytes_.size() - at_ < 4) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes_[at_ + k])) << (8 * k);
        at_ += 4;
        return true;
    }
    bool I32(std::int32_t &v) {
        std::uint32_t u;
        if (!U32(u)) return false;
        v = static_cast<std::int32_t>(u);
        return true;
    }
    bool I16(std::int16_t &v) {
        if (bytes_.size() - at_ < 2) return false;
        v = static_cast<std::int16_t>(static_cast<unsigned char>(bytes_[at_]) | (static_cast<unsigned char>(bytes_[at_ + 1]) << 8));
        at_ += 2;
        return true;
    }
    bool I8(std::int8_t &v) {
        if (bytes_.size() - at_ < 1) return false;
        v = static_cast<std::int8_t>(bytes_[at_++]);
        return true;
    }
    bool U8(std::uint8_t &v) {
        if (bytes_.size() - at_ < 1) return false;
        v = static_cast<std::uint8_t>(bytes_[at_++]);
        return true;
    }
    bool Bytes(void *data, std::size_t size) {
        if (bytes_.size() - at_ < size) return false;
        std::memcpy(data, bytes_.data() + at_, size);
        at_ += size;
        return true;
    }
    bool Done() const { return at_ == bytes_.size(); }

private:
    std::string_view bytes_;
    std::size_t      at_ = 0;
};

constexpr std::uint32_t kLayoutVersion = 1;
constexpr std::uint32_t kLayoutPartsMax = 65536;
constexpr std::uint32_t kLayoutHousesMax = 4096;
constexpr std::uint32_t kLayoutLogMax = 1u << 20;
constexpr std::uint32_t kLayoutGridsMax = 256;
constexpr std::uint32_t kLayoutCellsMax = 1u << 20;

} // namespace

std::string LayoutBytes(const TownLayout &layout) {
    Writer w;
    w.U32(kLayoutVersion);
    w.U32(static_cast<std::uint32_t>(layout.parts.size()));
    for (const TownPart &part : layout.parts) {
        w.I32(part.id);
        w.I8(part.state);
        w.I8(part.angle);
        for (const s16 p : part.pos) w.I16(p);
        w.U32(static_cast<std::uint32_t>(part.color.size()));
        for (const auto &color : part.color) {
            for (const u8 c : color) w.U8(c);
        }
        w.I32(part.house_no);
    }
    w.U32(static_cast<std::uint32_t>(layout.houses.size()));
    for (const TownHouse &house : layout.houses) w.I16(house.npc_no);
    w.U32(static_cast<std::uint32_t>(layout.place_log.size()));
    for (const EditPlaceLog &log : layout.place_log) w.Bytes(&log, sizeof(log));
    w.U32(static_cast<std::uint32_t>(layout.grids.size()));
    for (const TownGrid &grid : layout.grids) {
        w.I32(grid.num_x);
        w.I32(grid.num_z);
        for (const s16 p : grid.pos) w.I16(p);
        w.U32(static_cast<std::uint32_t>(grid.cells.size()));
        w.Bytes(grid.cells.data(), grid.cells.size());
    }
    return w.Take();
}

bool LayoutFromBytes(std::string_view bytes, TownLayout &layout, std::string &error) {
    const auto damaged = [&error] {
        error = "a town's layout is damaged";
        return false;
    };
    Reader        r(bytes);
    std::uint32_t version = 0, count = 0;
    if (!r.U32(version) || version != kLayoutVersion) {
        error = "a town's layout comes from another build of the game; play on the same build";
        return false;
    }
    TownLayout out;
    if (!r.U32(count) || count > kLayoutPartsMax) return damaged();
    out.parts.resize(count);
    for (TownPart &part : out.parts) {
        std::uint32_t colors = 0;
        if (!r.I32(part.id) || !r.I8(part.state) || !r.I8(part.angle)) return damaged();
        for (s16 &p : part.pos) {
            if (!r.I16(p)) return damaged();
        }
        if (!r.U32(colors) || colors != part.color.size()) {
            error = "a town's layout comes from another build of the game; play on the same build";
            return false;
        }
        for (auto &color : part.color) {
            for (u8 &c : color) {
                if (!r.U8(c)) return damaged();
            }
        }
        if (!r.I32(part.house_no)) return damaged();
    }
    if (!r.U32(count) || count > kLayoutHousesMax) return damaged();
    out.houses.resize(count);
    for (TownHouse &house : out.houses) {
        if (!r.I16(house.npc_no)) return damaged();
    }
    if (!r.U32(count) || count > kLayoutLogMax) return damaged();
    out.place_log.resize(count);
    for (EditPlaceLog &log : out.place_log) {
        if (!r.Bytes(&log, sizeof(log))) return damaged();
    }
    if (!r.U32(count) || count > kLayoutGridsMax) return damaged();
    out.grids.resize(count);
    for (TownGrid &grid : out.grids) {
        std::uint32_t cells = 0;
        if (!r.I32(grid.num_x) || !r.I32(grid.num_z)) return damaged();
        for (s16 &p : grid.pos) {
            if (!r.I16(p)) return damaged();
        }
        if (!r.U32(cells) || cells > kLayoutCellsMax) return damaged();
        grid.cells.resize(cells);
        if (!r.Bytes(grid.cells.data(), cells)) return damaged();
    }
    if (!r.Done()) return damaged();
    layout = std::move(out);
    return true;
}

std::string ExportWorld(const CSaveData &save) { return Export(kWorldTag, WorldFields(const_cast<CSaveData &>(save))); }

bool ImportWorld(std::string_view text, CSaveData &save, std::string &error) {
    return Import(kWorldTag, text, WorldFields(save), error);
}

std::string ExportPlayer(const CSaveData &save) { return Export(kPlayerTag, PlayerFields(const_cast<CSaveData &>(save))); }

bool ImportPlayer(std::string_view text, CSaveData &save, std::string &error) {
    return Import(kPlayerTag, text, PlayerFields(save), error);
}

std::string ExportTown(const CSaveData &save, int town) {
    if (town < 0 || town >= SAVE_EDIT_DATA_MAX) return {};
    return Export(TownTag(town), TownFields(const_cast<CSaveData &>(save), town));
}

bool ImportTown(std::string_view text, int town, CSaveData &save, std::string &error) {
    if (town < 0 || town >= SAVE_EDIT_DATA_MAX) {
        error = "no such town";
        return false;
    }
    if (!text.starts_with(TownTag(town))) {
        error = "not this town's georama.town_export";
        return false;
    }
    return Import(TownTag(town), text, TownFields(save, town), error);
}

namespace {

// A town decoded apart from any save: its record and its layout.
struct TownCopy {
    std::unique_ptr<CEditData> record = std::make_unique<CEditData>();
    TownLayout                 layout;
    std::vector<Field>         Fields() {
        Field live;
        live.id = 2;
        live.write = [this] { return LayoutBytes(layout); };
        live.check = [](std::string_view bytes, std::string &error) {
            TownLayout parsed;
            return LayoutFromBytes(bytes, parsed, error);
        };
        live.apply = [this](std::string_view bytes) {
            std::string error;
            LayoutFromBytes(bytes, layout, error);
        };
        return {Fixed(1, record.get(), sizeof(CEditData)), live};
    }
};

bool SamePart(const TownPart &a, const TownPart &b) {
    return a.id == b.id && a.state == b.state && a.angle == b.angle && a.pos == b.pos && a.color == b.color && a.house_no == b.house_no;
}
bool SameGrid(const TownGrid &a, const TownGrid &b) {
    return a.num_x == b.num_x && a.num_z == b.num_z && a.pos == b.pos && a.cells == b.cells;
}
bool SameLog(const EditPlaceLog &a, const EditPlaceLog &b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

// The parts a layout holds (slots with a definition), by value.
std::vector<TownPart> Placed(const TownLayout &layout) {
    std::vector<TownPart> out;
    for (const TownPart &part : layout.parts) {
        if (part.id != 0) out.push_back(part);
    }
    return out;
}

} // namespace

bool MergeTown(std::string_view base, std::string_view edited, int town, CSaveData &save, std::string &error) {
    if (town < 0 || town >= SAVE_EDIT_DATA_MAX) {
        error = "no such town";
        return false;
    }
    const std::string tag = TownTag(town);
    if (!base.starts_with(tag) || !edited.starts_with(tag)) {
        error = "not this town's georama.town_export";
        return false;
    }
    auto before = std::make_unique<TownCopy>(), after = std::make_unique<TownCopy>(), now = std::make_unique<TownCopy>();
    if (!Import(tag, base, before->Fields(), error) || !Import(tag, edited, after->Fields(), error)) return false;
    std::memcpy(static_cast<void *>(now->record.get()), &save.edit_data[town], sizeof(CEditData));
    if (const TownLayout *live = PortFindTownLayout(save.edit_data[town]); live != nullptr) now->layout = *live;
    else now->layout = TownLayoutFromRecord(save.edit_data[town]);

    // The record: what they changed of its scalars and analysis takes their value.
    const CEditData &was = *before->record, &is = *after->record;
    CEditData       &rec = *now->record;
    if (was.save_count != is.save_count) rec.save_count = is.save_count;
    if (was.culture_point != is.culture_point) rec.culture_point = is.culture_point;
    if (std::memcmp(&was.analyze, &is.analyze, sizeof(was.analyze)) != 0) rec.analyze = is.analyze;
    if (std::memcmp(was.grid, is.grid, sizeof(was.grid)) != 0) std::memcpy(rec.grid, is.grid, sizeof(rec.grid));

    // The layout: houses and river grids they changed take their value, by index.
    TownLayout &layout = now->layout;
    for (std::size_t i = 0; i < after->layout.houses.size(); ++i) {
        const bool changed = i >= before->layout.houses.size() || before->layout.houses[i].npc_no != after->layout.houses[i].npc_no;
        if (!changed) continue;
        if (layout.houses.size() <= i) layout.houses.resize(i + 1);
        layout.houses[i] = after->layout.houses[i];
    }
    for (std::size_t i = 0; i < after->layout.grids.size(); ++i) {
        const bool changed = i >= before->layout.grids.size() || !SameGrid(before->layout.grids[i], after->layout.grids[i]);
        if (!changed) continue;
        if (layout.grids.size() <= i) layout.grids.resize(i + 1);
        layout.grids[i] = after->layout.grids[i];
    }
    // The placed parts: what both lists hold is neither taken away nor added (one for one, as a part may stand twice).
    std::vector<TownPart> placed = Placed(layout), removed = Placed(before->layout), added = Placed(after->layout);
    for (auto it = removed.begin(); it != removed.end();) {
        const auto kept = std::ranges::find_if(added, [&](const TownPart &part) { return SamePart(part, *it); });
        if (kept != added.end()) {
            added.erase(kept);
            it = removed.erase(it);
        } else {
            ++it;
        }
    }
    for (const TownPart &gone : removed) {
        const auto found = std::ranges::find_if(placed, [&](const TownPart &part) { return SamePart(part, gone); });
        if (found != placed.end()) placed.erase(found);
    }
    for (const TownPart &part : added) placed.push_back(part);
    if (placed.size() > kLayoutPartsMax) placed.resize(kLayoutPartsMax);
    layout.parts = std::move(placed);
    // The placement log indexes parts, which moved: theirs is taken when it changed, else dropped (the game rebuilds
    // what it needs from the parts on load).
    bool log_changed = before->layout.place_log.size() != after->layout.place_log.size();
    for (std::size_t i = 0; !log_changed && i < after->layout.place_log.size(); ++i) {
        log_changed = !SameLog(before->layout.place_log[i], after->layout.place_log[i]);
    }
    if (log_changed) layout.place_log = after->layout.place_log;
    else layout.place_log.clear();

    std::memcpy(static_cast<void *>(&save.edit_data[town]), now->record.get(), sizeof(CEditData));
    PortTownLayout(save.edit_data[town]) = std::move(now->layout);
    return true;
}

namespace {

constexpr char kAccountsFile[] = "coop_accounts.json";
std::map<std::string, Account, std::less<>> g_accounts;
std::optional<std::filesystem::path>        g_accounts_directory;
bool                                        g_accounts_loaded = false;

} // namespace

bool ValidAccountId(std::string_view id) {
    return id.size() >= 8 && id.size() <= 64 &&
           std::ranges::all_of(id, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

const Account *AccountGet(std::string_view id) {
    const auto found = g_accounts.find(id);
    return found == g_accounts.end() ? nullptr : &found->second;
}

bool AccountSet(Account account, std::string &error) {
    if (!ValidAccountId(account.id)) {
        error = "an account id is 8-64 of a-z, 0-9 and -";
        return false;
    }
    if (!account.player.starts_with(kPlayerTag) || account.player.size() > kAccountMax) {
        error = "an account holds save.player_export text";
        return false;
    }
    if (account.name.size() > 24) account.name.resize(24);
    if (!g_accounts.contains(account.id) && g_accounts.size() >= kAccountsMax) {
        error = "this save keeps " + std::to_string(kAccountsMax) + " guests at most; remove one first";
        return false;
    }
    const std::string id = account.id;
    g_accounts.insert_or_assign(id, std::move(account));
    return true;
}

bool AccountRemove(std::string_view id) {
    const auto found = g_accounts.find(id);
    if (found == g_accounts.end()) return false;
    g_accounts.erase(found);
    return true;
}

std::vector<const Account *> Accounts() {
    std::vector<const Account *> out;
    for (const auto &[id, account] : g_accounts) out.push_back(&account);
    return out;
}

void AccountsClear() {
    g_accounts.clear();
    g_accounts_loaded = true;
}

void AccountsDelete(const std::filesystem::path &directory) {
    std::error_code error;
    std::filesystem::remove(directory / kAccountsFile, error);
}

bool AccountsSave(const std::filesystem::path &directory) {
    if (g_accounts.empty()) {
        AccountsDelete(directory);
        return true;
    }
    std::error_code ignored;
    std::filesystem::create_directories(directory, ignored);
    nlohmann::json accounts = nlohmann::json::object();
    for (const auto &[id, account] : g_accounts) accounts[id] = {{"name", account.name}, {"player", account.player}};
    const std::string text = nlohmann::json{{"format", 1}, {"game", "chronicle-two"}, {"accounts", std::move(accounts)}}.dump();
    if (::FilesWrite(directory / kAccountsFile, text.data(), text.size(), true) != ::FilesResult::kWritten) {
        std::fprintf(stderr, "co-op: could not write %s\n", (directory / kAccountsFile).string().c_str());
        return false;
    }
    return true;
}

void AccountsLoad(const std::filesystem::path &directory) {
    g_accounts.clear();
    g_accounts_loaded = true;
    std::string text;
    if (std::ifstream stream(directory / kAccountsFile, std::ios::binary); stream) {
        text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
    if (text.empty()) return;
    const nlohmann::json file = nlohmann::json::parse(text, nullptr, false);
    if (!file.is_object() || file.value("format", 0) != 1 || !file.contains("accounts") || !file["accounts"].is_object()) {
        std::fprintf(stderr, "co-op: %s could not be read; this game keeps no guests\n", (directory / kAccountsFile).string().c_str());
        return;
    }
    for (const auto &[id, entry] : file["accounts"].items()) {
        if (!entry.is_object()) continue;
        std::string error;
        AccountSet({id, entry.value("name", std::string()), entry.value("player", std::string())}, error);
    }
}

void SetAccountsDirectory(std::filesystem::path directory) {
    g_accounts_directory = std::move(directory);
    g_accounts_loaded = false;
}

const std::filesystem::path &AccountsDirectory() {
    if (!g_accounts_directory) g_accounts_directory = PathsSaveRoot() / "moonmod";
    return *g_accounts_directory;
}

void AccountsEnsureLoaded() {
    if (!g_accounts_loaded) AccountsLoad(AccountsDirectory());
}

bool AccountsStore() { return AccountsSave(AccountsDirectory()); }

} // namespace moonmod::coop
