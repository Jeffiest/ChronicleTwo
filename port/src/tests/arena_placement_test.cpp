#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "actionchara.hpp"
#include "automap.hpp"
#include "character.hpp"
#include "collision.hpp"
#include "dng_event.hpp"
#include "dngmenu.hpp"
#include "dynamicanime.hpp"
#include "editeff.hpp"
#include "editexception.hpp"
#include "editinfo.hpp"
#include "editmap.hpp"
#include "editmenu.hpp"
#include "editparts.hpp"
#include "editriver.hpp"
#include "effscript.hpp"
#include "event_func.hpp"
#include "mapload.hpp"
#include "mapparts.hpp"
#include "mapselect.hpp"
#include "mapsky.hpp"
#include "mdslist.hpp"
#include "memcard.hpp"
#include "menucapt.hpp"
#include "menuchr.hpp"
#include "menucls1.hpp"
#include "menudraw.hpp"
#include "menushop.hpp"
#include "mg_camera.hpp"
#include "mg_drawenv.hpp"
#include "mg_drawprim.hpp"
#include "mg_frame.hpp"
#include "mg_memory.hpp"
#include "mg_sprite.hpp"
#include "mg_tanime.hpp"
#include "mg_texture.hpp"
#include "mg_visual.hpp"
#include "movie.hpp"
#include "movieviewlp.hpp"
#include "nameregi.hpp"
#include "outline.hpp"
#include "quest.hpp"
#include "runscript.hpp"
#include "swordeffect.hpp"
#include "title.hpp"
#include "villagermngr.hpp"
#include "wavetable.hpp"

namespace {

struct Site {
    const char *where;
    std::size_t host_bytes;
    int quadwords;
};

const Site kSites[] = {
    {"automap.cpp:821 AUTOMAP_ROOM_INFO[AUTOMAP_ROOM_INFO_MAX]", sizeof(AUTOMAP_ROOM_INFO) * AUTOMAP_ROOM_INFO_MAX, 0x62},
    {"character.cpp:1958 COutLineDraw[1]", sizeof(COutLineDraw) * 1, 9},
    {"character.cpp:3714 COutLineDraw[1]", sizeof(COutLineDraw) * 1, 9},
    {"character.cpp:3792 CSWordAfterEffect[1]", sizeof(CSWordAfterEffect) * 1, 12},
    {"dngmenu.cpp:3947 CMenuTreeMap[1]", sizeof(CMenuTreeMap) * 1, 0x2FC0},
    {"dngmenu.cpp:3949 CDngFreeMap[1]", sizeof(CDngFreeMap) * 1, 0x13},
    {"dng_event.cpp:1704 TRESURE_BOX_FLOOR_INFO[1]", sizeof(TRESURE_BOX_FLOOR_INFO) * 1, 0x1A43},
    {"dynamicanime.cpp:1217 CDAColPipe[1]", sizeof(CDAColPipe) * 1, 16},
    {"editeff.cpp:82 CPaintEffect[1]", sizeof(CPaintEffect) * 1, 0x42},
    {"editeff.cpp:554 CMapParts[1]", sizeof(CMapParts) * 1, 0x33},
    {"editexception.cpp:390 mgC3DSprite[1]", sizeof(mgC3DSprite) * 1, 7},
    {"editexception.cpp:391 FirePowder[FIRE_POWDER_NUM]", sizeof(FirePowder) * FIRE_POWDER_NUM, 0x202},
    {"editexception.cpp:392 mgCFrame[1]", sizeof(mgCFrame) * 1, 0x13},
    {"editexception.cpp:393 mgCFrameAttr[1]", sizeof(mgCFrameAttr) * 1, 0xB},
    {"editexception.cpp:691 mgCFrame[1]", sizeof(mgCFrame) * 1, 0x13},
    {"editexception.cpp:692 mgCFrameAttr[1]", sizeof(mgCFrameAttr) * 1, 0xB},
    {"editexception.cpp:696 CGeyserEffect[4]", sizeof(CGeyserEffect) * 4, 0x22},
    {"editexception.cpp:700 CGeyserEffectPoint[0x30]", sizeof(CGeyserEffectPoint) * 0x30, 0x92},
    {"editmap.cpp:2091 CMapPiece[1]", sizeof(CMapPiece) * 1, 0xD},
    {"editmap.cpp:2124 CMapPiece[1]", sizeof(CMapPiece) * 1, 0xD},
    {"editmap.cpp:2151 CMapPiece[1]", sizeof(CMapPiece) * 1, 0xD},
    {"editmap.cpp:2518 CEditPartsInfo[EDIT_MAP_RIVER_PARTS_MAX]", sizeof(CEditPartsInfo) * EDIT_MAP_RIVER_PARTS_MAX, 0x142},
    {"editmenu.cpp:1162 CMenuGeorama[1]", sizeof(CMenuGeorama) * 1, 0x1B92},
    {"editmenu.cpp:1222 CDC2Mes[1]", sizeof(CDC2Mes) * 1, 0x2A7},
    {"editmenu.cpp:2099 CDC2Mes[1]", sizeof(CDC2Mes) * 1, 0x2A7},
    {"editmenu.cpp:2407 CEditInfoMngr[1]", sizeof(CEditInfoMngr) * 1, 4},
    {"editmenu.cpp:2477 CMenuFont[1]", sizeof(CMenuFont) * 1, 0xE},
    {"editmenu.cpp:2528 CMenuFont[1]", sizeof(CMenuFont) * 1, 0xE},
    {"editmenu.cpp:5763 CRemovalMenu[1]", sizeof(CRemovalMenu) * 1, 0x152},
    {"editmode.cpp:676 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"editmode.cpp:695 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"editmode.cpp:710 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"editriver.cpp:53 CEditGrid[1]", sizeof(CEditGrid) * 1, 0x15},
    {"effscript.cpp:546 EFF_SPT_BASE[1]", sizeof(EFF_SPT_BASE) * 1, 4},
    {"effscript.cpp:560 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"effscript.cpp:1294 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"event_func.cpp:2016 ARG_LIST[1]", sizeof(ARG_LIST) * 1, 3},
    {"event_func.cpp:6080 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"event_func.cpp:17132 CSWordAfterEffect[1]", sizeof(CSWordAfterEffect) * 1, 12},
    {"event_func.cpp:18927 mgCMemory[1]", sizeof(mgCMemory) * 1, 5},
    {"fishing.cpp:1197 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"fishing.cpp:1420 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"fishing.cpp:1422 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"fishing.cpp:1424 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"fishing.cpp:1426 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"fishing.cpp:1428 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"fishing.cpp:1430 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"fishing.cpp:1432 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"funcpoint.cpp:495 CList<CFuncPoint>[1]", sizeof(CList<CFuncPoint>) * 1, 0x20},
    {"maintex.cpp:283 CSWordAfterEffect[1]", sizeof(CSWordAfterEffect) * 1, 12},
    {"mapselect.cpp:441 char *[SELECT_MAP_MAX]", sizeof(char *) * SELECT_MAP_MAX, 0x22},
    {"mapselect.cpp:998 EVENT_VIEW_INFO[EVENT_VIEW_MAX]", sizeof(EVENT_VIEW_INFO) * EVENT_VIEW_MAX, 0x382},
    {"mdslist.cpp:245 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"mdslist.cpp:803 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"menuchr.cpp:1030 mgCTexture[1]", sizeof(mgCTexture) * 1, 9},
    {"menuchr.cpp:1189 CActionChara[1]", sizeof(CActionChara) * 1, 0x105},
    {"menuchr.cpp:6620 CActionChara[1]", sizeof(CActionChara) * 1, 0x105},
    {"menuchr.cpp:7242 CMenuCostumeSel[1]", sizeof(CMenuCostumeSel) * 1, 0x2F},
    {"menuchr.cpp:7828 CActionChara[1]", sizeof(CActionChara) * 1, 0x105},
    {"menuchr.cpp:7876 CMosBookMenu[1]", sizeof(CMosBookMenu) * 1, 0x9A},
    {"menudraw.cpp:5809 mgCTexture[1]", sizeof(mgCTexture) * 1, 9},
    {"menudraw.cpp:5810 mgCTexture[1]", sizeof(mgCTexture) * 1, 9},
    {"menudraw.cpp:5811 mgCTexture[1]", sizeof(mgCTexture) * 1, 9},
    {"menudraw.cpp:6359 CActionChara[1]", sizeof(CActionChara) * 1, 0x105},
    {"menudraw.cpp:6402 CRepairEffect[1]", sizeof(CRepairEffect) * 1, 5},
    {"menuop.cpp:3128 CDC2Mes[1]", sizeof(CDC2Mes) * 1, 0x2A7},
    {"menushop.cpp:1868 CShopMenu[1]", sizeof(CShopMenu) * 1, 0x23},
    {"menushop.cpp:1874 CShop[1]", sizeof(CShop) * 1, 0x123},
    {"menushop.cpp:1888 CMenuMoveItem[1]", sizeof(CMenuMoveItem) * 1, 0x13},
    {"menushop.cpp:2336 CMenuQuestView[1]", sizeof(CMenuQuestView) * 1, 0x1B},
    {"menushop.cpp:2341 CQuestManager[1]", sizeof(CQuestManager) * 1, 3},
    {"menusys.cpp:6954 CActionChara[1]", sizeof(CActionChara) * 1, 0x105},
    {"menusys.cpp:6960 CMenuMoveItem[1]", sizeof(CMenuMoveItem) * 1, 0x13},
    {"menusys.cpp:6964 CActionChara[1]", sizeof(CActionChara) * 1, 0x105},
    {"menusys.cpp:6966 CMenuEffect[1]", sizeof(CMenuEffect) * 1, 6},
    {"menusys.cpp:6972 CMenuEffect[1]", sizeof(CMenuEffect) * 1, 6},
    {"menusys.cpp:6978 CRepairManager[1]", sizeof(CRepairManager) * 1, 0x21},
    {"menusys.cpp:11797 CDC2Mes[1]", sizeof(CDC2Mes) * 1, 0x2A7},
    {"mg_dataset.cpp:867 mgCFrame::BoundInfo[1]", sizeof(mgCFrame::BoundInfo) * 1, 0xD},
    {"mg_dataset.cpp:1034 mgCFrame::BoundInfo[1]", sizeof(mgCFrame::BoundInfo) * 1, 0xD},
    {"mg_shadow.cpp:162 mgCFace[1]", sizeof(mgCFace) * 1, 5},
    {"mg_tanime.cpp:653 CList<mgCTexAnimeData>[1]", sizeof(CList<mgCTexAnimeData>) * 1, 6},
    {"mg_tanime.cpp:1081 mgCTextureAnime[1]", sizeof(mgCTextureAnime) * 1, 0x21},
    {"mg_visual.cpp:504 mgFACE_GROUP[1]", sizeof(mgFACE_GROUP) * 1, 4},
    {"mg_visual.cpp:522 mgFACE_GROUP[1]", sizeof(mgFACE_GROUP) * 1, 4},
    {"monster.cpp:1109 CSWordAfterEffect[1]", sizeof(CSWordAfterEffect) * 1, 0xC},
    {"movieviewlp.cpp:203 CMovie[1]", sizeof(CMovie) * 1, 0x2396},
    {"movieviewlp.cpp:205 MOVIE_LIST_ENTRY[64]", sizeof(MOVIE_LIST_ENTRY) * 64, 0x32},
    {"nameregi.cpp:1067 CNameRegiMenu[1]", sizeof(CNameRegiMenu) * 1, 0xBF},
    {"sceneload.cpp:187 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"sceneload.cpp:242 CCharacter2[1]", sizeof(CCharacter2) * 1, 0x68},
    {"sceneload.cpp:325 CEditMap[1]", sizeof(CEditMap) * 1, 0x111},
    {"sceneload.cpp:401 CMapSky[1]", sizeof(CMapSky) * 1, 0x13},
    {"scenevillager.cpp:851 CVillagerPlaceInfo[1]", sizeof(CVillagerPlaceInfo) * 1, 6},
    {"title.cpp:661 TITLE_INFO[1]", sizeof(TITLE_INFO) * 1, 0x1C},
    {"title.cpp:686 CMemoryCardManager[1]", sizeof(CMemoryCardManager) * 1, 0x112},
    {"title.cpp:688 mgCCameraFollow[1]", sizeof(mgCCameraFollow) * 1, 0xE},
    {"title.cpp:689 mgCCamera[1]", sizeof(mgCCamera) * 1, 9},
    {"title.cpp:690 CWaveTable[1]", sizeof(CWaveTable) * 1, 0x123},
    {"title.cpp:2644 CDC2Mes[1]", sizeof(CDC2Mes) * 1, 0x2A7},
    {"title.cpp:2645 CDC2Mes[1]", sizeof(CDC2Mes) * 1, 0x2A7},
    {"villagermngr.cpp:46 CVillagerPlaceInfo::Node[1]", sizeof(CVillagerPlaceInfo::Node) * 1, 4},
};

// Casted Alloc runs carry no type, so the count is what the caller hands the run (load capacity,
// loop bound, fixed write length) rather than a placement count.
const Site kCastSites[] = {
    {"actscript.cpp:2612 RS_STACKDATA x 0x80", sizeof(RS_STACKDATA) * 0x80, 0x40},
    {"actscript.cpp:2613 RS_CALLDATA x 0x200", sizeof(RS_CALLDATA) * 0x200, 0x180},
    {"dngmenu.cpp:2579 DNGMAP_KOMA_POS x 1", sizeof(DNGMAP_KOMA_POS) * 1, 1},
    {"dngmenu.cpp:2609 DNGMAP_KOMA_POS x 1", sizeof(DNGMAP_KOMA_POS) * 1, 1},
    {"dngmenu.cpp:2617 DNGMAP_KOMA_POS x 1", sizeof(DNGMAP_KOMA_POS) * 1, 1},
    {"dngmenu.cpp:2642 DNGMAP_KOMA_POS x 1", sizeof(DNGMAP_KOMA_POS) * 1, 1},
    {"dngmenu.cpp:2650 DNGMAP_KOMA_POS x 1", sizeof(DNGMAP_KOMA_POS) * 1, 1},
    {"dngmenu.cpp:2664 DNGMAP_KOMA_POS x 1", sizeof(DNGMAP_KOMA_POS) * 1, 1},
    {"dngmenu.cpp:2672 DNGMAP_KOMA_POS x 1", sizeof(DNGMAP_KOMA_POS) * 1, 1},
    {"dynamicanime.cpp:1005 int x 4", sizeof(int) * 4, 1},
    {"effscript.cpp:5563 RS_STACKDATA x 0x40", sizeof(RS_STACKDATA) * 0x40, 0x20},
    {"event.cpp:112 RS_STACKDATA x 0x80", sizeof(RS_STACKDATA) * 0x80, 0x40},
    {"event.cpp:113 RS_CALLDATA x 0x200", sizeof(RS_CALLDATA) * 0x200, 0x180},
    {"mapload.cpp:780 float x 4", sizeof(float) * 4, 1},
    {"menucapt.cpp:110 MENU_CHAPTER_INFO x 1", sizeof(MENU_CHAPTER_INFO) * 1, 2},
    {"menuchr.cpp:776 MENU_BGREAD_INFO2 x 1", sizeof(MENU_BGREAD_INFO2) * 1, 8},
    {"menuchr.cpp:4135 MENU_BGREAD_INFO2 x 1", sizeof(MENU_BGREAD_INFO2) * 1, 8},
    {"menucommon.cpp:2033 MENU_FORM_ACTION_MOVE x 1", sizeof(MENU_FORM_ACTION_MOVE) * 1, 2},
    {"menudraw.cpp:1515 float x 0x9C * 0x20", sizeof(float) * (0x9C * 0x20), 0x4E0},
    {"menudraw.cpp:5776 char x 5", sizeof(char) * 5, 1}, // "icon" plus terminator
    {"mg_drawprim.cpp:751 mgSORT_PACKET x 1", sizeof(mgSORT_PACKET) * 1, 1},
    {"mg_shadow.cpp:186 mgFACE_GROUP x 1", sizeof(mgFACE_GROUP) * 1, 0x12},
    {"mg_visual.cpp:472 mgCFace x 1", sizeof(mgCFace) * 1, 3},
    {"nameregi.cpp:761 NAMEREGI_KANJI_NODE x 1", sizeof(NAMEREGI_KANJI_NODE) * 1, 1},
    {"nameregi.cpp:764 NAMEREGI_KANJI_NODE x 1", sizeof(NAMEREGI_KANJI_NODE) * 1, 1},
    {"runscript_opcodes.cpp:4678 RS_STACKDATA x 0x80", sizeof(RS_STACKDATA) * 0x80, 0x40},
    {"runscript_opcodes.cpp:4679 RS_CALLDATA x 0x200", sizeof(RS_CALLDATA) * 0x200, 0x180},
    {"scenevillager.cpp:1094 CCPoly x 0x80", sizeof(CCPoly) * 0x80, 0x280},
};

} // namespace

TEST(CtArenaPlacement, RetailRunsAgainstHostSizes) {
    std::vector<std::string> outgrowing;
    for (const Site &site : kSites) {
        const std::size_t run_bytes = static_cast<std::size_t>(site.quadwords) * 16;
        if (site.host_bytes > run_bytes) {
            std::printf("outgrows: %s host %zu run %zu\n", site.where, site.host_bytes, run_bytes);
            outgrowing.emplace_back(site.where);
        }
    }

    // The placement operator grows these runs while they are the arena's last (mg_memory.cpp).
    const std::vector<std::string> kOutgrowing = {
    "automap.cpp:821 AUTOMAP_ROOM_INFO[AUTOMAP_ROOM_INFO_MAX]",
    "character.cpp:3792 CSWordAfterEffect[1]",
    "dngmenu.cpp:3947 CMenuTreeMap[1]",
    "dngmenu.cpp:3949 CDngFreeMap[1]",
    "editeff.cpp:82 CPaintEffect[1]",
    "editeff.cpp:554 CMapParts[1]",
    "editexception.cpp:390 mgC3DSprite[1]",
    "editexception.cpp:696 CGeyserEffect[4]",
    "editmenu.cpp:1162 CMenuGeorama[1]",
    "editmenu.cpp:5763 CRemovalMenu[1]",
    "editmode.cpp:676 CCharacter2[1]",
    "editmode.cpp:695 CCharacter2[1]",
    "editmode.cpp:710 CCharacter2[1]",
    "effscript.cpp:560 CCharacter2[1]",
    "effscript.cpp:1294 CCharacter2[1]",
    "event_func.cpp:6080 CCharacter2[1]",
    "event_func.cpp:17132 CSWordAfterEffect[1]",
    "fishing.cpp:1197 CCharacter2[1]",
    "fishing.cpp:1420 CCharacter2[1]",
    "fishing.cpp:1422 CCharacter2[1]",
    "fishing.cpp:1424 CCharacter2[1]",
    "fishing.cpp:1426 CCharacter2[1]",
    "fishing.cpp:1428 CCharacter2[1]",
    "fishing.cpp:1430 CCharacter2[1]",
    "fishing.cpp:1432 CCharacter2[1]",
    "funcpoint.cpp:495 CList<CFuncPoint>[1]",
    "maintex.cpp:283 CSWordAfterEffect[1]",
    "mapselect.cpp:441 char *[SELECT_MAP_MAX]",
    "mapselect.cpp:998 EVENT_VIEW_INFO[EVENT_VIEW_MAX]",
    "mdslist.cpp:245 CCharacter2[1]",
    "mdslist.cpp:803 CCharacter2[1]",
    "menuchr.cpp:1189 CActionChara[1]",
    "menuchr.cpp:6620 CActionChara[1]",
    "menuchr.cpp:7242 CMenuCostumeSel[1]",
    "menuchr.cpp:7828 CActionChara[1]",
    "menuchr.cpp:7876 CMosBookMenu[1]",
    "menudraw.cpp:6359 CActionChara[1]",
    "menushop.cpp:1868 CShopMenu[1]",
    "menusys.cpp:6954 CActionChara[1]",
    "menusys.cpp:6964 CActionChara[1]",
    "menusys.cpp:6978 CRepairManager[1]",
    "mg_tanime.cpp:1081 mgCTextureAnime[1]",
    "monster.cpp:1109 CSWordAfterEffect[1]",
    "movieviewlp.cpp:203 CMovie[1]",
    "movieviewlp.cpp:205 MOVIE_LIST_ENTRY[64]",
    "sceneload.cpp:187 CCharacter2[1]",
    "sceneload.cpp:242 CCharacter2[1]",
    "sceneload.cpp:325 CEditMap[1]",
    "sceneload.cpp:401 CMapSky[1]",
    "title.cpp:661 TITLE_INFO[1]",
    "title.cpp:686 CMemoryCardManager[1]",
    };
    EXPECT_EQ(outgrowing, kOutgrowing);
}

TEST(CtArenaPlacement, CastRunsAgainstHostSizes) {
    std::vector<std::string> outgrowing;
    for (const Site &site : kCastSites) {
        const std::size_t run_bytes = static_cast<std::size_t>(site.quadwords) * 16;
        if (site.host_bytes > run_bytes) {
            std::printf("outgrows: %s host %zu run %zu\n", site.where, site.host_bytes, run_bytes);
            outgrowing.emplace_back(site.where);
        }
    }

    // Cast, not placed, so nothing grows them: each overruns its run on the host until ps2/src sizes it with sizeof.
    const std::vector<std::string> kCastOutgrowing = {
    "actscript.cpp:2612 RS_STACKDATA x 0x80",
    "actscript.cpp:2613 RS_CALLDATA x 0x200",
    "effscript.cpp:5563 RS_STACKDATA x 0x40",
    "event.cpp:112 RS_STACKDATA x 0x80",
    "event.cpp:113 RS_CALLDATA x 0x200",
    "mg_drawprim.cpp:751 mgSORT_PACKET x 1",
    "runscript_opcodes.cpp:4678 RS_STACKDATA x 0x80",
    "runscript_opcodes.cpp:4679 RS_CALLDATA x 0x200",
    };
    EXPECT_EQ(outgrowing, kCastOutgrowing);
}
