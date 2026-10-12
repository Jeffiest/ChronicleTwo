#include "common.h"

#include "dng_hud.hpp"
#include "dng_main.hpp"
#include "dng_status.hpp"
#include "scenesnd.hpp"
#include "userdata.hpp"

#include "gfx/gfx.hpp"

PORT_OVERRIDE void DrawStatusBord() {
    gfx::UiAnchorScope anchor(gfx::UiAnchor::Side(-1, -1, true));

    float rate;
    int   chara;

    chara = DngUserData->active_chr_no;
    rate = BattleAreaScene->statusbar_rate;
    WarningGage2.warning[0] = false;
    WarningGage2.warning[1] = false;
    WarningGage2.warning[2] = false;
    LockOnModel.pos[3] = 0.0f;

    switch (chara) {
        case USER_CHARA_MAX:
        case USER_CHARA_MONICA:
            DrawMainUnitStatusBord(rate);
            break;
        case USER_CHARA_ROBO:
            DrawRoboUnitStatusBord(rate);
            break;
        case USER_CHARA_MONSTER:
            DrawMonsterUnitStatusBord(rate);
            break;
    }
}
