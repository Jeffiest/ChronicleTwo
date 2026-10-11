#include "mainloop.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "charaviewlp.hpp"
#include "convviewlp.hpp"
#include "dataread.hpp"
#include "dng_main.hpp"
#include "editdata.hpp"
#include "editloop.hpp"
#include "font.hpp"
#include "gaiji.hpp"
#include "gamedata.hpp"
#include "gamepad.hpp"
#include "helpmes.hpp"
#include "inventmn.hpp"
#include "main.hpp"
#include "mainloop3.hpp"
#include "mapselect.hpp"
#include "mapviewlp.hpp"
#include "menuchr.hpp"
#include "mg_drawprim.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "monster.hpp"
#include "movieviewlp.hpp"
#include "nd_meswin.hpp"
#include "nowload.hpp"
#include "npccfg.hpp"
#include "padcontrol.hpp"
#include "platform/clock.hpp"
#include "savedata.hpp"
#include "scene.hpp"
#include "scenesnd.hpp"
#include "scriptinterpreter.hpp"
#include "snd_mngr.hpp"
#include "sndviewlp.hpp"
#include "subgame.hpp"
#include "sysmes.hpp"
#include "texviewlp.hpp"
#include "title.hpp"
#include "userdata.hpp"
#include "visualmotion.hpp"
#include "vlgr_info.hpp"
#include "water.hpp"

// The main loop and the state it owns, as the host can run it: every wait retail made on the PS2's
// VSync interrupt goes through the platform clock (platform/clock.hpp) instead. The functions the
// loop's file statics reach through (NextLoop and the accessors below) are the port's copies of
// ps2/src/mainloop.cpp's, so they read and write the same values the loop does.

extern CScene MainScene;
extern CFont  Font;

/**
 * Resource arena the loop hands the game through GetMainStack.
 */
u_long128 main_buffer[0x1A0000];

namespace {

/**
 * Mode entered after the current main loop finishes.
 */
int NextLoopNo;
/**
 * Mode active before the current main loop.
 */
int PrevLoopNo;
/**
 * Selected input capture or replay mode.
 */
int CaptureMode;
/**
 * Currently active main-loop mode.
 */
int LoopNo;
/**
 * Whether vertical blank advances the saved play time.
 */
int PlayTimeCountFlag;
/**
 * Save data used by the current game mode.
 */
CSaveData *ActiveSaveData;
/**
 * Save-data arena for the active extra game.
 */
CSubGameData *SubGameSaveData;

/**
 * Memory backing the system sound-effect loader.
 */
u_long128 SystemSeBuff[400];
/**
 * Memory backing configuration and villager data.
 */
u_long128 InfoBuff[5000];

/**
 * Language-adjusted mappings from logical controls to pad buttons.
 */
PAD_TABLE_ENTRY pad_table[] = {
    {PAD_BTN_CONFIRM,            PAD_CTRL_TRIGGER_DOWN, PAD_CIRCLE  },
    {PAD_BTN_CANCEL,             PAD_CTRL_TRIGGER_DOWN, PAD_CROSS   },
    {PAD_BTN_R1_HELD,            PAD_CTRL_TRIGGER_ON,   PAD_R1      },
    {PAD_BTN_L1_HELD,            PAD_CTRL_TRIGGER_ON,   PAD_L1      },
    {PAD_BTN_L2,                 PAD_CTRL_TRIGGER_DOWN, PAD_L2      },
    {PAD_BTN_MENU,               PAD_CTRL_TRIGGER_DOWN, PAD_TRIANGLE},
    {PAD_BTN_R2,                 PAD_CTRL_TRIGGER_DOWN, PAD_R2      },
    {PAD_BTN_UP,                 PAD_CTRL_TRIGGER_DOWN, PAD_UP      },
    {PAD_BTN_DOWN,               PAD_CTRL_TRIGGER_DOWN, PAD_DOWN    },
    {PAD_BTN_RIGHT,              PAD_CTRL_TRIGGER_DOWN, PAD_RIGHT   },
    {PAD_BTN_LEFT,               PAD_CTRL_TRIGGER_DOWN, PAD_LEFT    },
    {11,                         PAD_CTRL_TRIGGER_ON,   PAD_UP      },
    {12,                         PAD_CTRL_TRIGGER_ON,   PAD_DOWN    },
    {13,                         PAD_CTRL_TRIGGER_ON,   PAD_RIGHT   },
    {14,                         PAD_CTRL_TRIGGER_ON,   PAD_LEFT    },
    {PAD_BTN_START,              PAD_CTRL_TRIGGER_DOWN, PAD_START   },
    {PAD_BTN_CONFIRM_2,          PAD_CTRL_TRIGGER_DOWN, PAD_CIRCLE  },
    {PAD_BTN_CANCEL_2,           PAD_CTRL_TRIGGER_DOWN, PAD_CROSS   },
    {19,                         PAD_CTRL_TRIGGER_ON,   PAD_CIRCLE  },
    {PAD_BTN_EVENT_SKIP_2,       PAD_CTRL_TRIGGER_DOWN, PAD_CIRCLE  },
    {PAD_BTN_PAUSE,              PAD_CTRL_TRIGGER_DOWN, PAD_START   },
    {PAD_BTN_EVENT_SKIP,         PAD_CTRL_TRIGGER_DOWN, PAD_TRIANGLE},
    {PAD_BTN_QUICK_CHANGE,       PAD_CTRL_TRIGGER_DOWN, PAD_L3      },
    {24,                         PAD_CTRL_TRIGGER_DOWN, PAD_R3      },
    {PAD_BTN_ACTION_CONFIRM,     PAD_CTRL_TRIGGER_DOWN, PAD_CIRCLE  },
    {PAD_BTN_ACTION_SQUARE,      PAD_CTRL_TRIGGER_DOWN, PAD_SQUARE  },
    {PAD_BTN_ACTION_CANCEL,      PAD_CTRL_TRIGGER_DOWN, PAD_CROSS   },
    {PAD_BTN_L1_HELD_2,          PAD_CTRL_TRIGGER_ON,   PAD_L1      },
    {PAD_BTN_R1_HELD_2,          PAD_CTRL_TRIGGER_ON,   PAD_R1      },
    {55,                         PAD_CTRL_TRIGGER_DOWN, PAD_SELECT  },
    {PAD_BTN_ACTION_HELD,        PAD_CTRL_TRIGGER_ON,   PAD_CIRCLE  },
    {PAD_BTN_EDIT_TURN_DECREASE, PAD_CTRL_TRIGGER_DOWN, PAD_R2      },
    {PAD_BTN_EDIT_TURN_INCREASE, PAD_CTRL_TRIGGER_DOWN, PAD_L2      },
    {PAD_BTN_EDIT_PLACE,         PAD_CTRL_TRIGGER_DOWN, PAD_CIRCLE  },
    {PAD_BTN_EDIT_REMOVE,        PAD_CTRL_TRIGGER_DOWN, PAD_CIRCLE  },
    {PAD_BTN_EDIT_PAINT,         PAD_CTRL_TRIGGER_DOWN, PAD_CIRCLE  },
    {PAD_BTN_EDIT_PAINT_ALL,     PAD_CTRL_TRIGGER_DOWN, PAD_SQUARE  },
    {PAD_BTN_EDIT_WALL_NEXT,     PAD_CTRL_TRIGGER_DOWN, PAD_R2      },
    {PAD_BTN_EDIT_WALL_PREVIOUS, PAD_CTRL_TRIGGER_DOWN, PAD_L2      },
    {18,                         PAD_CTRL_TRIGGER_DOWN, PAD_R1      },
    {PAD_BTN_EDIT_SWITCH,        PAD_CTRL_TRIGGER_DOWN, PAD_SELECT  },
    {PAD_BTN_EDIT_MAGNET,        PAD_CTRL_TRIGGER_DOWN, PAD_SQUARE  },
    {PAD_BTN_FISH_ACTION,        PAD_CTRL_TRIGGER_DOWN, PAD_CIRCLE  },
    {PAD_BTN_FISH_REEL,          PAD_CTRL_TRIGGER_ON,   PAD_CIRCLE  },
    {PAD_BTN_FISH_REEL_FAST,     PAD_CTRL_TRIGGER_ON,   PAD_SQUARE  },
    {-1,                         -1,                    -1          },
};
/**
 * Mappings from logical analog controls to pad stick axes.
 */
ANALOG_TABLE_ENTRY analog_table[] = {
    {PAD_ANALOG_LEFT_X,    PAD_CTRL_AXIS_LX},
    {PAD_ANALOG_LEFT_Y,    PAD_CTRL_AXIS_LY},
    {PAD_ANALOG_RIGHT_X,   PAD_CTRL_AXIS_RX},
    {PAD_ANALOG_RIGHT_Y,   PAD_CTRL_AXIS_RY},
    {PAD_ANALOG_LEFT_Y_2,  PAD_CTRL_AXIS_LY},
    {PAD_ANALOG_LEFT_X_2,  PAD_CTRL_AXIS_LX},
    {PAD_ANALOG_RIGHT_X_2, PAD_CTRL_AXIS_RX},
    {PAD_ANALOG_RIGHT_Y_2, PAD_CTRL_AXIS_RY},
    {-1,                   -1              },
};

/**
 * Advances saved play time once per tick, the port's PlayTimeCount tick callback (retail's
 * VSyncCallBack, installed with mgInitVSyncCallBack).
 */
int PlayTimeTick(int unused) {
    if (PlayTimeCountFlag != 0) {
        CSaveData *save = GetSaveData();
        s64        ticks = save->play_time;
        save->play_time = ticks + 1;
    }

    return 0;
}

/**
 *
 * Registers the logical controller bindings for the selected language.
 *
 */
void InitPadTable(int language) {
    int confirm[2] = {PAD_CIRCLE, PAD_CROSS};
    int cancel[2] = {PAD_CROSS, PAD_CIRCLE};
    int language_index;
    int i;

    language_index = 0;

    if (language != LANG_JAPANESE) {
        language_index = 1;
    }

    PadCtrl.Initialize();

    for (i = 0; pad_table[i].no >= 0; i++) {
        switch (pad_table[i].no) {
            case PAD_BTN_CONFIRM:
            case PAD_BTN_CONFIRM_2:
            case PAD_BTN_EVENT_SKIP_2:
            case PAD_BTN_ACTION_CONFIRM:
            case PAD_BTN_ACTION_HELD:
            case PAD_BTN_EDIT_PLACE:
            case PAD_BTN_EDIT_REMOVE:
            case PAD_BTN_EDIT_PAINT:
            case PAD_BTN_FISH_ACTION:
            case PAD_BTN_FISH_REEL:
                pad_table[i].button = confirm[language_index];
                break;
            case PAD_BTN_CANCEL:
            case PAD_BTN_CANCEL_2:
            case PAD_BTN_ACTION_CANCEL:
                pad_table[i].button = cancel[language_index];
                break;
        }

        PadCtrl.RegisterBtn(pad_table[i].no, pad_table[i].button, pad_table[i].trigger);
    }

    for (i = 0; analog_table[i].no >= 0; i++) {
        PadCtrl.RegisterAnalog(analog_table[i].no, analog_table[i].axis);
    }
}

} // namespace

int GetCaptureMode() {
    return CaptureMode;
}

CSaveData *GetSaveData() {
    return ActiveSaveData;
}

CSubGameData *GetSubGameSaveData() {
    return SubGameSaveData;
}

void InitSaveData() {
    GetSaveData()->Initialize();
}

void NextLoop(int loop_no, INIT_LOOP_ARG arg) {
    NextLoopNo = loop_no;
    NextInitArg = arg;
}

int GetNowLoopNo() {
    return LoopNo;
}

INIT_LOOP_ARG *GetNowInitArg() {
    return &InitArg;
}

void PlayTimeCount(int enable) {
    PlayTimeCountFlag = enable;
}

int GetPlayTimeCountFlag() {
    return PlayTimeCountFlag;
}

void MainLoop() {
    /**
     *
     * Microprogram addresses loaded when the main game loop starts.
     *
     */
    static u_long128 *vu_prog[16];

    mgCMemory        *memory;
    mgCMemory        *read_memory;
    CUserDataManager *user_data;
    u_long128        *file_buffer;
    int               file_size;
    int               mode_finished;
    uintptr_t         buffer_address;
    int               alignment;

    memory = GetMainStack();
    memory->stSetBuffer(main_buffer, 0x1A0000);
    memory->stack_used = 0;
    memory->lock = 0;
    SaveData.Initialize();
    ActiveSaveData = &SaveData;
    SubGameSaveData = NULL;
    DebugFlag = false;
    GamePad__2.Init();
    InitFileCache(NULL, 0);
    mgInit(MG_SCREEN_MODE_512X480, 3);
    PlayTimeCountFlag = false;
    mgInitVSyncCallBack(PlayTimeTick);
    LanguageCode = LANG_FRENCH;
    SCElogoFade(0, memory);
    LoopNo = LOOP_TITLE;
    DebugFlag = false;
    DebugInfo.chara_move = 0;
    /**
     *
     * Whether the debug performance meter is displayed.
     *
     */
    static int pmeter_flag = false;

    pmeter_flag = false;
    GamePad__2.WaitEnable();
    ClockSyncV();
    GamePad__2.UpDate();
    MasterDebugCode = 0;
    DebugFlag = false;

    if (GamePad__2.On2(PAD_R1) && GamePad__2.On2(PAD_R2) && GamePad__2.On2(PAD_L1) && GamePad__2.On2(PAD_L2)) {
        MasterDebugCode = MASTER_DEBUG_CODE;
    }

    GamePad__2.DebugKeyLock(1);
    ClockSyncV();
    GamePad__2.UpDate();
    ClockSyncV();
    GamePad__2.UpDate();
    ClockSyncV();
    GamePad__2.UpDate();
    mgSetUserVuProg(vu_prog, 16);
    mgSetUserVuProgAdr(0, Vu_prog_wtr);
    InitPadTable(LanguageCode);
    GameItemDataManage.LoadData();
    GameItemDataManage.LoadItemSystemMes(LanguageCode);
    LoadSystemMes();
    CreateSystemMes();
    LoadFontTexture();
    LoadGaijiImg();
    LoadFontTblBin();
    LoadFontTex2Img();
    LoadNPCCfg();
    InfoStack.stSetBuffer(InfoBuff, 5000);
    LoadGameInfo(&InfoStack);
    InitPauseData();
    InitSaveData();
    user_data = NULL;

    if (ActiveSaveData != NULL) {
        user_data = &ActiveSaveData->user_data;
    }

    LoadMonsterLanguage(LanguageCode);
    InitPadTable(LanguageCode);
    LanguageEquipChange();
    DebugGetItem(user_data, 0);
    CaptureMode = CAPTURE_OFF;
    sndInitMngr();
    MainScene.InitSnd();
    SetCurrentDir(NULL);
    SystemSeStack.stSetBuffer(SystemSeBuff, 400);
    SystemSeStack.stack_used = 0;
    SystemSeStack.lock = 0;
    read_memory = GetMainStack();
    // Retail truncated the cursor to 32 bits; the host keeps the whole address, so the arena
    // stays reachable above 4 GiB.
    buffer_address = reinterpret_cast<uintptr_t>(read_memory->stack + read_memory->stack_used);
    alignment = static_cast<int>(buffer_address % 64);

    if (alignment != 0) {
        buffer_address += ((64 - alignment) / 16) * 16;
    }

    file_buffer = reinterpret_cast<u_long128 *>(buffer_address);
    SystemSND_ID = -1;

    if (LoadFile2((char *) "snd2/SY_000.snd", file_buffer, NULL, 0)) {
        SystemSND_ID = sndLoadSound(SND_PORT_SYSTEM, (u_int *) file_buffer, &SystemSeStack);
    }

    if (LoadFile2((char *) "snd2/rev.bin", file_buffer, &file_size, 0)) {
        MainScene.LoadSndRevInfo((char *) file_buffer, file_size);
    }

    if (LoadFile2((char *) "snd2/MAP_index.txt", file_buffer, &file_size, 0)) {
        MainScene.LoadSndFileInfo((char *) file_buffer, file_size);
    }

    LoadHelpMes(file_buffer);
    LoadMapName(LanguageCode, file_buffer);
    LoadEditAnalyzeData(LanguageCode, file_buffer);
    LoadFilePictureName();
    Font.Init();
    Font.Preset(FONT_PRESET_SHADOWED);
    Font.SetFuchi(0);
    Font.SetClearance(15, 18);
    CreateGamePadThread(&GamePad__2);
    LoadGameConfig(NULL);
    SetIoErrCallBack(EmergencyMessage);
    SCElogoFade(1, memory);

    while (1) {
        if (LoopNo == LOOP_TITLE || OmakeFlag != 0) {
            SubGameSaveData = (CSubGameData *) main_buffer;
            memory->stSetBuffer(&main_buffer[5000], 0x1A0000 - 5000);
            memory->stack_used = 0;
            memory->lock = 0;
        }

        printf("main_data remain = %dkb\n", ((memory->stack_size - memory->stack_used) * 16) / 1024);

        if (LoopNo < 0 || LoopNo >= LOOP_MODE_NUM) {
            break;
        }

        if (DebugFlag == 0 && LoopNo == LOOP_MENU) {
            LoopNo = LOOP_TITLE;
        }

        if (CaptureMode == CAPTURE_RECORD) {
            GamePad__2.CaptureStart();
            srand(9999);
        }

        if (CaptureMode == CAPTURE_PLAY || CaptureMode == CAPTURE_PLAY_SCREEN) {
            if (CaptureMode != CAPTURE_OFF) {
                GamePad__2.LoadCapture();
            }

            GamePad__2.CapturePlay();
            srand(9999);
        }

        mgFrameRate = 2;
        GetMainScene()->save_data = GetSaveData();
        float now_time = GetSaveData()->now_time;
        GetMainScene()->SetTime(now_time);
        GetMainScene()->day = GetSaveData()->day;

        if (GetSaveData()->game_progress == 2) {
            CSaveData *save = GetSaveData();
            save->now_time = 22.0f;
            GetMainScene()->SetTime(22.0f);
            GetMainScene()->time_step = 0;
        }

        if (LoopNo == LOOP_TITLE) {
            sndSeAllStop(-1);
            sndDeletePort(SND_PORT_BGM);
            MainScene.InitSnd();
        }

        printf("##### %d\n", mgGetVSyncCount());
        LoopInit[LoopNo](InitArg);
        INIT_LOOP_ARG next_arg;

        NextLoop(LOOP_MENU, next_arg);

        while (1) {
            if (GamePad__2.On(PAD_CIRCLE)) {
                cat_start();
            }

            if (LoopNo != LOOP_MENU) {
                GetMainScene()->save_data = GetSaveData();
                float now_time = GetSaveData()->now_time;
                GetMainScene()->SetTime(now_time);
                GetMainScene()->day = GetSaveData()->day;

                if (GetSaveData()->game_progress == 2) {
                    CSaveData *save = GetSaveData();
                    save->now_time = 22.0f;
                    GetMainScene()->SetTime(22.0f);
                    GetMainScene()->time_step = 0;
                }
            }

            SV_CONFIG_OPTION *config = &GetSaveData()->config;
            GamePad__2.VibrationEnable(!config->vibration);

            if (GamePad__2.Down2(PAD_L1)) {
                pmeter_flag = !pmeter_flag;
            }

            mgPerformanceMeter(pmeter_flag);

            if (GamePad__2.Down2(PAD_CIRCLE)) {
                DebugFlag = !DebugFlag;
            }

            mgBeginFrame(NULL);
            mode_finished = LoopMain[LoopNo]();
            sndStep(mgGetNowFrameRate());
            MainScene.StepSnd();
            GamePad__2.UpDate();
            PadCtrl.Update(&GamePad__2);
            mgCDrawPrim frame_border;
            frame_border.Initialize(NULL, NULL);
            frame_border.DepthTestEnable(false);
            frame_border.AlphaTestEnable(false);
            frame_border.AlphaBlendEnable(false);
            frame_border.TextureMapEnable(false);
            frame_border.ZMask(MG_Z_MASK_MASKED);
            frame_border.Begin(MG_PRIM_LINE_STRIP);
            frame_border.Color(0, 0, 0, 0x80);
            frame_border.Vertex(0, 0, 0);
            frame_border.Vertex(mgScreenWidth - 1, 0, 0);
            frame_border.Vertex(mgScreenWidth - 1, mgScreenHeight - 1, 0);
            frame_border.Vertex(0, mgScreenHeight - 1, 0);
            frame_border.Vertex(0, 0, 0);
            frame_border.End();
            mgEndFrame(NULL);

            if (mode_finished != 0) {
                PauseCancel();
                break;
            }

            if (GamePad__2.On(PAD_CIRCLE)) {
                cat_end();
            }

            switch (LoopNo) {
                case LOOP_EDIT:
                case LOOP_DUNGEON:
                case LOOP_TITLE: {
                    /**
                     *
                     * Whether the debug frame-advance pause is enabled.
                     *
                     */
                    static int pause = false;

                    if (GamePad__2.Down2(PAD_SELECT)) {
                        pause = true;
                    }

                    while (pause != 0) {
                        ClockSyncV();
                        GamePad__2.UpDate();

                        if (GamePad__2.Down(PAD_START) || GamePad__2.Down2(PAD_SELECT)) {
                            pause = false;
                            break;
                        }

                        if (GamePad__2.Down2(PAD_START)) {
                            mgStoreFrameImage();
                        }

                        if (GamePad__2.Down(PAD_RIGHT | PAD_CIRCLE) || GamePad__2.Down2(PAD_RIGHT)) {
                            break;
                        }
                    }

                    break;
                }
            }

            while (PauseLoop() != 0) {
            }

            PauseCount();
        }

        LoopExit[LoopNo]();

        if (LoopNo != LOOP_MENU) {
            if (CaptureMode != CAPTURE_OFF) {
                GamePad__2.SaveCapture();
            }

            CaptureMode = CAPTURE_OFF;
        }

        PrevLoopNo = LoopNo;
        PrevInitArg = InitArg;
        LoopNo = NextLoopNo;
        InitArg = NextInitArg;
        GamePad__2.CaptureEnd();
        ClockSyncV();
        GamePad__2.UpDate();
        ClockSyncV();
        GamePad__2.UpDate();

        while ((ClockSyncV() & 1) != 0) {
        }
    }

    sndSeAllStop(-1);
    sndStopVoice(0);
    sndStopVoice(1);
    ClockSyncV();
    sndStep(2.0f);
    ClockSyncV();
    sndStep(2.0f);
    GamePad__2.Close();
}
