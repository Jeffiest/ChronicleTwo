// Co-op's screens (moonmod/coop.hpp): the choice panel over the game, a mod's title mode page, and a visit's start
// and end.

#include "moonmod/coop.hpp"
#include "moonmod/coop_save.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "font.hpp"
#include "gamepad.hpp"
#include "mainloop.hpp"
#include "mapselect.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "menusys.hpp"
#include "mglib.hpp"
#include "moonmod/mapselect_moonmod.hpp"
#include "savedata.hpp"
#include "userdata.hpp"

namespace moonmod::coop {

namespace {

ApiCallResult Invalid(std::string message) { return {ApiCallStatus::InvalidArgument, nullptr, std::move(message)}; }
ApiCallResult Failed(std::string message) { return {ApiCallStatus::Failed, nullptr, std::move(message)}; }

// The canvas is 512 units wide and the game's menus lay out 640-wide (options/mod_manager_ui.cpp).
constexpr float kUiScaleX = 0.8f;
float           UiX(int x) { return static_cast<float>(x) * kUiScaleX; }

void Fill(int x, int y, int width, int height, int red, int green, int blue, int alpha) {
    DrawMenuFillBox(UiX(x), static_cast<float>(y), UiX(width), static_cast<float>(height),
                    static_cast<unsigned char>(std::clamp(alpha, 0, 0x80)), red, green, blue);
}

void Text(std::string value, int x, int y, int width, bool selected, int alpha, int size = 16) {
    if (value.empty()) return;
    CFont     font;
    const int glyph_width = std::max(1, static_cast<int>(static_cast<float>(size) * kUiScaleX));
    font.SetDrawSize(glyph_width, size + 4);
    font.SetClearance(glyph_width, size + 5);
    font.SetFuchi(FUCHI_SHADOW_BLACK);
    font.SetColor(selected ? 128 : 118, selected ? 108 : 118, selected ? 64 : 118, static_cast<unsigned char>(std::clamp(alpha, 0, 0x80)));
    int measured_width = 0, measured_height = 0;
    font.CalcDrawWH(value.data(), &measured_width, &measured_height);
    const int scaled_width = static_cast<int>(static_cast<float>(width) * kUiScaleX);
    while (measured_width > scaled_width && value.size() > 3) {
        value.pop_back();
        std::string shortened = value + "...";
        font.CalcDrawWH(shortened.data(), &measured_width, &measured_height);
        if (measured_width <= scaled_width) {
            value = std::move(shortened);
            break;
        }
    }
    font.DrawDirect(value.data(), static_cast<int>(UiX(x)), y);
}

bool ValidLocalId(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

bool ValidText(const Json &value, std::size_t max_bytes, bool allow_empty) {
    if (!value.is_string()) return false;
    const std::string &text = value.get_ref<const std::string &>();
    return (allow_empty || !text.empty()) && text.size() <= max_bytes;
}

// ---- The choice panel ----------------------------------------------------------------------------------------------

std::optional<Choice> g_choice;
int                   g_choice_grace = 0; // frames before the panel takes a press (the one that opened it isn't its)

void ChoiceClose(bool cancelled, std::size_t index) {
    if (!g_choice) return;
    const Choice choice = *g_choice;
    g_choice.reset();
    SharedRuntime().PublishToMod(choice.owner, "ui.choice_made",
                                 {{"id", choice.id}, {"index", cancelled ? Json(nullptr) : Json(index + 1)},
                                  {"cancelled", cancelled}, {"frame", SharedRuntime().Frame()}});
}

// ---- Title modes -----------------------------------------------------------------------------------------------------

std::vector<TitleMode>     g_title_modes;
std::optional<std::string> g_open_mode;
int                        g_ignored_backs = 0;
std::optional<VisitRequest> g_visit_request;
struct Visit {
    std::string owner, mode_id;
};
std::optional<Visit> g_visit;

std::string Qualify(std::string_view owner, std::string_view local) { return std::string(owner) + ":" + std::string(local); }

TitleMode *FindMutable(std::string_view id, std::string_view owner) {
    for (TitleMode &mode : g_title_modes) {
        if (mode.id == id || (!owner.empty() && mode.owner == owner && mode.local_id == id)) return &mode;
    }
    return nullptr;
}

const Json *ReadRequest(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return nullptr;
    return &args[0];
}

// Reads the page fields shared by mode_register and mode_set into mode. Fields left out keep their value.
ApiCallResult ReadPage(const Json &request, TitleMode &mode) {
    for (auto field = request.begin(); field != request.end(); ++field) {
        const std::string &key = field.key();
        if (key != "id" && key != "label" && key != "title" && key != "subtitle" && key != "hint" && key != "lines" &&
            key != "entries" && key != "cursor" && key != "root") {
            return Invalid("unsupported title mode field '" + key + "'");
        }
    }
    if (request.contains("label")) {
        if (!ValidText(request["label"], 32, false)) return Invalid("label must be 1-32 bytes of text");
        mode.label = request["label"].get<std::string>();
    }
    if (request.contains("title")) {
        if (!ValidText(request["title"], 40, false)) return Invalid("title must be 1-40 bytes of text");
        mode.title = request["title"].get<std::string>();
    }
    for (const char *key : {"subtitle", "hint"}) {
        if (!request.contains(key)) continue;
        if (!ValidText(request[key], 96, true)) return Invalid(std::string(key) + " must be at most 96 bytes of text");
        (std::string_view(key) == "subtitle" ? mode.subtitle : mode.hint) = request[key].get<std::string>();
    }
    if (request.contains("lines")) {
        const Json &lines = request["lines"];
        if (!lines.is_array() || lines.size() > kTitleModeLineLimit) return Invalid("lines must be an array of at most 8 strings");
        std::vector<std::string> out;
        for (const Json &line : lines) {
            if (!ValidText(line, 96, true)) return Invalid("each line must be at most 96 bytes of text");
            out.push_back(line.get<std::string>());
        }
        mode.lines = std::move(out);
    }
    if (request.contains("entries")) {
        const Json &entries = request["entries"];
        if (!entries.is_array() || entries.size() > kTitleModeEntryLimit) return Invalid("entries must be an array of at most 8 entries");
        std::vector<TitleModeEntry> out;
        for (const Json &entry : entries) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() ||
                !ValidLocalId(entry["id"].get_ref<const std::string &>()) || entry["id"] == "back") {
                return Invalid("each entry needs an id of 1-64 letters, digits, '.', '_' or '-' (not 'back')");
            }
            for (auto field = entry.begin(); field != entry.end(); ++field) {
                if (field.key() != "id" && field.key() != "label" && field.key() != "enabled") return Invalid("entries accept only id, label and enabled");
            }
            if (!entry.contains("label") || !ValidText(entry["label"], 64, false)) return Invalid("each entry needs a label of 1-64 bytes of text");
            if (entry.contains("enabled") && !entry["enabled"].is_boolean()) return Invalid("entry enabled must be a boolean");
            const std::string id = entry["id"].get<std::string>();
            if (std::ranges::find(out, id, &TitleModeEntry::id) != out.end()) return Invalid("entry ids must be unique");
            out.push_back({id, entry["label"].get<std::string>(), entry.value("enabled", true)});
        }
        mode.entries = std::move(out);
        if (!request.contains("cursor")) mode.cursor = 0;
    }
    if (request.contains("cursor")) {
        if (!request["cursor"].is_number_integer() || request["cursor"].get<std::int64_t>() < 0) return Invalid("cursor must be a 0-based entry index");
        mode.cursor = static_cast<std::size_t>(request["cursor"].get<std::int64_t>());
    }
    if (mode.cursor >= mode.entries.size()) mode.cursor = mode.entries.empty() ? 0 : mode.entries.size() - 1;
    if (request.contains("root")) {
        if (!request["root"].is_boolean()) return Invalid("root must be a boolean");
        mode.root = request["root"].get<bool>();
    }
    return {ApiCallStatus::Ok, nullptr, {}};
}

void CloseTitlePage() {
    g_open_mode.reset();
    g_ignored_backs = 0;
}

// The main map of a town (area 0-4): the Georama map of that area, else none.
int TownMainMap(int town) {
    const int count = GetMapNameCount();
    for (int map = 0; map < count; ++map) {
        if (GetMapType(map) == MAP_TYPE_GEORAMA && GetMapAreaNo(map) == town) return map;
    }
    return -1;
}

} // namespace

// ---- The choice panel: the API -------------------------------------------------------------------------------------

bool ChoiceOpen(Choice choice) {
    if (g_choice || choice.options.empty() || choice.options.size() > 6) return false;
    choice.cursor = std::min(choice.cursor, choice.options.size() - 1);
    g_choice = std::move(choice);
    g_choice_grace = 8;
    return true;
}

bool ChoiceIsOpen() { return g_choice.has_value(); }
const Choice *OpenChoice() { return g_choice ? &*g_choice : nullptr; }

void ChoiceStep() {
    if (!g_choice) return;
    const int loop = GetNowLoopNo();
    if (loop != LOOP_DUNGEON && loop != LOOP_EDIT && loop != LOOP_TITLE) {
        ChoiceClose(true, 0);
        return;
    }
    if (g_choice_grace > 0) {
        --g_choice_grace;
        return;
    }
    Choice &choice = *g_choice;
    if (GamePad__2.Down(PAD_UP) != 0) choice.cursor = (choice.cursor + choice.options.size() - 1) % choice.options.size();
    if (GamePad__2.Down(PAD_DOWN) != 0) choice.cursor = (choice.cursor + 1) % choice.options.size();
    const int push = ConvertCheckPushButton(MenuCheckPushButton());
    if (push & MENU_PUSH_BUTTON_DECIDE) {
        ChoiceClose(false, choice.cursor);
    } else if ((push & MENU_PUSH_BUTTON_CANCEL) && choice.cancel) {
        ChoiceClose(true, 0);
    }
}

void Draw2D() {
    if (!g_choice) return;
    const Choice &choice = *g_choice;
    const int rows = static_cast<int>(choice.options.size());
    const int height = 64 + rows * 40 + (choice.subtitle.empty() ? 0 : 20);
    const int top = std::max(20, (448 - height) / 2);
    Fill(96, top, 448, height, 8, 10, 24, 0x70);
    Fill(96, top, 448, 2, 128, 108, 64, 0x80);
    Fill(96, top + height - 2, 448, 2, 128, 108, 64, 0x80);
    int y = top + 10;
    Text(choice.title, 116, y, 408, true, 0x80, 18);
    y += 26;
    if (!choice.subtitle.empty()) {
        Text(choice.subtitle, 116, y, 408, false, 0x80, 13);
        y += 20;
    }
    for (int i = 0; i < rows; ++i) {
        const ChoiceOption &option = choice.options[static_cast<std::size_t>(i)];
        const bool          selected = static_cast<std::size_t>(i) == choice.cursor;
        if (selected) Fill(108, y - 2, 424, 38, 40, 36, 20, 0x60);
        Text(std::string(selected ? "> " : "  ") + option.title, 116, y, 408, selected, 0x80, 16);
        if (!option.subtitle.empty()) Text(option.subtitle, 136, y + 19, 388, false, 0x70, 12);
        y += 40;
    }
    Text(choice.cancel ? "Confirm picks; cancel backs out" : "Confirm picks", 116, top + height - 18, 408, false, 0x60, 11);
}

// ---- Title modes: the API ------------------------------------------------------------------------------------------

ApiCallResult RegisterTitleMode(std::string_view owner, const Json &args) {
    const Json *request = ReadRequest(args);
    if (request == nullptr || !request->contains("id") || !(*request)["id"].is_string()) {
        return Invalid("expected {id, label, title, subtitle?, hint?, lines?, entries?, cursor?, root?}");
    }
    const std::string local_id = (*request)["id"].get<std::string>();
    if (!ValidLocalId(local_id)) return Invalid("id must be 1-64 letters, digits, '.', '_' or '-'");
    if (!request->contains("label") || !request->contains("title")) return Invalid("a title mode needs a label and a title");
    const std::string id = Qualify(owner, local_id);
    if (TitleMode *existing = FindMutable(id, {})) {
        TitleMode updated = *existing;
        if (ApiCallResult read = ReadPage(*request, updated); read.status != ApiCallStatus::Ok) return read;
        *existing = std::move(updated);
        return {ApiCallStatus::Ok, TitleModeJson(*existing), {}};
    }
    if (g_title_modes.size() >= kTitleModeLimit) return Failed("the title menu has room for 2 mod title modes");
    TitleMode mode;
    mode.owner = std::string(owner);
    mode.id = id;
    mode.local_id = local_id;
    if (ApiCallResult read = ReadPage(*request, mode); read.status != ApiCallStatus::Ok) return read;
    g_title_modes.push_back(std::move(mode));
    return {ApiCallStatus::Ok, TitleModeJson(g_title_modes.back()), {}};
}

ApiCallResult SetTitleMode(std::string_view owner, const Json &args) {
    const Json *request = ReadRequest(args);
    if (request == nullptr || !request->contains("id") || !(*request)["id"].is_string()) {
        return Invalid("expected {id, title?, subtitle?, hint?, lines?, entries?, cursor?, root?, label?}");
    }
    TitleMode *mode = FindMutable((*request)["id"].get_ref<const std::string &>(), owner);
    if (mode == nullptr || mode->owner != owner) return Failed("no title mode with that id belongs to this mod");
    TitleMode updated = *mode;
    if (ApiCallResult read = ReadPage(*request, updated); read.status != ApiCallStatus::Ok) return read;
    *mode = std::move(updated);
    g_ignored_backs = 0;
    return {ApiCallStatus::Ok, TitleModeJson(*mode), {}};
}

ApiCallResult RemoveTitleMode(std::string_view owner, const Json &args) {
    const Json *request = ReadRequest(args);
    if (request == nullptr || !request->contains("id") || !(*request)["id"].is_string()) return Invalid("expected {id}");
    const TitleMode *mode = FindMutable((*request)["id"].get_ref<const std::string &>(), owner);
    if (mode == nullptr || mode->owner != owner) return Failed("no title mode with that id belongs to this mod");
    const std::string id = mode->id;
    if (g_open_mode == id) CloseTitlePage();
    std::erase_if(g_title_modes, [&id](const TitleMode &m) { return m.id == id; });
    return {ApiCallStatus::Ok, {{"id", id}, {"removed", true}}, {}};
}

const std::vector<TitleMode> &TitleModes() { return g_title_modes; }
const TitleMode              *OpenTitleMode() { return g_open_mode ? FindMutable(*g_open_mode, {}) : nullptr; }

Json TitleModeJson(const TitleMode &mode) {
    Json entries = Json::array();
    for (const TitleModeEntry &entry : mode.entries) entries.push_back({{"id", entry.id}, {"label", entry.label}, {"enabled", entry.enabled}});
    return {{"id", mode.id}, {"local_id", mode.local_id}, {"label", mode.label}, {"title", mode.title}, {"subtitle", mode.subtitle},
            {"hint", mode.hint}, {"lines", mode.lines}, {"entries", entries}, {"cursor", mode.cursor}, {"root", mode.root},
            {"open", g_open_mode == mode.id}};
}

int TitleExtraRows() { return static_cast<int>(g_title_modes.size()); }

bool TitleOpenRow(int row) {
    if (row < 0 || row >= static_cast<int>(g_title_modes.size())) return false;
    const TitleMode &mode = g_title_modes[static_cast<std::size_t>(row)];
    g_open_mode = mode.id;
    g_ignored_backs = 0;
    const std::string owner = mode.owner;
    const Json payload = {{"mode", mode.local_id}, {"qualified_id", mode.id}, {"frame", SharedRuntime().Frame()}};
    SharedRuntime().PublishToMod(owner, "game.title.opened", payload);
    return true;
}

bool TitlePageOpen() { return g_open_mode.has_value() && OpenTitleMode() != nullptr; }
void TitleClosePage() { CloseTitlePage(); }

TitlePageOutcome TitlePageStep(TitlePageInput input) {
    if (g_visit_request) return TitlePageOutcome::StartVisit;
    TitleMode *mode = g_open_mode ? FindMutable(*g_open_mode, {}) : nullptr;
    if (mode == nullptr) {
        CloseTitlePage();
        return TitlePageOutcome::ReturnToTitle;
    }
    switch (input) {
        case TitlePageInput::None: break;
        case TitlePageInput::Up:
            if (!mode->entries.empty()) mode->cursor = (mode->cursor + mode->entries.size() - 1) % mode->entries.size();
            break;
        case TitlePageInput::Down:
            if (!mode->entries.empty()) mode->cursor = (mode->cursor + 1) % mode->entries.size();
            break;
        case TitlePageInput::Confirm: {
            if (mode->entries.empty() || !mode->entries[mode->cursor].enabled) break;
            const std::string owner = mode->owner;
            const Json payload = {{"mode", mode->local_id}, {"qualified_id", mode->id}, {"entry", mode->entries[mode->cursor].id},
                                  {"index", mode->cursor}, {"action", "confirm"}, {"frame", SharedRuntime().Frame()}};
            SharedRuntime().PublishToMod(owner, "game.title.selected", payload);
            break;
        }
        case TitlePageInput::Back: {
            const std::string owner = mode->owner;
            if (mode->root) {
                const Json payload = {{"mode", mode->local_id}, {"qualified_id", mode->id}, {"entry", "back"}, {"closed", true},
                                      {"frame", SharedRuntime().Frame()}};
                CloseTitlePage();
                SharedRuntime().PublishToMod(owner, "game.title.selected", payload);
                g_visit_request.reset(); // the page is gone: nothing may start from it
                return TitlePageOutcome::ReturnToTitle;
            }
            // A sub-page: the mod answers. If it ignores two in a row, cancel leaves anyway.
            if (++g_ignored_backs > 2) {
                CloseTitlePage();
                return TitlePageOutcome::ReturnToTitle;
            }
            const Json payload = {{"mode", mode->local_id}, {"qualified_id", mode->id}, {"entry", "back"}, {"closed", false},
                                  {"frame", SharedRuntime().Frame()}};
            SharedRuntime().PublishToMod(owner, "game.title.selected", payload);
            break;
        }
    }
    if (g_visit_request) return TitlePageOutcome::StartVisit;
    if (!g_open_mode || FindMutable(*g_open_mode, {}) == nullptr) {
        CloseTitlePage();
        return TitlePageOutcome::ReturnToTitle;
    }
    return TitlePageOutcome::Stay;
}

void TitleDrawRows(int x, int y, int row_height, float alpha, int selected_row) {
    const int a = static_cast<int>(alpha);
    for (int i = 0; i < TitleExtraRows(); ++i) {
        const bool selected = i == selected_row;
        Fill(static_cast<int>(static_cast<float>(x) / kUiScaleX), y + i * row_height, 262, 40, 0, 0, 0, a / 3);
        Text(g_title_modes[static_cast<std::size_t>(i)].label, static_cast<int>(static_cast<float>(x) / kUiScaleX) + 16, y + i * row_height + 10, 230, selected, a, 18);
    }
}

void TitleDrawPage() {
    const TitleMode *mode = OpenTitleMode();
    if (mode == nullptr) return;
    Fill(40, 24, 560, 400, 6, 8, 20, 0x78);
    Fill(40, 24, 560, 2, 128, 108, 64, 0x80);
    int y = 36;
    Text(mode->title, 60, y, 520, true, 0x80, 20);
    y += 28;
    if (!mode->subtitle.empty()) {
        Text(mode->subtitle, 60, y, 520, false, 0x80, 13);
        y += 20;
    }
    for (const std::string &line : mode->lines) {
        Text(line, 60, y, 520, false, 0x78, 13);
        y += 18;
    }
    y += 8;
    for (std::size_t i = 0; i < mode->entries.size(); ++i) {
        const TitleModeEntry &entry = mode->entries[i];
        const bool            selected = i == mode->cursor;
        if (selected) Fill(52, y - 2, 536, 22, 40, 36, 20, 0x60);
        Text(std::string(selected ? "> " : "  ") + entry.label, 60, y, 520, selected && entry.enabled, entry.enabled ? 0x80 : 0x48, 15);
        y += 22;
    }
    Text(mode->hint.empty() ? (mode->root ? "Confirm picks; cancel returns to the title" : "Confirm picks; cancel goes back") : mode->hint,
         60, 404, 520, false, 0x60, 11);
}

// ---- Visits ----------------------------------------------------------------------------------------------------------

bool               VisitActive() { return g_visit.has_value(); }
const std::string *VisitOwner() { return g_visit ? &g_visit->owner : nullptr; }
bool               VisitRequestPending() { return g_visit_request.has_value(); }
void               QueueVisit(VisitRequest request) { g_visit_request = std::move(request); }

bool TitleBeginVisit(MENU_INIT_ARG &menu_arg) {
    std::optional<VisitRequest> request = std::move(g_visit_request);
    g_visit_request.reset();
    CloseTitlePage();
    if (!request) return false;
    const auto fail = [&request](const std::string &reason) {
        std::fprintf(stderr, "co-op: the visit could not start: %s\n", reason.c_str());
        SharedRuntime().PublishToMod(request->owner, "game.session.failed", {{"kind", "visit"}, {"reason", reason}});
        return false;
    };
    CSaveData *save = GetSaveData();
    if (save == nullptr) return fail("the game has no save data");
    const int map = TownMainMap(request->map);
    if (map < 0) return fail("no town map for area " + std::to_string(request->map) + " (the map catalog isn't loaded)");
    // A fresh game takes the host's world and the guest's own things (a new account keeps the new game's).
    InitSaveData();
    std::string error;
    if (!ImportWorld(request->world, *save, error)) return fail(error);
    if (!request->player.empty() && !ImportPlayer(request->player, *save, error)) return fail(error);
    save->map_no = static_cast<s16>(map);
    save->sub_map_no = -1;
    save->prev_map_no = static_cast<s16>(map);
    save->area_no = request->map;
    menu_arg.result[0] = LOOP_EDIT;
    menu_arg.result[1] = map;
    g_visit = Visit{request->owner, request->mode_id};
    SharedRuntime().Publish("game.session.started", {{"kind", "visit"}, {"owner", request->owner}, {"mode", request->mode_id},
                                                      {"map", request->map}, {"frame", SharedRuntime().Frame()}});
    return true;
}

void EndVisit(std::string_view reason) {
    if (!g_visit) return;
    const Visit ended = *g_visit;
    g_visit.reset();
    SharedRuntime().Publish("game.session.ended", {{"kind", "visit"}, {"owner", ended.owner}, {"mode", ended.mode_id},
                                                    {"reason", std::string(reason)}, {"frame", SharedRuntime().Frame()}});
}

Json SessionJson() {
    if (g_visit) return {{"kind", "visit"}, {"owner", g_visit->owner}, {"mode", g_visit->mode_id}};
    const bool title = GetNowLoopNo() == LOOP_TITLE;
    return {{"kind", title ? "title" : "story"}, {"owner", nullptr}, {"mode", nullptr},
            {"open_mode", g_open_mode ? Json(*g_open_mode) : Json(nullptr)}};
}

// ---- Each frame, and the mod's end -----------------------------------------------------------------------------------

void UiStep() {
    ChoiceStep();
    // A visit ends when the game is back at the title (the host left, or the player went home).
    if (g_visit && GetNowLoopNo() == LOOP_TITLE) EndVisit("left");
}

void UiOwnerUnloaded(std::string_view owner) {
    if (g_open_mode) {
        const TitleMode *open = OpenTitleMode();
        if (open != nullptr && open->owner == owner) CloseTitlePage();
    }
    std::erase_if(g_title_modes, [owner](const TitleMode &mode) { return mode.owner == owner; });
    if (g_choice && g_choice->owner == owner) g_choice.reset();
    if (g_visit_request && g_visit_request->owner == owner) g_visit_request.reset();
}

void UiReset() {
    g_choice.reset();
    g_choice_grace = 0;
    g_title_modes.clear();
    g_open_mode.reset();
    g_ignored_backs = 0;
    g_visit_request.reset();
    g_visit.reset();
}

} // namespace moonmod::coop
