#include "common.h"

// menudraw.hpp and what it pulls in carry MWCC pragmas clang has no name for; only the
// declarations of the rectangle primitive are wanted.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#include "menudraw.hpp"
#pragma clang diagnostic pop

// The title unit's overlays - the title screen's logo, prompt and menu rows, the copyright
// screen, the installation panel, the language rows - draw through the menu layer's rectangle
// primitive, and retail only ever had its float and int instantiations under the names MWCC
// gave them: PrimQuad_f_ / PrimQuad_i_ (menudraw.cpp, 0x234B20 / 0x234BD0). clang mangles the
// template apart from those names, so every PrimQuad<float> / PrimQuad<int> call in the title,
// dungeon-menu and editor-menu overlays was undefined. These give the template the two bodies;
// the primitive itself is the host draw path the mg_drawprim replacement feeds.
//
// The put rectangle is x, y, width and height, as its callers build it; the two opposite
// corners are textured.
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

// The water microprogram, the title unit's other hardware user: MainLoop installs it as user
// program 0 (mainloop.cpp:523) and every water surface water.cpp builds - the title screen's
// among them - is drawn by a VU1 packet that runs it. No host code executes VU1, and nothing
// reads the image: it only has to exist for the installed pointer table. The extent is the
// retail symbol table's - Vu_prog_wtr (0x329FD0) to the next top-level symbol, __data_start
// (0x32A320) - the boundary convention vuprog.cpp uses for the other microprogram images.
u_long128 Vu_prog_wtr[53] = {};
