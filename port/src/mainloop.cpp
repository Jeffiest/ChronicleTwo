#include "common.h"

#include <libgraph.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "arena_root.hpp"
#include "dataread.hpp"
#include "debug_mode.hpp"
#include "editdata.hpp"
#include "font.hpp"
#include "gaiji.hpp"
#include "gamedata.hpp"
#include "gamepad.hpp"
#include "helpmes.hpp"
#include "inventmn.hpp"
#include "main.hpp"
#include "mainloop.hpp"
#include "mainloop3.hpp"
#include "mapselect.hpp"
#include "mg_drawprim.hpp"
#include "mg_memory.hpp"
#include "mg_memory_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "monster.hpp"
#include "nowload.hpp"
#include "npccfg.hpp"
#include "padcontrol.hpp"
#include "platform/arena.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "savedata.hpp"
#include "scene.hpp"
#include "scenesnd.hpp"
#include "snd_mngr.hpp"
#include "sysmes.hpp"
#include "userdata.hpp"
#include "vlgr_info.hpp"

extern int           PortNextLoopNo asm("MainLoop_NextLoopNo");
extern int           PortLoopNo asm("MainLoop_LoopNo");
extern int           PortPrevLoopNo asm("MainLoop_PrevLoopNo");
extern int           PortCaptureMode asm("MainLoop_CaptureMode");
extern int           PortCaptureScreen asm("MainLoop_CaptureScreen");
extern int           PortPlayTimeCountFlag asm("MainLoop_PlayTimeCountFlag");
extern CSaveData    *PortActiveSaveData asm("MainLoop_ActiveSaveData");
extern CSubGameData *PortSubGameSaveData asm("MainLoop_SubGameSaveData");
extern u_long128     PortSystemSeBuff[400] asm("MainLoop_SystemSeBuff");
extern u_long128     PortInfoBuff[5000] asm("MainLoop_InfoBuff");
void MainLoop_InitPadTable(int language);
void MainLoop_VSyncCallBack(int unused);
extern u_long128     main_buffer[0x1A0000];
extern CScene        MainScene;
extern CFont         Font;

static constexpr int kSubGameQuads = std::max(5000, static_cast<int>((sizeof(CSubGameData) + 15) / 16));

PORT_OVERRIDE void MainLoop() {
    static u_long128 *vu_prog[16];

    mgCMemory        *memory;
    mgCMemory        *read_memory;
    CUserDataManager *user_data;
    u_long128        *file_buffer;
    int               file_size;
    int               mode_finished;
    std::uintptr_t    buffer_address;
    std::uintptr_t    alignment;
    u_long128        *main_arena = ArenaRoot(&main_buffer, 0x1A0000, "MainBuffer");

    memory = GetMainStack();
    memory->stSetBuffer(main_arena, 0x1A0000 * kArenaHeadroom);
    memory->stack_used = 0;
    memory->lock = 0;
    SaveData.Initialize();
    PortActiveSaveData = &SaveData;
    PortSubGameSaveData = NULL;
    DebugFlag = false;
    GamePad__2.Init();
    InitFileCache(NULL, 0);
    mgInit(MG_SCREEN_MODE_512X480, 3);
    PortPlayTimeCountFlag = false;
    mgInitVSyncCallBack((int (*)(int)) MainLoop_VSyncCallBack);
    LanguageCode = LANG_FRENCH;
    SCElogoFade(0, memory);
    PortLoopNo = LOOP_TITLE;
    DebugFlag = false;
    DebugInfo.chara_move = 0;

    static int pmeter_flag = false;

    pmeter_flag = false;
    GamePad__2.WaitEnable();
    sceGsSyncV(0);
    GamePad__2.UpDate();
    MasterDebugCode = 0;
    DebugFlag = false;

    if (GamePad__2.On2(PAD_R1) && GamePad__2.On2(PAD_R2) && GamePad__2.On2(PAD_L1) && GamePad__2.On2(PAD_L2)) {
        MasterDebugCode = MASTER_DEBUG_CODE;
    }

    GamePad__2.DebugKeyLock(1);
    sceGsSyncV(0);
    GamePad__2.UpDate();
    sceGsSyncV(0);
    GamePad__2.UpDate();
    sceGsSyncV(0);
    GamePad__2.UpDate();
    mgSetUserVuProg(vu_prog, 16);
    mgSetUserVuProgAdr(0, Vu_prog_wtr);
    MainLoop_InitPadTable(LanguageCode);
    GameItemDataManage.LoadData();
    GameItemDataManage.LoadItemSystemMes(LanguageCode);
    LoadSystemMes();
    CreateSystemMes();
    LoadFontTexture();
    LoadGaijiImg();
    LoadFontTblBin();
    LoadFontTex2Img();
    LoadNPCCfg();
    InfoStack.stSetBuffer(ArenaRoot(PortInfoBuff, 5000, "InfoStack"), 5000 * kArenaHeadroom);
    LoadGameInfo(&InfoStack);
    InitPauseData();
    InitSaveData();
    user_data = NULL;

    if (PortActiveSaveData != NULL) {
        user_data = &PortActiveSaveData->user_data;
    }

    LoadMonsterLanguage(LanguageCode);
    MainLoop_InitPadTable(LanguageCode);
    LanguageEquipChange();
    DebugGetItem(user_data, 0);
    PortCaptureMode = CAPTURE_OFF;
    sndInitMngr();
    MainScene.InitSnd();
    SetCurrentDir(NULL);
    SystemSeStack.stSetBuffer(ArenaRoot(PortSystemSeBuff, 400, "SystemSeStack"), 400 * kArenaHeadroom);
    SystemSeStack.stack_used = 0;
    SystemSeStack.lock = 0;
    read_memory = GetMainStack();
    buffer_address = reinterpret_cast<std::uintptr_t>(read_memory->stack + read_memory->stack_used);
    alignment = buffer_address % 64;

    if (alignment != 0) {
        buffer_address += ((64 - alignment) / 16) * 16;
    }

    file_buffer = reinterpret_cast<u_long128 *>(buffer_address);
    SystemSND_ID = -1;

    char sy_path[] = "snd2/SY_000.snd";
    char rev_path[] = "snd2/rev.bin";
    char map_index_path[] = "snd2/MAP_index.txt";

    if (LoadFile2(sy_path, file_buffer, NULL, 0)) {
        SystemSND_ID = sndLoadSound(SND_PORT_SYSTEM, (u_int *) file_buffer, &SystemSeStack);
    }

    if (LoadFile2(rev_path, file_buffer, &file_size, 0)) {
        MainScene.LoadSndRevInfo((char *) file_buffer, file_size);
    }

    if (LoadFile2(map_index_path, file_buffer, &file_size, 0)) {
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
    PortApplyDebugMode(ConfigGet().debug_mode);
    SetIoErrCallBack(EmergencyMessage);
    SCElogoFade(1, memory);

    while (1) {
        if (PortLoopNo == LOOP_TITLE || OmakeFlag != 0) {
            PortSubGameSaveData = (CSubGameData *) main_arena;
            memory->stSetBuffer(&main_arena[kSubGameQuads],0x1A0000 * kArenaHeadroom - kSubGameQuads);
            memory->stack_used = 0;
            memory->lock = 0;
        }

        printf("main_data remain = %dkb\n", ((memory->stack_size - memory->stack_used) * 16) / 1024);

        if (PortLoopNo < 0 || PortLoopNo >= LOOP_MODE_NUM) {
            break;
        }

        if (DebugFlag == 0 && PortLoopNo == LOOP_MENU) {
            PortLoopNo = LOOP_TITLE;
        }

        if (PortCaptureMode == CAPTURE_RECORD) {
            GamePad__2.CaptureStart();
            srand(9999);
        }

        if (PortCaptureMode == CAPTURE_PLAY || PortCaptureMode == CAPTURE_PLAY_SCREEN) {
            if (PortCaptureMode != CAPTURE_OFF) {
                GamePad__2.LoadCapture();
            }

            GamePad__2.CapturePlay();
            srand(9999);
        }

        mgFrameRate = 2;
        PortCaptureScreen = false;
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

        if (PortLoopNo == LOOP_TITLE) {
            sndSeAllStop(-1);
            sndDeletePort(SND_PORT_BGM);
            MainScene.InitSnd();
        }

        printf("##### %d\n", mgGetVSyncCount());
        // Single entry for every mode change: covers the LOOP_MENU->LOOP_TITLE redirect above and the PortNextLoopNo hand-off below.
        mgPortCutInterpolation();
        LoopInit[PortLoopNo](InitArg);
        INIT_LOOP_ARG next_arg;

        NextLoop(LOOP_MENU, next_arg);

        while (1) {
            if (GamePad__2.On(PAD_CIRCLE)) {
                cat_start();
            }

            if (PortLoopNo != LOOP_MENU) {
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

            if (GamePad__2.Down2(PAD_CIRCLE) || InputHostPressed(InputHostAction::DebugMenu)) {
                PortToggleDebugMode();
            }

            mgBeginFrame(NULL);
            mode_finished = LoopMain[PortLoopNo]();
            sndStep(mgGetNowFrameRate());
            MainScene.StepSnd();
            GamePad__2.UpDate();
            PadCtrl.Update(&GamePad__2);
            // Retail's screen-edge outline sat on the native 4:3 boundary. In Fill mode that
            // boundary is inside the window, so omit the obsolete outline when the viewport expands.
            const mgPortExtent visible_extent = mgPortVisibleExtent();
            if (visible_extent.left >= 0.0f && visible_extent.top >= 0.0f &&
                visible_extent.right <= static_cast<float>(mgScreenWidth) &&
                visible_extent.bottom <= static_cast<float>(mgScreenHeight)) {
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
            }
            mgEndFrame(NULL);

            if (mode_finished != 0) {
                PauseCancel();
                break;
            }

            if (GamePad__2.On(PAD_CIRCLE)) {
                cat_end();
            }

            switch (PortLoopNo) {
                case LOOP_EDIT:
                case LOOP_DUNGEON:
                case LOOP_TITLE: {
                    static int pause = false;

                    if (GamePad__2.Down2(PAD_SELECT)) {
                        pause = true;
                    }

                    while (pause != 0) {
                        sceGsSyncV(0);
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

        LoopExit[PortLoopNo]();
        MemoryLogPeaks();

        if (PortLoopNo != LOOP_MENU) {
            if (PortCaptureMode != CAPTURE_OFF) {
                GamePad__2.SaveCapture();
            }

            PortCaptureMode = CAPTURE_OFF;
        }

        PortPrevLoopNo = PortLoopNo;
        PrevInitArg = InitArg;
        PortLoopNo = PortNextLoopNo;
        InitArg = NextInitArg;
        GamePad__2.CaptureEnd();
        sceGsSyncV(0);
        GamePad__2.UpDate();
        sceGsSyncV(0);
        GamePad__2.UpDate();

        while (sceGsSyncV(0) != 0) {
        }
    }

    sndSeAllStop(-1);
    sndStopVoice(0);
    sndStopVoice(1);
    sceGsSyncV(0);
    sndStep(2.0f);
    sceGsSyncV(0);
    sndStep(2.0f);
    GamePad__2.Close();
}
