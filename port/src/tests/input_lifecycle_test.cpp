#include <cassert>
#include <cmath>
#include <cstdint>

#include <libpad.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include "gamepad.hpp"
#include "padcontrol.hpp"
#include "mainloop.hpp"
#include "input_lifecycle.hpp"
#include "platform/gamepad_map.hpp"
#include "platform/input.hpp"

extern CGamePad GamePad__2;

namespace {

unsigned char g_dma_buffer[2][1024];

void OpenPads() {
    assert(scePadInit(0) == 1);
    assert(scePadPortOpen(0, 0, g_dma_buffer[0]) == 1);
    assert(scePadPortOpen(1, 0, g_dma_buffer[1]) == 1);
}

void SetPad(std::uint16_t buttons, int lx = 128, int ly = 128) {
    InputPadState state;
    state.connected = true;
    state.buttons = buttons;
    state.left_x = static_cast<std::uint8_t>(lx);
    state.left_y = static_cast<std::uint8_t>(ly);
    InputSetOverride(0, &state);
}

void SetXboxPad(SDL_GamepadButton button, bool right_trigger = false) {
    InputPadState state;
    state.connected = true;
    state.buttons = GamepadButtonPad(SDL_GAMEPAD_TYPE_XBOXONE, button);
    if (right_trigger) state.buttons |= kInputR2;
    InputSetOverride(0, &state);
}

void Unplug(int pad) {
    InputPadState state;
    InputSetOverride(pad, &state);
}

} // namespace

int main() {
    // This is the same pure mapping used by host input polling: Xbox Y is DualShock Triangle.
    assert(GamepadButtonPad(SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_BUTTON_NORTH) == PAD_TRIANGLE);

    OpenPads();
    SetPad(0);
    Unplug(1);
    for (int frame = 0; frame < 4; ++frame) {
        GamePad__2.UpDate();
    }

    CPadControl controls{};
    controls.Initialize();
    // The retail language table binds confirm/cancel to Circle/Cross or Cross/Circle; Triangle
    // remains the menu button and is deliberately not used as a confirm binding here.
    assert(controls.RegisterBtn(PAD_BTN_CONFIRM, PAD_CIRCLE, PAD_CTRL_TRIGGER_DOWN) == 1);
    assert(controls.RegisterBtn(PAD_BTN_CANCEL, PAD_CROSS, PAD_CTRL_TRIGGER_DOWN) == 1);
    assert(controls.RegisterAnalog(PAD_ANALOG_LEFT_X, PAD_CTRL_AXIS_LX) == 1);

    InputPadState keyboard_base;
    keyboard_base.connected = true;
    InputKeyboardMouse keyboard;
    keyboard.keys.push_back(SDL_SCANCODE_ESCAPE);
    assert(InputApplyKeyboardMouse(keyboard_base, keyboard).buttons == PAD_TRIANGLE);

    SetPad(PAD_TRIANGLE, 255);
    GamePad__2.MenuModeOn(120);
    assert(InputApplyKeyboardMouse(keyboard_base, keyboard).buttons == PAD_CIRCLE);
    GamePad__2.UpDate();
    controls.Update(&GamePad__2);
    assert(GamePad__2.On(PAD_TRIANGLE));
    assert(GamePad__2.Down(PAD_TRIANGLE));
    assert(!GamePad__2.Down(PAD_CIRCLE));
    assert(!GamePad__2.Up(PAD_TRIANGLE));
    assert(GamePad__2.On(PAD_RIGHT));
    assert(controls.Btn(PAD_BTN_CONFIRM) == 0);
    assert(controls.Btn(PAD_BTN_CANCEL) == 0);
    assert(std::fabs(controls.Analog(PAD_ANALOG_LEFT_X) - 1.0f) < 0.00001f);

    GamePad__2.UpDate();
    controls.Update(&GamePad__2);
    assert(GamePad__2.On(PAD_TRIANGLE));
    assert(!GamePad__2.Down(PAD_TRIANGLE));

    // Left-stick threshold uses a strict comparison: 250 calibrates to 119, 251 to 121.
    SetPad(0, 250);
    GamePad__2.UpDate();
    assert(!GamePad__2.On(PAD_RIGHT));
    SetPad(0, 251);
    GamePad__2.UpDate();
    assert(GamePad__2.On(PAD_RIGHT));

    // A mode transition removes the stick-generated direction while exposing Circle's press and
    // its CPadControl down edge as the language-table confirm binding.
    SetPad(PAD_CIRCLE, 255);
    GamePad__2.MenuModeOff();
    assert(InputApplyKeyboardMouse(keyboard_base, keyboard).buttons == PAD_TRIANGLE);
    GamePad__2.UpDate();
    controls.Update(&GamePad__2);
    assert(GamePad__2.Down(PAD_CIRCLE));
    assert(GamePad__2.Up(PAD_RIGHT));
    assert(!GamePad__2.On(PAD_RIGHT));
    assert(controls.Btn(PAD_BTN_CONFIRM) == 1);
    assert(controls.Btn(PAD_BTN_CANCEL) == 0);

    // Disconnect clears held state; a reconnect walks the controller setup phases before input
    // is accepted again, so the held Circle becomes a fresh down edge only once ready.
    Unplug(0);
    GamePad__2.UpDate();
    controls.Update(&GamePad__2);
    assert(!GamePad__2.On(PAD_CIRCLE));
    assert(GamePad__2.Up(PAD_CIRCLE));
    assert(controls.Btn(PAD_BTN_CONFIRM) == 0);

    SetPad(PAD_CIRCLE);
    for (int frame = 0; frame < 4; ++frame) {
        GamePad__2.UpDate();
    }
    controls.Update(&GamePad__2);
    assert(GamePad__2.Down(PAD_CIRCLE));
    assert(controls.Btn(PAD_BTN_CONFIRM) == 1);

    // Exercise the actual game's language table, including the PAL English controls.
    for (int language : {LANG_ENGLISH, LANG_JAPANESE}) {
        PortInitializePadBindings(language);
        SetPad(0);
        GamePad__2.UpDate();
        SetPad(language == LANG_ENGLISH ? PAD_CROSS : PAD_CIRCLE);
        GamePad__2.UpDate();
        PadCtrl.Update(&GamePad__2);
        assert(PadCtrl.Btn(PAD_BTN_CONFIRM) == 1 && PadCtrl.Btn(PAD_BTN_CANCEL) == 0);
        GamePad__2.UpDate();
        PadCtrl.Update(&GamePad__2);
        assert(PadCtrl.Btn(PAD_BTN_CONFIRM) == 0);
        SetPad(language == LANG_ENGLISH ? PAD_CIRCLE : PAD_CROSS);
        GamePad__2.UpDate();
        PadCtrl.Update(&GamePad__2);
        assert(PadCtrl.Btn(PAD_BTN_CANCEL) == 1 && PadCtrl.Btn(PAD_BTN_CONFIRM) == 0);
    }

    // Verify Xbox face buttons through host mapping, pad latching and PAL English bindings.
    // The right-trigger bit is the value ReadGamepad publishes for SDL's RT axis.
    PortInitializePadBindings(LANG_ENGLISH);
    SetPad(0);
    GamePad__2.UpDate();
    PadCtrl.Update(&GamePad__2);
    SetXboxPad(SDL_GAMEPAD_BUTTON_SOUTH);
    GamePad__2.UpDate();
    PadCtrl.Update(&GamePad__2);
    assert(PadCtrl.Btn(PAD_BTN_ACTION_CONFIRM) == 1);
    assert(PadCtrl.Btn(PAD_BTN_ACTION_HELD) == 1);

    GamePad__2.UpDate();
    PadCtrl.Update(&GamePad__2);
    assert(PadCtrl.Btn(PAD_BTN_ACTION_CONFIRM) == 0);
    assert(PadCtrl.Btn(PAD_BTN_ACTION_HELD) == 1);
    SetPad(0);
    GamePad__2.UpDate();
    PadCtrl.Update(&GamePad__2);
    assert(PadCtrl.Btn(PAD_BTN_ACTION_HELD) == 0);

    SetXboxPad(SDL_GAMEPAD_BUTTON_EAST);
    GamePad__2.UpDate();
    PadCtrl.Update(&GamePad__2);
    assert(PadCtrl.Btn(PAD_BTN_ACTION_CANCEL) == 1);

    SetXboxPad(SDL_GAMEPAD_BUTTON_INVALID, true);
    GamePad__2.UpDate();
    PadCtrl.Update(&GamePad__2);
    assert(PadCtrl.Btn(PAD_BTN_R2) == 1);
    SetPad(0);
    GamePad__2.UpDate();
    PadCtrl.Update(&GamePad__2);
    assert(PadCtrl.Btn(PAD_BTN_R2) == 0);
    return 0;
}
