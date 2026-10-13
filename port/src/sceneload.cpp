#include "common.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "editmap.hpp"
#include "map.hpp"
#include "mapsky.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "scene.hpp"
#include "sceneload.hpp"
#include "scenesnd.hpp"

#include "platform/clock.hpp"

namespace {

constexpr double kHblanksPerField = 262.0;

bool TraceMapLoads() {
    return std::getenv("DC2_TRACE_TEXTURES") != nullptr;
}

// Only differences of the count are used, so the anchor is arbitrary. The count is truncated to
// 32 bits like the hardware register, so a difference wraps the same way.
u_int HostTimer0Count() {
    static const std::chrono::steady_clock::time_point anchor = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - anchor;
    return static_cast<u_int>(static_cast<std::uint64_t>(elapsed.count() * ClockTickRate() * kHblanksPerField));
}

} // namespace

PORT_OVERRIDE int CScene::LoadMapFromMemory(int map_no, int step, SCN_LOADMAP_INFO2 *info) {
    mgCMemory *stack = info->stack;
    int        tex_block = info->tex_block;
    const bool trace = TraceMapLoads();

    if (trace) {
        std::fprintf(stderr, "scene-load: map=%d step=%d enter\n", map_no, step);
    }

    if (step == SCN_LOADMAP_STEP_CREATE) {
        CEditMap *edit_map;

        if (info->data_ready == 0) {
            return -1;
        }

        u8 *buf = info->load_buf;

        if (stack == NULL || buf == NULL) {
            return -1;
        }

        edit_map = new (stack->Alloc(0x111)) CEditMap;

        if (edit_map == NULL) {
            return -1;
        }

        edit_map->mds_list_set = &mds_list_set;
        return GetSceneMap(AssignMap(map_no, edit_map, info->name)) != NULL ? 1 : -1;
    }

    if (step == SCN_LOADMAP_STEP_MAP_INFO) {
        SCN_LOADMAP_INFO2::MapFiles *files = &info->files[0];
        SCN_LOADMAP_INFO2::MapFiles *add_files = NULL;
        CMap                        *map = GetMap(map_no);

        if (info->files[1].enable != 0) {
            add_files = &info->files[1];
        }

        map->map_info.LoadMapInfo(files->map_data, files->map_size, stack);

        if (add_files != NULL) {
            map->map_info.AddMapInfo(add_files->map_data, add_files->map_size, stack);
        }

        if (trace) {
            std::fprintf(stderr, "scene-load: map info complete add=%d\n", add_files != NULL);
        }

        return SCN_LOADMAP_STEP_DATA;
    }

    if (step == SCN_LOADMAP_STEP_DATA) {
        SCN_LOADMAP_INFO2::MapFiles *files = &info->files[0];
        SCN_LOADMAP_INFO2::MapFiles *add_files = NULL;
        CMap                        *map = GetMap(map_no);
        int                          add_block_num = 0;
        int                          block;

        if (info->files[1].enable != 0) {
            add_files = &info->files[1];
        }

        block = tex_block;

        if (add_files != NULL) {
            map->LoadData(add_files->mpk_data, add_files->ipk_data, &block, stack);
            add_block_num += block;
            tex_block += block;
        }

        block = tex_block;
        map->LoadData(files->mpk_data, files->ipk_data, &block, stack);
        add_block_num += block;
        info->tex_block_num = add_block_num;
        if (trace) {
            std::fprintf(stderr, "scene-load: data complete blocks=%d\n", add_block_num);
        }
        return SCN_LOADMAP_STEP_EFFECT;
    }

    if (step == SCN_LOADMAP_STEP_EFFECT) {
        SCN_LOADMAP_INFO2::MapFiles *files = &info->files[0];
        CMap                        *map = GetMap(map_no);
        unsigned int                *efp_data = files->efp_data;

        if (efp_data != NULL) {
            if (trace) {
                std::fprintf(stderr, "scene-load: effects begin\n");
            }
            map->CreateEffect(efp_data, info->efp_tex_block, stack);
            if (trace) {
                std::fprintf(stderr, "scene-load: effects complete\n");
            }
        }

        strcpy(mgTexManager.name_suffix, "1");

        if (map->map_info.sky_info != 0 && files->sky_data != NULL && info->sky_tex_block > 0) {
            DeleteSky(0);

            CMapSky *sky = (CMapSky *) operator new(sizeof(CMapSky), stack->Alloc(0x13));

            if (sky != NULL) {
                sky->Initialize();
                sky->LoadPack(files->sky_data, info->sky_tex_block, stack);
                AssignSky(0, sky, NULL);
            }
            if (trace) {
                std::fprintf(stderr, "scene-load: sky complete\n");
            }
        }

        mgTexManager.name_suffix[0] = 0;
        return SCN_LOADMAP_STEP_CREATE_MAP;
    }

    if (step == SCN_LOADMAP_STEP_CREATE_MAP) {
        CMap *map;
        u_int start_count = HostTimer0Count();

        map = GetMap(map_no);

        if (info->place_parts_max > 0) {
            map->SetPlacePartsBuff(stack, info->place_parts_max);
        }

        if (trace) {
            std::fprintf(stderr, "scene-load: create map begin parts=%d\n", info->place_parts_max);
        }
        map->CreateMap(&mds_list_set, stack);
        if (trace) {
            std::fprintf(stderr, "scene-load: create map complete\n");
        }
        printf("5:%d\n", HostTimer0Count() - start_count);
        return SCN_LOADMAP_STEP_FUNC_POINT;
    }

    if (step == SCN_LOADMAP_STEP_FUNC_POINT) {
        if (trace) {
            std::fprintf(stderr, "scene-load: function points begin\n");
        }
        GetMap(map_no)->AssignFuncPoint(stack);
        if (trace) {
            std::fprintf(stderr, "scene-load: function points complete\n");
        }
        return SCN_LOADMAP_STEP_CFG;
    }

    if (step == SCN_LOADMAP_STEP_CFG) {
        SCN_LOADMAP_INFO2::MapFiles *files = &info->files[0];
        CMap                        *map = GetMap(map_no);
        CSceneMap                   *slot = GetSceneMap(map_no);

        if (files->cfg_size > 0) {
            if (trace) {
                std::fprintf(stderr, "scene-load: cfg begin size=%d\n", files->cfg_size);
            }
            map->LoadCfgFile(files->cfg_data, files->cfg_size, stack);
            if (trace) {
                std::fprintf(stderr, "scene-load: cfg complete\n");
            }
        }

        int num = info->tex_block_num;
        slot->tex_block = info->tex_block;
        slot->tex_block_num = num;
        slot->stack = stack;
        slot->status |= SCENE_DATA_LOADED;
        if (trace) {
            std::fprintf(stderr, "scene-load: place-parts finalization begin\n");
        }
        map->PlacePartsEnd();
        if (trace) {
            std::fprintf(stderr, "scene-load: place-parts finalization complete\n");
        }
        printf("remain %dkByte\n", stack->stGetRest() * 16 / 1024);
        return SCN_LOADMAP_STEP_CFG;
    }

    return -1;
}
