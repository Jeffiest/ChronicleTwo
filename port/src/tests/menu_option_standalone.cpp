#include <array>
#include <cstdio>
#include <cstring>

#include "mainloop.hpp"
#include "menuop.hpp"

void PortSetMenuOptionFormForTest(int enabled);

int LanguageCode = LANG_ENGLISH;

CBaseMenuClass::CBaseMenuClass() {}
MENU_ASKMODE_PARA::MENU_ASKMODE_PARA() {}

namespace {

bool Check(bool condition, const char *message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

bool IsChoice(const MENUFORMPARTS_TYPE &part, bool selected) {
    const u8 expected = selected ? 0x80 : 0x40;
    return part.rgba[0] == expected && part.rgba[1] == expected && part.rgba[2] == expected;
}

bool Run(int map_choice, int enemy_hp, int language) {
    CMenuOption option;
    std::array<std::array<MENUFORMPARTS_TYPE, OPTION_BUTTON_NUM>, OPTION_ITEM_MAX> parts{};
    for (int item = 0; item < OPTION_ITEM_MAX; ++item) {
        for (int choice = 0; choice < OPTION_BUTTON_NUM; ++choice) {
            option.button[item][choice] = &parts[item][choice];
            std::memset(parts[item][choice].rgba, 0xA5, sizeof(parts[item][choice].rgba));
        }
    }

    option.config = {};
    option.config.cursor_save = 2;
    option.config.vibration = 1;
    option.config.message_speed = 0;
    option.config.sound_mode = 2;
    option.config.fast_time = 1;
    option.config.map = map_choice;
    option.config.damage_off = 2;
    option.config.enemy_hp = enemy_hp;
    option.config.anger_counter = 2;
    option.config.unk_24 = 1;
    option.config.monster_name = 2;
    option.config.dof_off = 0;
    option.config.caption_off = 1;
    option.config.pause_overlay_off = 2;
    option.config.eye_reverse = 1;
    option.config.rot_normal = 2;

    LanguageCode = language;
    PortSetMenuOptionFormForTest(1);
    option.UpdateOptionForm();

    bool ok = true;
    const int expected[] = {2, 1, 0, 2, 1, map_choice, 2, enemy_hp, enemy_hp == 0 ? 2 : -1,
                            1, 2, 0, 1, 2, 1, 2};
    for (int item = 0; item < 16; ++item) {
        for (int choice = 0; choice < OPTION_BUTTON_NUM; ++choice) {
            if (language == LANG_JAPANESE && item >= OPTION_ITEM_CAMERA_VERTICAL) {
                ok &= Check(parts[item][choice].rgba[0] == 0xA5 &&
                                parts[item][choice].rgba[1] == 0xA5 &&
                                parts[item][choice].rgba[2] == 0xA5,
                            "Japanese leaves the two language-specific camera rows untouched");
                continue;
            }
            ok &= Check(IsChoice(parts[item][choice], choice == expected[item]),
                        "selected option color matches its configured choice");
        }
    }
    return ok;
}

bool RunInactiveFormGuard() {
    CMenuOption option;
    std::array<std::array<MENUFORMPARTS_TYPE, OPTION_BUTTON_NUM>, OPTION_ITEM_MAX> parts{};
    for (int item = 0; item < OPTION_ITEM_MAX; ++item) {
        for (int choice = 0; choice < OPTION_BUTTON_NUM; ++choice) {
            option.button[item][choice] = &parts[item][choice];
            std::memset(parts[item][choice].rgba, 0xA5, sizeof(parts[item][choice].rgba));
        }
    }
    option.config = {};
    LanguageCode = LANG_ENGLISH;
    PortSetMenuOptionFormForTest(0);
    option.UpdateOptionForm();
    for (const auto &row : parts) {
        for (const auto &part : row) {
            if (part.rgba[0] != 0xA5 || part.rgba[1] != 0xA5 || part.rgba[2] != 0xA5) {
                return Check(false, "inactive option form leaves every button unchanged");
            }
        }
    }
    return true;
}

} // namespace

int main() {
    bool ok = true;
    for (int map_choice = 0; map_choice <= 2; ++map_choice) {
        ok &= Run(map_choice, 0, LANG_ENGLISH);
    }
    ok &= Run(1, 1, LANG_ENGLISH); // Enemy HP hides the anger-counter selection.
    ok &= Run(2, 0, LANG_JAPANESE); // The two camera options are absent in Japanese.
    ok &= RunInactiveFormGuard();
    PortSetMenuOptionFormForTest(0);
    if (ok) std::puts("PASS: menu option choices 0, 1, 2 and configured option rows");
    return ok ? 0 : 1;
}
