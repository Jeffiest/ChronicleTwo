#include "common.h"

#include <vector>

#include <libvu0.h>

#include "mg_drawenv.hpp"
#include "mg_frame.hpp"
#include "mg_math.hpp"
#include "mg_visual.hpp"
#include "mg_visual_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "stubs/mg_frame.hpp"

PORT_OVERRIDE mgCFrame *mgCFrame::SearchFrame(char *name) {
    if (mgFrameNameComp(this->name, name) != 0) {
        return this;
    }

    // CHR attachment can leave a child/brother cycle on the PC. Keep retail's
    // child-before-sibling search order while bounding visits by the model's
    // frame count (or a conservative cap when this is a detached subframe).
    std::vector<mgCFrame *> pending;
    if (child != NULL) {
        pending.push_back(child);
    }

    const std::size_t visit_limit = frame_num > 0 ? static_cast<std::size_t>(frame_num) : 4096;
    std::size_t       visited = 0;
    while (!pending.empty() && visited < visit_limit) {
        mgCFrame *node = pending.back();
        pending.pop_back();
        if (node == NULL) {
            continue;
        }

        if (mgFrameNameComp(node->name, name) != 0) {
            return node;
        }
        ++visited;

        // The stack is LIFO: enqueue the sibling first to visit this node's
        // descendants before moving to the next sibling, as the retail loop does.
        if (node->brother != NULL) {
            pending.push_back(node->brother);
        }
        if (node->child != NULL) {
            pending.push_back(node->child);
        }
    }

    return NULL;
}

PORT_OVERRIDE int mgCFrame::Draw(unsigned int *packet) {
    mgRENDER_INFO *info = &mgRenderInfo;
    int            words = 0;
    sceVu0FMATRIX  world;

    if (attr == NULL) {
        GetLWMatrixTopBottom(world);
    } else {
        if (attr->billboard != 0) {
            GetBBoardMatrix(attr->billboard, world, info);
        } else {
            GetLWMatrixTopBottom(world);
        }

        while (attr != NULL && (attr->draw & MG_FRAME_DRAW_VISIBLE)) {
            if (visual == NULL) {
                break;
            }

            if (!attr->no_cull && bound != NULL) {
                sceVu0FVECTOR box_max;
                sceVu0FVECTOR box_min;
                test1(bound->corner, info->world_screen_rel, world, box_max, box_min);

                if (box_max[3] < info->clip_min[2]) {
                    break;
                }

                test2(box_max, box_min);

                const mgPortExtent extent = mgPortVisibleExtent();
                const float        centre_x = 0.5f * static_cast<float>(mgScreenWidth);
                const float        centre_y = 0.5f * static_cast<float>(mgScreenHeight);
                float visible_max[4] = {extent.right - centre_x, extent.bottom - centre_y, info->screen_box_max[2],
                                        info->screen_box_max[3]};
                float visible_min[4] = {extent.left - centre_x, extent.top - centre_y, info->screen_box_min[2],
                                        info->screen_box_min[3]};

                if (!mgClipBoxW(box_max, box_min, visible_max, visible_min)) {
                    break;
                }

                if (mgClipInBoxW(box_max, box_min, info->gs_box_max, info->gs_box_min)) {
                    info->clip = 0;
                    info->scissor = 0;
                } else {
                    info->clip = 1;

                    if (attr->program_mode & 2) {
                        info->scissor = attr->clip_enable != 0;
                    } else {
                        info->scissor = (attr->clip_enable != 0) | info->all_scissor;
                    }
                }
            }

            info->attr = attr;
            sceVu0CopyVector(info->object_color, attr->color);
            info->plight_hit = 0;

            if (info->plight_enable && attr->point_light && !attr->no_light && bound != NULL) {
                sceVu0FMATRIX transposed;
                sceVu0TransposeMatrix(transposed, world);
                float scale = mgDistVector(transposed[0]);
                float y_scale = mgDistVector(transposed[1]);
                float z_scale = mgDistVector(transposed[2]);
                scale = scale > y_scale ? (scale > z_scale ? scale : z_scale)
                                        : (y_scale > z_scale ? y_scale : z_scale);
                float         radius = bound->radius * scale;
                sceVu0FVECTOR center;
                sceVu0CopyVector(center, bound->center);
                center[3] = 1.0f;
                sceVu0ApplyMatrix(center, world, center);
                mgLIGHT_INFO *light = info->GetpLightInfo();

                for (int i = 0; i < 4; i++) {
                    if (!(light->point_light[i].power <= 0.0f)) {
                        float reach = radius + light->point_light[i].range;

                        if (!(reach <= mgDistVector(light->point_light[i].pos, center))) {
                            info->plight_hit = 1;
                            break;
                        }
                    }
                }
            }

            {
                mgVisualPortObjectScope object_scope(this);
                words += visual->Draw(packet, world, NULL);
            }
            break;
        }

        if (attr->draw & MG_FRAME_DRAW_SKIP_CHILDREN) {
            return words;
        }
    }

    for (mgCFrame *node = child; node != NULL; node = node->brother) {
        int use = 1;

        if (node->attr != NULL && (node->attr->draw & MG_FRAME_DRAW_SKIP_BY_PARENT)) {
            use = 0;
        }

        if (use) {
            words += node->Draw(packet + words * 4);
        }
    }

    return words;
}
