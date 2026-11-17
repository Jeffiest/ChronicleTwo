// Co-op's names over the other players' heroes (moonmod/coop.hpp): each avatar a scene draws carries its player's name
// just over its head, in the game's own font, in towns and on dungeon floors. The place is measured as the scene draws
// the body, with the camera it draws with (AvatarBody::DrawDirect: the point over the head goes through the camera's
// world-to-screen matrix, as the game's damage numbers do, dng_damage.cpp); the name is drawn in the 2D pass after the
// scene, over everything the scene drew.

#include "moonmod/coop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "dng_main.hpp"
#include "editctrl.hpp"
#include "editloop.hpp"
#include "font.hpp"
#include "gfx/gfx.hpp"
#include "mainloop.hpp"
#include "photo.hpp"
#include "platform/game_loop_state.hpp"
#include "object/world/town_control.hpp"
#include "platform/legacy_encoding.hpp"
#include "scenesnd.hpp"
#include "screen_overlay_anchor.hpp"

namespace moonmod::coop {

namespace {

// A hero drawn shorter than this on the screen (in the screen's 448 lines) is too far for a name over it to be read
// or to be told whose it is: the name goes. Between the two heights it fades in, so it never pops.
constexpr float kHiddenBelow = 14.0f;
constexpr float kFullFrom = 30.0f;
// The name's letters: a little smaller than the game's own captions, outlined so they read over a bright wall or a
// dark floor alike.
constexpr int kGlyphHeight = 14;
constexpr int kGlyphWidth = 11;

// The interpolation of each avatar's name between frames (the presentation draws more frames than the game steps):
// a new generation when the camera cuts or the name changes, so the name never slides across a cut.
struct NameTrack {
    ScreenOverlayAnchor anchor;
    std::uint64_t       generation = 0;
    std::string         name;
};
std::array<NameTrack, kAvatars> g_tracks;
std::uint64_t                   g_next_generation = 0;

// Whether the game shows the field now: no event scene, no photo, no menu over a floor, and (in a town) the player
// walking, not building.
bool FieldShown() {
    if (NowTakePhoto() != 0) return false;
    const int loop = GetNowLoopNo();
    if (loop == LOOP_DUNGEON) {
        if (DngMainScene == nullptr || BattleAreaScene == nullptr || DngStatus.mode != DNG_STATUS_FIELD) return false;
        return DngMainScene->event_run == 0 && BattleAreaScene->script.running == 0;
    }
    if (loop == LOOP_EDIT) { // (a scene or a talk takes the town's control: ControlMode is the event's then)
        CScene *scene = GetMainScene();
        return scene != nullptr && scene->event_run == 0 && IsWalkMode() != 0 && IsEditMode() == 0 &&
               PortTownControlMode == EDIT_CONTROL_PLAYER;
    }
    return false;
}

} // namespace

bool PlayerFree() {
    if (NowTakePhoto() != 0) return false;
    const int loop = GetNowLoopNo();
    if (loop == LOOP_DUNGEON) {
        return DngMainScene != nullptr && BattleAreaScene != nullptr && DngStatus.mode == DNG_STATUS_FIELD &&
               DngMainScene->event_run == 0 && BattleAreaScene->script.running == 0;
    }
    if (loop == LOOP_EDIT) return IsEditMode() != 0 ? TownEditorReady() : PortTownFieldControl();
    return false;
}

void DrawAvatarNames() {
    const bool shown = FieldShown();
    for (int slot = 0; slot < kAvatars; ++slot) {
        float       head[2], feet[2];
        std::string name;
        // (asked even when nothing is shown, so a measure from a frame without names is not used later)
        if (!AvatarNameOnScreen(slot, head, feet, name) || !shown) continue;
        const float tall = std::fabs(feet[1] - head[1]);
        if (tall < kHiddenBelow) continue;
        const float opacity = std::clamp((tall - kHiddenBelow) / (kFullFrom - kHiddenBelow), 0.0f, 1.0f);

        std::string text = legacy_encoding::GameText(name);
        if (text.empty()) continue;
        CFont font;
        font.SetDrawSize(kGlyphWidth, kGlyphHeight + 4);
        font.SetClearance(kGlyphWidth, kGlyphHeight + 5);
        font.SetFuchi(FUCHI_OUTLINE);
        font.SetColor(128, 128, 128, static_cast<int>(128.0f * opacity));
        int width = 0, height = 0;
        font.CalcDrawWH(text.data(), &width, &height);
        const int x = static_cast<int>(std::lround(head[0])) - width / 2;
        const int y = static_cast<int>(std::lround(head[1])) - height;

        NameTrack &track = g_tracks[static_cast<std::size_t>(slot)];
        if (track.anchor.Update(x, y) || track.name != name || track.generation == 0) track.generation = ++g_next_generation;
        track.name = name;
        gfx::Interp2DScope interpolation({.key = static_cast<gfx::InterpKey>(reinterpret_cast<std::uintptr_t>(&track)), .generation = track.generation});
        font.DrawDirect(text.data(), x, y);
    }
}

} // namespace moonmod::coop
