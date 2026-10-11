#include "common.h"

#include "menudraw.hpp"

// The template is declared in menudraw.hpp but has no definition; the float and int rectangles are
// the retail PrimQuad_f_ and PrimQuad_i_ in ps2/src/menudraw.cpp, which are reached from here.
template <>
void PrimQuad<float>(mgCDrawPrim *prim, mgRect<float> put_rect, mgRect<int> tex_rect) {
    PrimQuad_f_(prim, mgRect_f_{put_rect.left, put_rect.top, put_rect.right, put_rect.bottom},
                mgRect_i_{tex_rect.left, tex_rect.top, tex_rect.right, tex_rect.bottom});
}

template <>
void PrimQuad<int>(mgCDrawPrim *prim, mgRect<int> put_rect, mgRect<int> tex_rect) {
    PrimQuad_i_(prim, mgRect_i_{put_rect.left, put_rect.top, put_rect.right, put_rect.bottom},
                mgRect_i_{tex_rect.left, tex_rect.top, tex_rect.right, tex_rect.bottom});
}
