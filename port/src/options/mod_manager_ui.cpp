#include "options/mod_manager_ui.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "font.hpp"
#include "menudraw.hpp"
#include "moonmod/manager_ui.hpp"
#include "moonmod/runtime.hpp"

namespace options::mod_manager_ui {
namespace {

constexpr float kUiScaleX = 0.8f; // The Chronicle Two PC canvas is 512 units wide; manager input uses a 640-wide layout.

float UiX(int x) { return static_cast<float>(x) * kUiScaleX; }

void Fill(int x, int y, int width, int height, int red, int green, int blue, int alpha) {
    DrawMenuFillBox(UiX(x), static_cast<float>(y), UiX(width), static_cast<float>(height),
                    static_cast<unsigned char>(std::clamp(alpha, 0, 0x80)), red, green, blue);
}

void Text(int slot, std::string value, int x, int y, int width, bool selected, int alpha, int size = 16) {
    (void)slot;
    CFont font;
    const int glyph_width = std::max(1, static_cast<int>(static_cast<float>(size) * kUiScaleX));
    font.SetDrawSize(glyph_width, size + 4);
    font.SetClearance(glyph_width, size + 5);
    font.SetFuchi(FUCHI_SHADOW_BLACK);
    font.SetColor(selected ? 128 : 118, selected ? 108 : 118, selected ? 64 : 118,
                  static_cast<unsigned char>(std::clamp(alpha, 0, 0x80)));
    int measured_width = 0;
    int measured_height = 0;
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

std::string ModTitle(const moonmod::ModRecord &record) {
    return std::format("{}  {}", record.display_name, record.manifest.version);
}

std::string JournalStatus(std::string status) {
    if (status == "completed") return "DONE";
    if (status == "registered") return "NEW";
    if (status == "failed") return "FAILED";
    return status == "active" ? "ACTIVE" : status;
}

std::vector<std::string> NativePermissionLines(const moonmod::ModRecord *record) {
    std::vector<std::string> lines;
    std::string line = "Permissions:";
    if (record == nullptr || record->manifest.permissions.empty()) {
        lines.push_back("Permissions: none");
        return lines;
    }
    std::vector<std::string> sorted = record->manifest.permissions;
    std::ranges::sort(sorted);
    for (const std::string &permission : sorted) {
        const std::string separator = line == "Permissions:" ? " " : ", ";
        if (line.size() + separator.size() + permission.size() > 46 && line != "Permissions:") {
            lines.push_back(std::move(line));
            line = "  " + permission;
        } else {
            line += separator + permission;
        }
    }
    lines.push_back(std::move(line));
    return lines;
}

} // namespace

void Draw() {
    using namespace moonmod;
    constexpr int alpha = 0x80;
    const ManagerUiState &state = ManagerState();
    const auto           mods = SharedRuntime().Mods();
    const auto           journal_quests = ManagerJournalQuests();
    const bool           journal_tab = state.detail_tab == 4 && !journal_quests.empty();
    Fill(36, 66, 568, 396, 14, 17, 23, alpha);
    Fill(39, 69, 562, 390, 39, 44, 54, alpha);
    Fill(43, 73, 554, 382, 22, 25, 31, alpha);
    Fill(45, 111, 198, 274, 12, 14, 18, alpha);
    Fill(252, 111, 344, 274, 12, 14, 18, alpha);
    Fill(247, 113, 2, 270, 77, 81, 89, alpha);
    Fill(45, 397, 550, 55, 22, 25, 31, alpha);

    Text(0, "MOON FACTORY  /  MOD MANAGER", 58, 79, 440, true, alpha, 18);
    Text(1, journal_tab ? "QUEST JOURNAL" : "LOCAL PACKAGES", 56, 116, 166, true, alpha, 14);
    Text(2, journal_tab ? "QUEST DETAILS" : "DETAILS AND SETTINGS", 265, 116, 294, true, alpha, 14);
    Fill(55, 136, 174, 1, 91, 95, 103, alpha);
    Fill(264, 136, 320, 1, 91, 95, 103, alpha);

    if (mods.empty()) {
        Text(10, "No local mods found.", 62, 159, 164, false, alpha);
        Text(11, "Create a package folder in", 62, 189, 164, false, alpha, 14);
        Text(12, "save/mods and add mod.json", 62, 211, 164, false, alpha, 14);
        Text(13, "plus a Lua entry script.", 62, 233, 164, false, alpha, 14);
        Text(14, "See the MoonFactory example.", 62, 255, 164, false, alpha, 14);
        Text(15, "The package can target either game.", 268, 160, 300, false, alpha, 14);
    } else {
        if (journal_tab) {
            const int entry_end = std::min(static_cast<int>(journal_quests.size()), state.first_journal_quest + kManagerVisibleMods);
            for (int row = state.first_journal_quest; row < entry_end; ++row) {
                const ManagerJournalQuest &entry = journal_quests[static_cast<std::size_t>(row)];
                const int y = 143 + (row - state.first_journal_quest) * 29;
                const bool selected = row == state.selected_journal_quest;
                Fill(49, y - 2, 190, 26, selected ? 69 : 31, selected ? 54 : 33, selected ? 36 : 40, alpha);
                Text(20 + row, entry.quest.value("title", std::string("Untitled quest")), 56, y + 1, 122, selected, alpha, 13);
                Text(40 + row, JournalStatus(entry.quest.value("status", std::string("unknown"))),
                     181, y + 2, 52, selected, alpha, 10);
            }
            if (journal_quests.empty()) {
                Text(10, "No mod quests are registered.", 58, 159, 174, false, alpha, 12);
                Text(11, "Quest packages appear here.", 58, 181, 174, false, alpha, 11);
            }
        } else {
            const int mod_end = std::min(static_cast<int>(mods.size()), state.first_mod + kManagerVisibleMods);
            for (int row = state.first_mod; row < mod_end; ++row) {
                const ModRecord &record = mods[static_cast<std::size_t>(row)];
                const int y = 143 + (row - state.first_mod) * 29;
                const bool selected = row == state.selected_mod;
                Fill(49, y - 2, 190, 26, selected ? 69 : 31, selected ? 54 : 33, selected ? 36 : 40, alpha);
                Text(20 + row, record.display_name, 56, y + 1, 126, selected, alpha, 14);
                const bool has_native = !record.manifest.native_library.empty();
                Fill(184, y, has_native ? 31 : 50, 21, record.enabled_next_launch ? 34 : 59,
                     record.enabled_next_launch ? 82 : 44, record.enabled_next_launch ? 58 : 41, alpha);
                Text(40 + row, std::string(record.enabled_next_launch ? "ON" : "OFF") + (record.restart_required ? "*" : ""),
                     187, y + 2, has_native ? 26 : 43, selected, alpha, 13);
                if (has_native) {
                    Fill(217, y, 26, 21,
                         state.native_trust_confirmation && selected ? 104 : record.native_trusted ? 34 : 82,
                         record.native_trusted ? 82 : 48, record.native_trusted ? 58 : 35, alpha);
                    Text(140 + row, record.native_trusted ? "N+" : "N?", 219, y + 2, 22,
                         selected && state.native_trust_focus, alpha, 12);
                }
            }
        }
        if (const ModRecord *record = ManagerSelectedMod()) {
            if (!journal_tab) {
                Text(60, ModTitle(*record), 269, 145, 315, true, alpha, 18);
                const std::string author = record->manifest.author.empty() ? "Unknown author" : record->manifest.author;
                Text(61, std::format("{}  /  {}", author, ModStateText(record->state)), 269, 169, 315, false, alpha, 13);
                Text(62, record->manifest.description.empty() ? record->status_message : record->manifest.description,
                     269, 190, 315, false, alpha, 13);
                Text(63, record->restart_required ? "Restart the game to apply the ON/OFF change." : record->status_message,
                     269, 211, 315, record->restart_required, alpha, 12);
                if (!record->native_status.empty()) Text(66, record->native_status, 269, 221, 315, false, alpha, 10);
            }
            const auto actions = ManagerDisplayActions(record->id);
            const ApiCallResult quest_result = ManagerQuests(record->id);
            const Json quests = quest_result.status == ApiCallStatus::Ok && quest_result.value.is_object()
                                    ? quest_result.value.value("quests", Json::array())
                                    : Json::array();
            const ApiCallResult skill_result = ManagerSkills(record->id);
            const Json skills = skill_result.status == ApiCallStatus::Ok && skill_result.value.is_object()
                                    ? skill_result.value.value("skills", Json::array())
                                    : Json::array();
            const bool has_quests = quests.is_array() && !quests.empty();
            const bool has_skills = skills.is_array() && !skills.empty();
            const bool action_tab = state.detail_tab == 1 && !actions.empty();
            const bool quest_tab = state.detail_tab == 2 && has_quests;
            const bool skill_tab = state.detail_tab == 3 && has_skills;
            const bool manager_journal_tab = state.detail_tab == 4 && !journal_quests.empty();
            const int tab_y = manager_journal_tab ? 139 : 230;
            const auto draw_tab = [&](int index, std::string label, bool available, int text_id) {
                const int x = 263 + index * 64;
                const bool selected = state.settings_focus && state.detail_tab == index && available;
                if (selected) Fill(x, tab_y, 62, 23, 67, 55, 37, alpha);
                Text(text_id, std::move(label), x + 2, tab_y + 5, 58, selected, alpha, 10);
            };
            draw_tab(0, "SETTINGS", !record->manifest.settings.empty(), 64);
            draw_tab(1, "ACTIONS", !actions.empty(), 65);
            draw_tab(2, "QUESTS", has_quests, 67);
            draw_tab(3, "SKILLS", has_skills, 68);
            draw_tab(4, "JOURNAL", !journal_quests.empty(), 69);
            Fill(266, tab_y + 24, 320, 1, 77, 81, 89, alpha);
            if (manager_journal_tab) {
                if (state.selected_journal_quest >= 0 && state.selected_journal_quest < static_cast<int>(journal_quests.size())) {
                    const ManagerJournalQuest &entry = journal_quests[static_cast<std::size_t>(state.selected_journal_quest)];
                    const Json &quest = entry.quest;
                    const Json objectives = quest.value("objectives", Json::array());
                    const int completed = objectives.is_array()
                                              ? static_cast<int>(std::ranges::count_if(objectives, [](const Json &objective) {
                                                    return objective.value("complete", false);
                                                }))
                                              : 0;
                    Text(400, std::format("{}  /  {}/{} objectives",
                                          JournalStatus(quest.value("status", std::string("unknown"))),
                                          completed, objectives.size()),
                         270, 170, 310, false, alpha, 12);
                    Text(401, quest.value("title", std::string("Untitled quest")), 270, 192, 310, true, alpha, 14);
                    Text(402, entry.mod_name, 270, 214, 310, false, alpha, 10);
                    Text(403, quest.value("description", std::string("")), 270, 236, 310, false, alpha, 10);
                    Text(404, ManagerQuestRewardSummary(quest), 270, 258, 310, false, alpha, 10);
                    if (objectives.is_array()) {
                        for (int row = 0; row < kManagerVisibleJournalObjectives; ++row) {
                            const int index = state.first_quest_objective + row;
                            if (index >= static_cast<int>(objectives.size())) break;
                            const Json &objective = objectives[static_cast<std::size_t>(index)];
                            const int y = 281 + row * 22;
                            const int target = std::max(1, objective.value("target", 1));
                            const int progress = std::clamp(objective.value("progress", 0), 0, target);
                            Text(410 + row, objective.value("title", std::string("Objective")), 270, y, 230,
                                 state.settings_focus && row == 0, alpha, 9);
                            Text(420 + row, std::format("{}/{}", progress, target),
                                 510, y, 65, state.settings_focus && row == 0, alpha, 9);
                            Fill(270, y + 21, 300, 2, 62, 67, 75, alpha);
                            if (progress > 0) Fill(270, y + 21, 300 * progress / target, 2, 29, 169, 112, alpha);
                        }
                    }
                } else {
                    Text(400, "Select a quest to see its progress.", 270, 170, 310, false, alpha, 12);
                }
            } else if (action_tab) {
                const int action_end = std::min(static_cast<int>(actions.size()),
                                                state.first_action + kManagerVisibleSettings);
                for (int row = state.first_action; row < action_end; ++row) {
                    const ManagerUiAction &action = actions[static_cast<std::size_t>(row)];
                    const int y = 262 + (row - state.first_action) * 24;
                    const bool selected = state.settings_focus && row == state.selected_action;
                    if (selected) Fill(263, y - 2, 326, 23, 67, 55, 37, alpha);
                    Text(120 + row, action.label, 270, y, 174, selected, alpha, 13);
                    Text(130 + row, action.description, 447, y, 134, false, alpha, 12);
                }
            } else if (quest_tab && state.selected_quest >= 0 && state.selected_quest < static_cast<int>(quests.size())) {
                const Json &quest = quests[static_cast<std::size_t>(state.selected_quest)];
                const Json objectives = quest.value("objectives", Json::array());
                const int completed = objectives.is_array()
                                          ? static_cast<int>(std::ranges::count_if(objectives, [](const Json &objective) {
                                                return objective.value("complete", false);
                                            }))
                                          : 0;
                Text(200, std::format("QUEST {}/{}  /  {}  /  {}/{} objectives",
                                      state.selected_quest + 1, quests.size(), quest.value("status", std::string("unknown")),
                                      completed, objectives.size()), 270, 260, 310, false, alpha, 12);
                Text(201, quest.value("title", std::string("Untitled quest")), 270, 279, 310, true, alpha, 14);
                Text(202, quest.value("description", std::string("")), 270, 298, 310, false, alpha, 11);
                Text(203, ManagerQuestRewardSummary(quest), 270, 313, 310, false, alpha, 11);
                if (objectives.is_array()) {
                    for (int row = 0; row < kManagerVisibleQuestObjectives; ++row) {
                        const int index = state.first_quest_objective + row;
                        if (index >= static_cast<int>(objectives.size())) break;
                        const Json &objective = objectives[static_cast<std::size_t>(index)];
                        const std::string progress = std::format("{}  {}/{}", objective.value("title", std::string("Objective")),
                                                                  objective.value("progress", 0), objective.value("target", 0));
                        Text(210 + row, progress, 270, 333 + row * 16, 310,
                             state.settings_focus && row == 0, alpha, 11);
                    }
                }
            } else if (skill_tab && state.selected_skill >= 0 && state.selected_skill < static_cast<int>(skills.size())) {
                // The full-width branch layout is drawn below.
            } else {
                const int setting_end = std::min(static_cast<int>(record->manifest.settings.size()),
                                                 state.first_setting + kManagerVisibleSettings);
                for (int row = state.first_setting; row < setting_end; ++row) {
                    const SettingDefinition &setting = record->manifest.settings[static_cast<std::size_t>(row)];
                    const int y = 262 + (row - state.first_setting) * 24;
                    const bool selected = state.settings_focus && row == state.selected_setting;
                    if (selected) Fill(263, y - 2, 326, 23, 67, 55, 37, alpha);
                    Text(80 + row, setting.name, 270, y, 232, selected, alpha, 13);
                    Text(180 + row, "<", 506, y, 18, selected, alpha, 13);
                    Text(90 + row, SettingValueText(setting, SharedRuntime().Setting(record->id, setting.id)),
                         526, y, 36, selected, alpha, 13);
                    Text(190 + row, ">", 570, y, 18, selected, alpha, 13);
                }
                if (record->manifest.settings.empty()) {
                    Text(100, "This mod has no generated settings.", 270, 266, 300, false, alpha, 13);
                }
            }
        }
    }

    // The shared controller uses branch columns and 55-unit node rows on the Skills tab.
    // Draw those same bounds so mouse selection and the visible nodes agree.
    if (state.detail_tab == 3) {
        if (const ModRecord *record = ManagerSelectedMod()) {
            const auto result = ManagerSkills(record->id);
            const Json skills = result.status == ApiCallStatus::Ok && result.value.is_object()
                                    ? result.value.value("skills", Json::array()) : Json::array();
            if (!skills.empty()) {
                Fill(45, 111, 550, 274, 12, 14, 18, alpha);
                Text(450, record->display_name + " / SKILLS", 56, 116, 526, true, alpha, 14);
                std::vector<std::string> branches;
                for (const auto &skill : skills) {
                    const auto tree = skill.value("tree_title", std::string{});
                    if (std::ranges::find(branches, tree) == branches.end()) branches.push_back(tree);
                }
                const int width = 536 / std::max(1, static_cast<int>(branches.size()));
                for (std::size_t column = 0; column < branches.size(); ++column) {
                    const int x = 56 + static_cast<int>(column) * width;
                    Text(451, branches[column], x + 4, 136, std::max(1, width - 12), true, alpha, 10);
                    int row = 0;
                    for (std::size_t index = 0; index < skills.size(); ++index) {
                        const auto &skill = skills[index];
                        if (skill.value("tree_title", std::string{}) != branches[column]) continue;
                        const int node_row = row++;
                        if (node_row >= 4) continue;
                        const int y = 155 + node_row * 55;
                        const bool selected = state.settings_focus && static_cast<int>(index) == state.selected_skill;
                        Fill(x, y, std::max(1, width - 8), 48, selected ? 67 : 31, selected ? 55 : 33,
                             selected ? 37 : 40, alpha);
                        Text(452, skill.value("title", std::string("Skill")), x + 4, y + 4,
                             std::max(1, width - 16), selected, alpha, 11);
                        const int rank = skill.value("rank", 0);
                        const int maximum = skill.value("max_rank", 1);
                        const auto status = rank >= maximum ? "MAX" : skill.value("prerequisites_met", false) ? "READY" : "LOCKED";
                        Text(453, std::format("{}/{}  {}", rank, maximum, status), x + 4, y + 24,
                             std::max(1, width - 16), selected, alpha, 10);
                    }
                }
                Text(454, "Up/Down node   Left/Right tree   Confirm train   L1/R1 tab", 56, 375, 526, false, alpha, 10);
            }
        }
    }

    Fill(43, 394, 554, 1, 77, 81, 89, alpha);
    std::string footer = state.detail_tab == 4
                             ? "Up/Down list/objectives   Left/Right quest   Click list to select"
                             : state.detail_tab == 2
                             ? "Up/Down objectives   Left/Right quest   L1/R1 switch tab"
                             : state.detail_tab == 3
                                   ? "Up/Down inspect skills   L1/R1 switch tab"
                                   : "Up/Down select   Left/Right change   L1/R1 switch tab";
    if (state.detail_tab == 3) {
        const ModRecord *record = ManagerSelectedMod();
        if (record != nullptr) {
            const ApiCallResult result = ManagerSkills(record->id);
            if (result.status == ApiCallStatus::Ok && result.value.is_object() &&
                result.value.contains("skills") && result.value["skills"].is_array() &&
                state.selected_skill >= 0 && state.selected_skill < static_cast<int>(result.value["skills"].size())) {
                const std::string description = result.value["skills"][static_cast<std::size_t>(state.selected_skill)]
                                                    .value("description", std::string{});
                if (!description.empty()) footer = description;
            }
        }
    }
    Text(110, state.notice.empty() ? footer : state.notice, 53, 400, 475, state.notice_frames > 0, alpha, 13);
    Fill(47, 422, 95, 25, 65, 49, 34, alpha);
    Text(111, "Back", 56, 426, 78, false, alpha, 13);
    Text(112, state.detail_tab == 4 ? "Journal progress updates as you play"
                                   : state.detail_tab == 3 ? "Confirm requests training from this skill package"
                                   : "Confirm runs an action or changes the selected setting",
         150, 426, 420, false, alpha, 12);

    if (state.native_trust_confirmation) {
        const ModRecord *record = ManagerSelectedMod();
        Fill(142, 168, 366, 182, 5, 7, 11, alpha);
        Fill(146, 172, 358, 174, 39, 44, 54, alpha);
        Fill(150, 176, 350, 166, 16, 19, 26, alpha);
        Text(150, state.pending_native_trust ? "TRUST NATIVE CODE?" : "REMOVE NATIVE TRUST?",
             165, 185, 320, true, alpha, 16);
        Text(151, "This library runs inside the game with your user permissions.", 165, 212, 320, false, alpha, 11);
        if (record != nullptr) {
            Text(152, record->id, 165, 234, 320, true, alpha, 12);
            Text(153, record->manifest.native_library.lexically_relative(record->package_root).generic_string(),
                 165, 254, 320, false, alpha, 10);
        }
        const std::vector<std::string> permissions = NativePermissionLines(record);
        for (std::size_t index = 0; index < permissions.size() && index < 3; ++index) {
            Text(156 + static_cast<int>(index), permissions[index], 165, 274 + static_cast<int>(index) * 14,
                 320, false, alpha, 10);
        }
        Fill(300, 318, 94, 27, 49, 90, 61, alpha);
        Fill(406, 318, 87, 27, 64, 56, 52, alpha);
        Text(154, "Confirm", 306, 323, 82, true, alpha, 12);
        Text(155, "Cancel", 412, 323, 76, false, alpha, 12);
    }
}

} // namespace options::mod_manager_ui
