// Co-op's screens (moonmod/coop.hpp): the choice panel over the game, a mod's title mode page, and a visit's start
// and end.

#include "moonmod/coop.hpp"
#include "moonmod/coop_pad.hpp"
#include "moonmod/coop_save.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "dng_event.hpp"
#include "editloop.hpp"
#include "font.hpp"
#include "gamepad.hpp"
#include "mainloop.hpp"
#include "mapselect.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "menusys.hpp"
#include "menucommon.hpp"
#include "mglib.hpp"
#include "nd_meswin.hpp"
#include "snd_mngr.hpp"
#include "sysmes.hpp"
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
bool                  g_choice_reads = false; // the panel is reading the pad now (PanelHoldsPad lets it through)
// The game's own message window a yes-or-no choice is asked in (Choice::yes_no), built when it is first drawn: its
// frame and letters are the system message's, which every floor and town loads (dng_main.cpp, editloop.cpp).
ClsMes g_ask;
bool   g_ask_built = false;

// A town's notices: a message window and its queue of co-op's own, made as dng_main.cpp makes the floor's (DngMess,
// MsgTaskMan) with the system message's frame and letters, the first time a notice comes in a town.
ClsMes             g_town_mes;
MessageTaskManager g_town_tasks;
bool               g_town_ready = false;

bool WalkingInTown() { return GetNowLoopNo() == LOOP_EDIT && GetMainScene() != nullptr && IsEditMode() == 0; }

void TownNoticesDraw() {
    if (!g_town_ready || g_town_tasks.top == nullptr) return;
    if (GetNowLoopNo() != LOOP_EDIT || GetMainScene() == nullptr) {
        // (out of the town: what was left goes, and the window closes)
        g_town_tasks.Initialize();
        g_town_tasks.mes = &g_town_mes;
        g_town_mes.open = 0;
        g_town_mes.fade = 0.0f;
        return;
    }
    if (g_choice || IsEditMode() != 0) return; // (it waits while the window is a question's, or the editor is open)
    g_town_tasks.Step();
    g_town_tasks.Draw();
}

// The window's text: the title, then the subtitle, each broken into lines as the game's messages are (at most four).
std::string AskText(const Choice &choice) {
    std::string text;
    int         lines = 0;
    for (const std::string *part : {&choice.title, &choice.subtitle}) {
        for (const std::string &window : GameNoticeText(*part, 52)) { // (a question's window takes a longer line)
            std::size_t start = 0;
            while (start <= window.size() && lines < 4) {
                const std::size_t end = window.find('\n', start);
                const std::string line = window.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!line.empty()) {
                    if (!text.empty()) text += '\n';
                    text += line;
                    ++lines;
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
    }
    return text;
}

void AskBuild(const Choice &choice) {
    const ClsMes *system = GetSystemMessage();
    g_ask.Init();
    g_ask.Preset(MES_PRESET_WINDOW);
    g_ask.texture_block = system != nullptr ? system->texture_block : 0;
    g_ask.SetWindowMode(MES_WIN_YESNO);
    g_ask.fuchi = FUCHI_SHADOW_BLACK_WIDE;
    const std::string text = AskText(choice);
    g_ask.MakeMesWin(text.c_str(), 1, 1);
    g_ask.cursor_time = 0;
    g_ask_built = true;
}

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
    bool        left_title = false; // the game has gone from the title into the host's world
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
    const int loop = GetNowLoopNo();
    if (choice.options.size() != 2 || (loop != LOOP_DUNGEON && loop != LOOP_EDIT)) choice.yes_no = false;
    g_choice = std::move(choice);
    g_choice_grace = 8;
    g_ask_built = false;
    return true;
}

bool ChoiceCloseQuietly(std::string_view owner) {
    if (!g_choice || g_choice->owner != owner) return false;
    g_choice.reset();
    g_choice_grace = 0;
    return true;
}

bool ChoiceIsOpen() { return g_choice.has_value(); }
bool PanelHoldsPad() { return g_choice.has_value() && !g_choice_reads; }
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
    // The buttons are the panel's while it is open (PanelHoldsPad): only its own reads see them.
    g_choice_reads = true;
    const bool up = GamePad__2.Down(PAD_UP) != 0, down = GamePad__2.Down(PAD_DOWN) != 0;
    const bool left = GamePad__2.Down(PAD_LEFT) != 0, right = GamePad__2.Down(PAD_RIGHT) != 0;
    const int  push = ConvertCheckPushButton(MenuCheckPushButton());
    g_choice_reads = false;
    const std::size_t before = choice.cursor;
    if (choice.yes_no) {
        // Yes on the left, No on the right, as the game's own (CDC2Mes::YesNoCursor): left and right choose.
        if (left) choice.cursor = 0;
        if (right) choice.cursor = 1;
    } else {
        if (up) choice.cursor = (choice.cursor + choice.options.size() - 1) % choice.options.size();
        if (down) choice.cursor = (choice.cursor + 1) % choice.options.size();
    }
    // And it sounds as the game's menus do: the cursor's tick, the pick's chime, the back-out's.
    if (choice.cursor != before) MenuSePlay(SYSTEM_SE_CURSOR);
    if (push & MENU_PUSH_BUTTON_DECIDE) {
        MenuSePlay(SYSTEM_SE_DECIDE);
        ChoiceClose(false, choice.cursor);
    } else if ((push & MENU_PUSH_BUTTON_CANCEL) && choice.cancel) {
        MenuSePlay(SYSTEM_SE_CANCEL);
        ChoiceClose(true, 0);
    }
}

bool TownNoticesOpen() { return WalkingInTown() && !g_choice; }

void TownNoticePrint(const std::string &window, int frames) {
    if (!g_town_ready) {
        const ClsMes *system = GetSystemMessage();
        g_town_mes.Init();
        g_town_mes.Preset(MES_PRESET_WINDOW);
        g_town_mes.SetWindowMode(7);
        g_town_mes.push_button = 0;
        g_town_mes.draw_speed_def = 0;
        g_town_mes.draw_speed = 0;
        g_town_mes.texture_block = system != nullptr ? system->texture_block : 0;
        if (LanguageCode > LANG_JAPANESE && LanguageCode < LANG_CHINESE) g_town_mes.value_half = 1;
        g_town_tasks.Initialize();
        g_town_tasks.mes = &g_town_mes;
        g_town_ready = true;
    }
    std::string text = window;
    g_town_tasks.Print(text.data(), frames, 8, 0);
}

void Draw2D() {
    DrawAvatarNames(); // the other players' names, under the panel
    TownNoticesDraw();
    if (!g_choice) return;
    const Choice &choice = *g_choice;
    if (choice.yes_no) {
        if (!g_ask_built) AskBuild(choice);
        if (g_ask.select < 0) g_ask.cursor_time = 0;
        g_ask.select = static_cast<int>(choice.cursor);
        g_ask.fukidashi_pos = 5; // (the middle of the screen, as the port's own yes-or-no asks)
        g_ask.Step();
        g_ask.DrawMesWin();
        return;
    }
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
    // The place: a map by its own number (Palm Brinks, which has no Georama map, has no other name), else the
    // Georama map of a town's area.
    const bool by_number = request->map_no >= 0;
    const int  map = by_number ? request->map_no : TownMainMap(request->map);
    if (map < 0) return fail("no town map for area " + std::to_string(request->map));
    if (GetMapType(map) < 0) return fail("no map has the number " + std::to_string(map));
    // A fresh game takes the host's world and the guest's own things (a new account keeps the new game's).
    InitSaveData();
    std::string error;
    if (!ImportWorld(request->world, *save, error)) return fail(error);
    if (!request->player.empty() && !ImportPlayer(request->player, *save, error)) return fail(error);
    // The room's hero is the one the visit starts as (the body is built for the active character as the map loads).
    if (const std::optional<int> hero = RoomHero()) save->user_data.active_chr_no = static_cast<s16>(*hero);
    // Palm Brinks is map 10 with its districts as sub maps 10-14, and the title's load path walks into the one the
    // world holds: the host's. Anywhere else a sub map is a house, and a visit starts outside.
    if (!(by_number && map == 10)) save->sub_map_no = -1;
    save->map_no = static_cast<s16>(map);
    save->prev_map_no = static_cast<s16>(map);
    save->area_no = by_number ? GetMapAreaNo(map) : request->map;
    menu_arg.result[0] = LOOP_EDIT;
    menu_arg.result[1] = map;
    g_visit = Visit{request->owner, request->mode_id};
    SharedRuntime().Publish("game.session.started", {{"kind", "visit"}, {"owner", request->owner}, {"mode", request->mode_id},
                                                      {"map", save->area_no}, {"map_no", map}, {"frame", SharedRuntime().Frame()}});
    return true;
}

void VisitStartForTests(std::string_view owner) { g_visit = Visit{std::string(owner), "test"}; }

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
    // It begins at the title too, and stays there until the world has loaded: only a return counts.
    if (g_visit) {
        if (GetNowLoopNo() != LOOP_TITLE) g_visit->left_title = true;
        else if (g_visit->left_title) EndVisit("left");
    }
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
