#include "common.h"

#include <libvu0.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "gfx/gfx.hpp"
#include "mg_dataset.hpp"
#include "mg_drawenv.hpp"
#include "mg_drawprim.hpp"
#include "mg_visual.hpp"

// The host replacement for the model dataset side. The ps2 file of this name - the MDS loader,
// frame copying and the MDT builder - is plain CPU work and still links from ps2/src. What the
// renderer cannot take from there is the VU1 vertex stream the face lists describe, so this unit
// collects those lists into the renderer's vertices and indices. mg_visual.cpp makes the draws
// from what this unit hands it.

namespace {

// Game colours are fractions of 128: 1.0 is the GS's 0x80.
uint8_t ColourValue(float value) {
    float scaled = value * 128.0f;
    return static_cast<uint8_t>(scaled <= 0.0f ? 0 : scaled >= 255.0f ? 255 : scaled + 0.5f);
}

// VU1 took texture coordinates scaled by the texture's power-of-two size (TEX0 TW/TH); the
// renderer samples in texels of the image it was given, which only differs for a non-power-of-two
// texture.
float UvScale(u_long tex0, const mgCTexture *texture, int axis) {
    if (texture == nullptr || texture->width <= 0 || texture->height <= 0) {
        return 1.0f;
    }
    unsigned shift = axis == 0 ? static_cast<unsigned>(tex0 >> 26) & 0xF : static_cast<unsigned>(tex0 >> 30) & 0xF;
    unsigned size = axis == 0 ? static_cast<unsigned>(texture->width) : static_cast<unsigned>(texture->height);
    return static_cast<float>(1u << shift) / static_cast<float>(size);
}

// A strip becomes a list wound as its first triangle, as the GS draws it.
void StripToList(std::vector<uint32_t> &indices, uint32_t first, uint32_t count) {
    for (uint32_t k = 0; k + 2 < count; k++) {
        uint32_t a = first + k;
        if (k & 1) {
            indices.insert(indices.end(), {a + 1, a, a + 2});
        } else {
            indices.insert(indices.end(), {a, a + 1, a + 2});
        }
    }
}

} // namespace

// One face group (a material's primitives) into a triangle list. The ps2 loader built mgCFace
// with an index stride that says which of position, normal, texture coordinate and colour each
// vertex carries; the order is exactly that. Returns the number of vertices appended; *has_colour
// is set when any vertex carried a colour.
int PortMgCollectGroup(const mgCVisualMDT *visual, const mgFACE_GROUP *group,
                       std::vector<gfx::Vertex3D> &vertices, std::vector<uint32_t> &indices,
                       bool *has_colour) {
    if (visual == nullptr || group == nullptr) {
        return 0;
    }

    const mgCTexture *texture = nullptr;
    u_long            tex0 = 0;

    if (visual->material != nullptr) {
        texture = (visual->material + group->material)->texture;
    }

    if (texture != nullptr) {
        tex0 = texture->tex0_bits;
    }

    float su = UvScale(tex0, texture, 0);
    float sv = UvScale(tex0, texture, 1);
    int   appended = 0;

    for (const mgCFace *face = group->face; face != nullptr; face = face->next) {
        int  primitive = face->type & MG_FACE_PRIM_MASK;
        bool colour = (face->type & MG_FACE_COLOUR) != 0;
        bool normal = (face->type & MG_FACE_NO_NORMAL) == 0;
        bool uv = (face->type & MG_FACE_NO_TEXTURE) == 0;
        int  stride = face->index_stride;
        int  offset = static_cast<int>(vertices.size());

        if (stride <= 0) {
            continue;
        }

        if (colour) {
            *has_colour = true;
        }

        for (int i = 0; i < face->vertex_num; i++) {
            const int *index = face->index + i * stride;
            gfx::Vertex3D vertex = {};
            int           cursor = 0;

            std::memcpy(vertex.position, visual->vertex[index[cursor]], sizeof(vertex.position));
            cursor++;

            if (normal && visual->normal != nullptr) {
                std::memcpy(vertex.normal, visual->normal[index[cursor]], sizeof(vertex.normal));
            } else {
                vertex.normal[1] = 1.0f;
            }
            if (normal) {
                cursor++;
            }

            if (uv && visual->uv != nullptr) {
                vertex.uv[0] = visual->uv[index[cursor]][0] * su;
                vertex.uv[1] = visual->uv[index[cursor]][1] * sv;
                cursor++;
            }

            if (colour && visual->colour != nullptr) {
                for (int channel = 0; channel < 4; channel++) {
                    vertex.color[channel] = ColourValue(visual->colour[index[cursor]][channel]);
                }
            } else {
                vertex.color[0] = vertex.color[1] = vertex.color[2] = vertex.color[3] = 0x80;
            }

            vertices.push_back(vertex);
        }

        if (primitive == MG_PRIM_TRIANGLE_STRIP) {
            StripToList(indices, static_cast<uint32_t>(offset), static_cast<uint32_t>(face->vertex_num));
        } else if (primitive == MG_PRIM_TRIANGLE_FAN) {
            for (int i = 1; i + 1 < face->vertex_num; i++) {
                indices.insert(indices.end(), {static_cast<uint32_t>(offset), static_cast<uint32_t>(offset + i),
                                               static_cast<uint32_t>(offset + i + 1)});
            }
        } else if (primitive == MG_PRIM_TRIANGLE) {
            for (int i = 0; i + 2 < face->vertex_num; i += 3) {
                indices.insert(indices.end(), {static_cast<uint32_t>(offset + i), static_cast<uint32_t>(offset + i + 1),
                                               static_cast<uint32_t>(offset + i + 2)});
            }
        }
        // Points, lines and GS sprites are not part of the models the loader builds; a face of
        // one draws nothing here.

        appended += face->vertex_num;
    }

    return appended;
}
