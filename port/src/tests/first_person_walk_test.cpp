#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <numbers>

#include "camera_control_port.hpp"
#include "dng_main.hpp"
#include "editctrl_port.hpp"
#include "editloop.hpp"
#include "first_person_walk.hpp"
#include "mainloop.hpp"
#include "mainloop_port.hpp"
#include "map.hpp"
#include "mglib_port.hpp"
#include "photo.hpp"
#include "platform/input_script.hpp"
#include "stubs/photo.hpp"
#include "userdata.hpp"

extern sceVu0FVECTOR PortTownSavedEye asm("EditCtrl_SavedEye");
extern sceVu0FVECTOR PortDungeonSavedEye asm("DngMain_SavedEye");
extern int           PortTownEyeInitialized asm("EditCtrl_EyeInitialized");
extern int           PortDungeonEyeInitialized asm("DngMain_EyeInitialized");

namespace {
struct RestoreFirstPersonState {
    std::function<void()> restore;

    RestoreFirstPersonState() {
        auto &scene = *GetMainScene();
        auto  slots = [camera = *scene.GetSceneCamera(0), chara = *scene.GetSceneCharacter(0),
                       active = scene.active_camera, player = scene.player_chara, event = scene.event_run,
                       dng = DngMainScene, area = BattleAreaScene, main = MainChara__2, user = DngUserData,
                       status = DngStatus, pad = GamePad__2, photo = TakePhotoMode, debug = DebugInfo.debug_camera] {
            auto &scene = *GetMainScene();
            *scene.GetSceneCamera(0) = camera;
            *scene.GetSceneCharacter(0) = chara;
            scene.active_camera = active;
            scene.player_chara = player;
            scene.event_run = event;
            DngMainScene = dng;
            BattleAreaScene = area;
            MainChara__2 = main;
            DngUserData = user;
            DngStatus = status;
            GamePad__2 = pad;
            TakePhotoMode = photo;
            DebugInfo.debug_camera = debug;
        };
        std::vector<std::pair<int *, int> > flags;
        for (auto *flag : {&PortLoopNo, &PortTownLoopMode, &PortTownControlMode, &PortTownLockChara,
                           &PortTownPause, &PortEditLadderMode, &PortDungeonDebugPause,
                           &PortTownEyeInitialized, &PortDungeonEyeInitialized}) {
            flags.emplace_back(flag, *flag);
        }
        restore = [slots, flags, view = PortEditViewMode] {
            slots();
            for (auto [flag, value] : flags) {
                *flag = value;
            }
            PortEditViewMode = view;
            InputScriptInstall({});
        };
    }

    ~RestoreFirstPersonState() { restore(); }
};
} // namespace

TEST(FirstPersonWalk, AcceptedDisplacementRestoresThirdPersonEyeAtNewPlayerPosition) {
    RestoreFirstPersonState restore;
    const float             before[] = {10, 20, 30, 1}, after[] = {310, 5, -70, 1};
    for (bool dungeon : {false, true}) {
        CScene         scene;
        CCameraControl camera;
        CActionChara   chara;
        scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
        scene.AssignChara(0, &chara, const_cast<char *>("player"));
        scene.active_camera = scene.player_chara = 0;
        float      *saved = dungeon ? PortDungeonSavedEye : PortTownSavedEye;
        const float entry[] = {10, 60, 130, 1};
        std::copy_n(entry, 4, saved);
        FirstPersonShiftSavedEye(saved, before, after);
        camera.SetPos(after[0], after[1], after[2]);
        camera.FollowOff();
        const auto camera_generation = mgPortCameraGeneration();
        if (dungeon) {
            DngMainScene = &scene;
            BattleAreaScene = &scene.battle_area;
            MainChara__2 = &chara;
            DngStatus.eye_view = 1;
            PortDungeonEyeInitialized = 1;
            TakePhotoMode = TAKE_PHOTO_OFF;
            ResetEyeView(&chara);
            EXPECT_GT(mgPortCameraGeneration(), camera_generation);
        } else {
            PortTownEyeInitialized = 1;
            ResetViewMode(&scene);
        }
        for (int i = 0; i < 3; ++i) {
            EXPECT_FLOAT_EQ(camera.pos[i], entry[i] + after[i] - before[i]);
        }
        EXPECT_FLOAT_EQ(saved[3], 1);
    }
}

TEST(FirstPersonWalk, AirborneContinuationKeepsFreshMouseLookAndLocomotion) {
    RestoreFirstPersonState restore;

    class EmptyMap : public CMap {
    public:
        int GetColPoly(CCPoly *, mgVu0FBOX &, int) override { return 0; }
    } map;

    CScene           scene;
    CCameraControl   camera;
    CActionChara     chara;
    CUserDataManager user;
    scene.AssignMap(0, &map, const_cast<char *>("map"));
    scene.active_map = 0;
    scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
    scene.active_camera = scene.event_run = 0;
    scene.battle_area.script.event_no = -1;
    scene.battle_area.camera_mode = DNG_CAMERA_FOLLOW;
    scene.battle_area.pause_flag = 0;
    scene.battle_area.script.running = 0;
    scene.battle_area.lock_on_mode = -1;
    DngMainScene = &scene;
    BattleAreaScene = &scene.battle_area;
    MainChara__2 = &chara;
    DngUserData = &user;
    user.active_chr_no = USER_CHARA_MAX;
    PortLoopNo = LOOP_DUNGEON;
    DngStatus = {};
    DngStatus.eye_view = 1;
    PortDungeonDebugPause = GamePad__2.key_lock = DebugInfo.debug_camera = 0;
    TakePhotoMode = TAKE_PHOTO_OFF;
    chara.SetPosition(0, 100, 0);
    chara.velocity[0] = 1;
    chara.velocity[1] = -1;
    chara.menu_flag = 1;
    chara.hold_type = ACTION_HOLD_NONE;
    chara.lock_on = 0;
    chara.damage_req = ACTION_DAMAGE_REQ_NONE;
    camera.FollowOff();
    camera.ControlOff();
    camera.SetPos(0, 128, 0);
    camera.SetRef(0, 128, 10);
    const auto  old_battle = BattleParamater;
    const float old_yaw = viewAngleH__2, old_pitch = viewAngleV__2;
    InputResetBindings();
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 key:w mouse:10,10\n", script, error));
    InputScriptInstall(script);
    viewAngleH__2 = viewAngleV__2 = 0;
    for (int i = 0; i < 3; ++i) {
        InputLatchPad(0);
        const float x = chara.position[0], yaw = viewAngleH__2, pitch = viewAngleV__2;
        camera.Step(1);
        EXPECT_EQ(chara.move_check.landed, 0);
        EXPECT_EQ(chara.menu_flag, 0);
        EXPECT_GT(chara.position[0], x);
        EXPECT_LT(viewAngleH__2, yaw);
        EXPECT_GT(viewAngleV__2, pitch);
        const float moved = chara.position[0], turned = viewAngleH__2;
        camera.Step(1);
        EXPECT_FLOAT_EQ(chara.position[0], moved);
        EXPECT_FLOAT_EQ(viewAngleH__2, turned);
    }
    chara.menu_flag = 1;
    chara.move_check.landed = 1;
    EXPECT_TRUE(DungeonFirstPersonOwns(&camera));
    for (auto *gate : {&scene.event_run, &GamePad__2.key_lock, &PortDungeonDebugPause}) {
        *gate = 1;
        EXPECT_FALSE(DungeonFirstPersonOwns(&camera));
        *gate = 0;
    }
    chara.lock_on = 1;
    EXPECT_FALSE(DungeonFirstPersonOwns(&camera));
    BattleParamater = old_battle;
    viewAngleH__2 = old_yaw;
    viewAngleV__2 = old_pitch;
}

TEST(FirstPersonWalk, ViewRelativeForwardBackAndStrafePreserveMagnitude) {
    for (float heading : {0.0f, 0.7f, -1.3f, std::numbers::pi_v<float>}) {
        for (float angle : {0.0f, 1.0f, -2.0f}) {
            for (auto key : {
                     InputKeyboardMovement{0,  -1},
                     {0,  1 },
                     {1,  0 },
                     {-1, 0 },
                     {1,  -1}
            }) {
                const auto  stick = FirstPersonWalkStick(key, heading, angle);
                const float x = stick.x * std::cos(angle) + stick.y * std::sin(angle);
                const float z = -stick.x * std::sin(angle) + stick.y * std::cos(angle);
                const float length = std::hypot(key.x, key.y);
                EXPECT_NEAR(x, (-key.x * std::cos(heading) - key.y * std::sin(heading)) / length, 0.00001f);
                EXPECT_NEAR(z, (key.x * std::sin(heading) - key.y * std::cos(heading)) / length, 0.00001f);
                EXPECT_NEAR(std::hypot(x, z), 1, 0.00001f);
            }
        }
    }
    const auto invalid = FirstPersonWalkStick({0, -1}, std::numeric_limits<float>::infinity(), 0);
    EXPECT_EQ(invalid.x, 0);
    EXPECT_EQ(invalid.y, 0);
}

TEST(FirstPersonWalk, TownGatesSpecialOwnersAndKeepsPadLook) {
    RestoreFirstPersonState restore;
    auto                   &scene = *GetMainScene();
    CCharacter2             chara;
    CCameraControl          camera;
    camera.ControlOff();
    camera.FollowOff();
    scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
    scene.AssignChara(0, &chara, const_cast<char *>("player"));
    scene.player_chara = scene.active_camera = scene.event_run = 0;
    PortLoopNo = LOOP_EDIT;
    PortTownLoopMode = EDIT_LOOP_WALK;
    PortTownControlMode = EDIT_CONTROL_PLAYER;
    PortTownLockChara = PortTownPause = PortEditLadderMode = 0;
    PortEditViewMode = EDIT_VIEW_MODE_EYE;
    GamePad__2.key_lock = DebugInfo.debug_camera = 0;
    ASSERT_TRUE(TownFirstPersonOwns(&scene, &PadCtrl));
    for (int gate = 0; gate < 9; ++gate) {
        if (gate == 0) {
            PortEditViewMode = EDIT_VIEW_MODE_PHOTO;
        }
        if (gate == 1) {
            PortTownPause = 1;
        }
        if (gate == 2) {
            PortTownLockChara = 1;
        }
        if (gate == 3) {
            scene.event_run = 1;
        }
        if (gate == 4) {
            GamePad__2.key_lock = 1;
        }
        if (gate == 5) {
            PortTownControlMode = EDIT_CONTROL_EVENT;
        }
        if (gate == 6) {
            PortTownLoopMode = EDIT_LOOP_WALK_MENU;
        }
        if (gate == 7) {
            PortEditLadderMode = EDIT_LADDER_MODE_BOTTOM;
        }
        if (gate == 8) {
            camera.FollowOn();
        }
        EXPECT_FALSE(TownFirstPersonOwns(&scene, &PadCtrl)) << gate;
        PortEditViewMode = EDIT_VIEW_MODE_EYE;
        PortTownPause = PortTownLockChara = scene.event_run = GamePad__2.key_lock = PortEditLadderMode = 0;
        PortTownControlMode = EDIT_CONTROL_PLAYER;
        PortTownLoopMode = EDIT_LOOP_WALK;
        camera.FollowOff();
    }
    InputResetBindings();
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 key:w key:d 220 128 128 128\n", script, error));
    InputScriptInstall(script);
    auto state = InputGetPad(0);
    GamePad__2.pad[0].left_x = state.left_x;
    GamePad__2.pad[0].left_y = state.left_y;
    EXPECT_FLOAT_EQ(GamePad__2.GetLYf(), 0);
    EXPECT_GT(GamePad__2.GetLXf(), 0.5f);
    PortEditViewMode = EDIT_VIEW_MODE_PHOTO;
    EXPECT_LT(GamePad__2.GetLYf(), -0.5f);
    GamePad__2.pad[0].left_x = GamePad__2.pad[0].left_y = 128;
    PortEditViewMode = EDIT_VIEW_MODE_EYE;
    InputScriptInstall({});
    InputPadState centered{.connected = true};
    InputSetOverride(0, &centered);
    GamePad__2.pad[0].button = PAD_RIGHT | PAD_UP;
    EXPECT_FLOAT_EQ(GamePad__2.GetLXf(), 1);
    EXPECT_FLOAT_EQ(GamePad__2.GetLYf(), -1);
    GamePad__2.pad[0].button = PAD_LEFT | PAD_DOWN;
    EXPECT_FLOAT_EQ(GamePad__2.GetLXf(), -1);
    EXPECT_FLOAT_EQ(GamePad__2.GetLYf(), 1);
    InputSetOverride(0, nullptr);
}

TEST(FirstPersonWalk, DungeonGatesPhotoRidepodPauseEventsAndActions) {
    RestoreFirstPersonState restore;
    CScene                  scene;
    CCameraControl          camera;
    CActionChara            chara;
    CUserDataManager        user;
    DngMainScene = &scene;
    BattleAreaScene = &scene.battle_area;
    MainChara__2 = &chara;
    DngUserData = &user;
    PortLoopNo = LOOP_DUNGEON;
    DngStatus = {};
    DngStatus.eye_view = 1;
    scene.active_camera = scene.event_run = 0;
    scene.AssignCamera(0, &camera, const_cast<char *>("MainCam"));
    camera.FollowOff();
    scene.battle_area.camera_mode = DNG_CAMERA_FOLLOW;
    scene.battle_area.pause_flag = 0;
    scene.battle_area.script.event_no = -1;
    scene.battle_area.script.running = 0;
    user.active_chr_no = USER_CHARA_MAX;
    chara.menu_flag = 1;
    chara.hold_type = ACTION_HOLD_NONE;
    chara.lock_on = 0;
    chara.damage_req = ACTION_DAMAGE_REQ_NONE;
    PortDungeonDebugPause = GamePad__2.key_lock = DebugInfo.debug_camera = 0;
    TakePhotoMode = TAKE_PHOTO_OFF;
    ASSERT_TRUE(DungeonFirstPersonOwns(&camera));
    chara.move_check.landed = 1;

    class TouchMap : public CMap {
    public:
        bool       touch = false;
        CFuncPoint point{};

        CFuncPoint *GetEvent(float *, int kind, MapEventInfo *info) override {
            *info = {};
            return touch && kind == MAP_EVENT_CHECK_TOUCH ? &point : nullptr;
        }
    } map;

    scene.AssignMap(0, &map, const_cast<char *>("map"));
    scene.map[0].status |= SCENE_DATA_ACTIVE;
    RandomCircle.Clear();
    GeoStone.flag = 0;
    EXPECT_FALSE(DungeonFirstPersonCheckEvents(camera));
    for (int event = 0; event < 3; ++event) {
        DngStatus.eye_view = 1;
        camera.FollowOff();
        PortDungeonEyeInitialized = 0;
        if (event == 0) {
            RandomCircle.SetCircle(chara.position);
        }
        if (event == 1) {
            map.touch = true;
        }
        if (event == 2) {
            GeoStone.flag = 1;
            GeoStone.SetPosition(chara.position[0], chara.position[1] + 20, chara.position[2]);
        }
        EXPECT_TRUE(DungeonFirstPersonCheckEvents(camera)) << event;
        EXPECT_EQ(DngStatus.eye_view, 0);
        EXPECT_TRUE(camera.follow_on);
        RandomCircle.Clear();
        map.touch = false;
        GeoStone.flag = 0;
    }
    DngStatus.eye_view = 1;
    camera.FollowOff();
    for (int gate = 0; gate < 8; ++gate) {
        if (gate == 0) {
            TakePhotoMode = TAKE_PHOTO_AIM;
        }
        if (gate == 1) {
            user.active_chr_no = USER_CHARA_ROBO;
        }
        if (gate == 2) {
            scene.battle_area.pause_flag = DNG_PAUSE_PLAYER_CONTROL;
        }
        if (gate == 3) {
            scene.battle_area.script.running = 1;
        }
        if (gate == 4) {
            PortDungeonDebugPause = 1;
        }
        if (gate == 5) {
            chara.menu_flag = 0;
        }
        if (gate == 6) {
            DngStatus.mode = DNG_STATUS_MENU;
        }
        if (gate == 7) {
            chara.damage_req = ACTION_DAMAGE_REQ_DEAD;
        }
        EXPECT_FALSE(DungeonFirstPersonOwns(&camera)) << gate;
        TakePhotoMode = TAKE_PHOTO_OFF;
        user.active_chr_no = USER_CHARA_MAX;
        scene.battle_area.pause_flag = scene.battle_area.script.running = PortDungeonDebugPause = 0;
        chara.menu_flag = 1;
        DngStatus.mode = DNG_STATUS_FIELD;
        chara.damage_req = ACTION_DAMAGE_REQ_NONE;
    }
    DngMainScene = nullptr;
    BattleAreaScene = nullptr;
    MainChara__2 = nullptr;
    DngUserData = nullptr;
}
