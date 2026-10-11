#include "mglib.hpp"

/**
 * The effect units' direct-draw seam, and the model-draw entry most units use.
 *
 * Retail's mgDrawDirect family termininates the frame's VIF1 packet, has the frame or visual
 * append its commands at the packet cursor and reserves the words it wrote; the GS alone could
 * read them. The host's visuals submit gfx draws when called (mg_sprite.cpp, mg_visual.cpp and
 * mg_shadow.cpp take no cursor), so the cursor here is a private scratch buffer that exists to
 * keep mgCFrame::Draw's pointer arithmetic defined; a visual that still builds ps2 packets (the
 * motion models until their CPU path lands) writes harmlessly into it and draws nothing.
 */

namespace {

// One frame's packet, the size ps2/src/mglib.cpp's null packet keeps. Nothing reads the words
// back; the buffer exists so an unreplaced packet writer cannot write outside it.
u_int g_direct_cursor[0x2710];

// Quadwords the mgDrawDirect2 run has written, retail's mglib.cpp file static.
int g_direct_words;

} // namespace

int mgDrawDirect(mgCFrame *frame) {
    if (frame == NULL) {
        return 0;
    }

    return frame->Draw(g_direct_cursor);
}

int mgDrawDirect(mgCVisual *visual, float (*matrix)[4]) {
    if (visual == NULL) {
        return 0;
    }

    return visual->Draw(g_direct_cursor, matrix, NULL);
}

void mgDrawDirectStart() {
    g_direct_words = 0;
}

int mgDrawDirect2(mgCFrame *frame) {
    if (frame == NULL) {
        return 0;
    }

    int size = frame->Draw(g_direct_cursor + g_direct_words * 4);
    g_direct_words += size;
    return size;
}

void mgDrawDirectEnd() {
    // The run went straight into the renderer's frame; there is no packet to claim.
}
