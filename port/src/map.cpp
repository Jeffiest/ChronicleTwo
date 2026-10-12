#include "common.h"

#include <cstdlib>
#include <cstdio>

#include "dataread.hpp"
#include "map.hpp"
#include "mglib.hpp"

namespace {

bool TraceMapLoads() {
    return std::getenv("DC2_TRACE_TEXTURES") != nullptr;
}

} // namespace

PORT_OVERRIDE void CMap::LoadData(unsigned int *pcp_pack, unsigned int *img_pack, int *tex_block,
                                  mgCMemory *stack) {
    if (mds_list_set == NULL) {
        return;
    }

    int block = *tex_block;
    mgCTextureManager *manager = &mgTexManager;

    for (int index = 0;; index++) {
        char *name = map_info.GetImgName(index);
        if (name == NULL) {
            break;
        }

        unsigned int *file = GetPackFile(img_pack, name, NULL);
        std::fprintf(stdout, "%p %s\n", static_cast<void *>(file), name);
        if (file == NULL) {
            continue;
        }

        const int first_block = block;
        if (TraceMapLoads()) {
            std::fprintf(stderr, "map-load: IMG %s enter block=%d\n", name, block);
        }
        mgCEnterIMGInfo info;
        block += manager->EnterIMGFile(reinterpret_cast<u_char *>(file), block, stack, &info);
        block++;
        if (TraceMapLoads()) {
            std::fprintf(stderr, "map-load: IMG %s imported; ending block=%d\n", name, first_block);
        }
        manager->EndEnterTexture(first_block);
        if (TraceMapLoads()) {
            std::fprintf(stderr, "map-load: IMG %s texture reload returned\n", name);
        }
        mds_list_set->LoadIMGFile(name, &info, stack);
        if (TraceMapLoads()) {
            std::fprintf(stderr, "map-load: IMG %s registration returned\n", name);
        }
    }

    for (int index = 0;; index++) {
        char *name = map_info.GetPCPName(index);
        if (name == NULL) {
            break;
        }

        unsigned int *file = GetPackFile(pcp_pack, name, NULL);
        if (file == NULL) {
            continue;
        }

        if (TraceMapLoads()) {
            std::fprintf(stderr, "map-load: PCP %s begin\n", name);
        }
        mds_list_set->LoadPCPFile(name, file, stack, map_info.all_scissor);
        if (TraceMapLoads()) {
            std::fprintf(stderr, "map-load: PCP %s returned\n", name);
        }
    }

    *tex_block = block - *tex_block;
    if (TraceMapLoads()) {
        std::fprintf(stderr, "map-load: complete, consumed %d texture blocks\n", *tex_block);
    }
}

PORT_OVERRIDE void CMap::CreateEffect(unsigned int *pack, int tex_block, mgCMemory *stack) {
    char name[] = "test";
    if (TraceMapLoads()) {
        std::fprintf(stderr, "map-load: effects begin block=%d\n", tex_block);
    }
    effect_list.LoadEFPFile(name, pack, tex_block, stack);
    if (TraceMapLoads()) {
        std::fprintf(stderr, "map-load: effects returned\n");
    }
}

PORT_OVERRIDE void CMap::CreateMap(CMdsListSet *list_set, mgCMemory *stack) {
    char *script;
    int size;
    (void)list_set;

    script = map_info.GetAddMapFile(&size);
    if (TraceMapLoads()) {
        std::fprintf(stderr, "map-load: add-map script begin size=%d\n", size);
    }
    if (script != NULL && size > 0) {
        LoadMapFile(script, size, stack, 1);
    }
    if (TraceMapLoads()) {
        std::fprintf(stderr, "map-load: add-map script returned\n");
    }

    script = map_info.GetMapFile(&size);
    if (TraceMapLoads()) {
        std::fprintf(stderr, "map-load: map script begin size=%d\n", size);
    }
    LoadMapFile(script, size, stack, 0);
    if (TraceMapLoads()) {
        std::fprintf(stderr, "map-load: map script returned\n");
    }
}

PORT_OVERRIDE void CMap::AssignFuncPoint(mgCMemory *stack) {
    CMapParts *parts;
    const bool trace = TraceMapLoads();
    if (trace) {
        std::fprintf(stderr, "map-load: function points count begin\n");
    }
    obj_anime_num = func_point.GetNum(FUNC_POINT_ANIME);
    if (trace) {
        std::fprintf(stderr, "map-load: function points count=%d\n", obj_anime_num);
    }

    if (obj_anime_num <= 0) {
        return;
    }

    const int count = obj_anime_num;
    const unsigned int bytes = static_cast<unsigned int>(count) * 0x30;
    const unsigned int blocks = (bytes & 0xF) == 0 ? (bytes >> 4) : ((bytes >> 4) + 1);
    obj_anime = new (stack->Alloc(blocks + 2)) CObjAnime[count];
    CFuncPoint *point;
    CObjAnime *animation = obj_anime;

    if (animation == NULL) {
        if (trace) {
            std::fprintf(stderr, "map-load: function point animation allocation failed\n");
        }
        return;
    }

    func_point.GetStart(FUNC_POINT_ANIME);
    int index = 0;
    while ((point = func_point.Get()) != NULL) {
        if (index >= count) {
            if (trace) {
                std::fprintf(stderr, "map-load: function point list exceeded count=%d; stopping\n", count);
            }
            break;
        }
        animation->frame = NULL;
        animation->piece = NULL;
        animation->parts = NULL;
        animation->func_point = point;
        animation->back = 0;
        animation->stop = 0;
        parts = NULL;

        if (point->anime.parts_name != NULL) {
            parts = GetPlaceParts(point->anime.parts_name);
        }

        if (trace) {
            std::fprintf(stderr, "map-load: function point %d AssignFuncAnime begin parts=%p\n", index,
                         static_cast<void *>(parts));
        }
        animation->AssignFuncAnime(point, parts);
        if (trace) {
            std::fprintf(stderr, "map-load: function point %d AssignFuncAnime returned\n", index);
        }
        animation++;
        ++index;
    }
    func_point.GetEnd();
    if (trace) {
        std::fprintf(stderr, "map-load: function point assignment returned count=%d\n", index);
    }
}
