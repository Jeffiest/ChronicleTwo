#define MGLIB_IMPLICIT_TEX0_ASSIGNMENT

#include <cstring>

// The game headers keep MWCC's pragmas, which clang names but ignores.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#include "mg_math.hpp"
#include "mglib.hpp"
#pragma clang diagnostic pop

#include "gfx/gfx.hpp"
#include "platform/clock.hpp"

// The VSync and timing group of ps2/src/mglib.cpp, moved onto the platform clock
// (platform/clock.hpp) in place of the PS2's root counter and VSync interrupt, plus the frame
// path's packet-layer functions. The count the game reads is the clock's tick count and every
// wait pumps it, so the platform (window, input, audio) keeps moving while the game waits for its
// next field. The VIF1 packet builder has no host counterpart, so the frame functions keep the
// manager and frame state the game reads and leave the packet bytes to ps2/src's weak copies
// elsewhere in the unit (mgSetPkMoveImage, mgStoreImage, the direct-draw entry points).

// A packet cursor with a home, so a write that lands before a mode has initialised the real
// packet buffers cannot reach nothing. The size is one frame's packet - the game's menu sizes its
// buffers 0x27100 bytes - so a direct-draw write that still lands here corrupts nothing. The
// packet is shared with the sce packet stubs, which pass quietly on it.
sceVif1Packet g_null_packet;

namespace {

unsigned int g_null_packet_words[0x2710];

// The packet and draw-data stacks the game alternates between frames, as mgSetPacketBuffer and
// mgSetDataBuffer were handed them. ps2/src/mglib.cpp keeps its own file-static copies; the
// functions that need them are here.
mgCMemory g_packet_buf[2];
mgCMemory g_data_buf[2];

// Index of the packet and data buffers used by the current frame (retail's mgDataID).
int g_data_id;

// Texture description of the frame buffer mgSetPkFrameBuffer last set (retail's frame_tex).
mgCTexture g_frame_tex;

// mgDBuffID of retail, kept here because the frame functions read and flip it.
int g_display_buffer;

// Vertical-sync count the previous frame ended at; the next frame waits mgFrameRate ticks past it.
int g_old_vcount;

// mgPerformanceMeter's switch, the same file-static ps2/src/mglib.cpp keeps.
int g_display_performance_meter;

// The normalized screen mode and its centered bounds; retail's GetScreenSize, with the data the
// port keeps the same way.
int GetScreenSize(int mode, int *width, int *height, int *left, int *top, int *right, int *bottom) {
    switch (mode) {
        case MG_SCREEN_MODE_640X448:
            *width = 640;
            *height = 448;
            break;
        case MG_SCREEN_MODE_512X480:
            *width = 512;
            *height = 480;
            break;
        case MG_SCREEN_MODE_512X416:
            *width = 512;
            *height = 416;
            break;
        case MG_SCREEN_MODE_512X448:
        default:
            mode = MG_SCREEN_MODE_512X448;
            *width = 512;
            *height = 448;
            break;
    }

    *left = -(*width >> 1);
    *top = -(*height >> 1);
    *right = *width + *left;
    *bottom = *height + *top;
    return mode;
}

} // namespace

void mgPerformanceMeter(int enable) {
    g_display_performance_meter = enable;
}

int mgGetPerformanceMeterFlag() {
    return g_display_performance_meter;
}

// The game's per-field callback, which retail's VSync interrupt ran. The clock runs it once for
// every tick a pump elapses, with the count already advanced to that tick.
void mgInitVSyncCallBack(int (*callback)(int)) {
    ClockSetTickCallback(callback);
}

// Retail's loop rotated the ready queue on the EE while it watched vcount; the port waits on the
// clock, which pumps the platform as it sleeps and counts the tick itself.
void WaitVSync(int start, int frames) {
    while (mgGetVSyncCount() - start < frames) {
        ClockSyncV();
    }
}

int mgGetVSyncCount() {
    return static_cast<int>(ClockTickCount());
}

void mgInit(int screen_mode, int video_mode) {
    // Retail's mgInit resets DMA and the GS, clears video memory, builds the double buffer and
    // loads the microprograms over the VIF1 path, then installs its VSync handler. The host has
    // none of that; what remains is the state the rest of the game reads.
    (void) video_mode;

    mgAntialiasing = true;
    g_display_buffer = 0;
    g_data_id = 0;
    std::memset(&mgRenderInfo, 0, sizeof(mgRenderInfo));
    mgVif1Packet = &g_null_packet;
    g_null_packet.pBase = reinterpret_cast<u_int *>(g_null_packet_words);
    g_null_packet.pCurrent = reinterpret_cast<u_int *>(g_null_packet_words);
    *(u_long128 *) &mgGiftagAD = 0;
    mgGiftagAD.EOP = 1;
    mgGiftagAD.NREG = 1;
    mgGiftagAD.REGS0 = SCE_GIF_PACKED_AD;

    mgScreenMode = GetScreenSize(screen_mode, &mgScreenWidth, &mgScreenHeight, &mgScreenNX, &mgScreenNY, &mgScreenMX,
                                 &mgScreenMY);
    int aligned_height = mgScreenHeight;

    if (aligned_height % 32 != 0) {
        aligned_height += 32 - aligned_height % 32;
    }

    mgScreenOffx = 0x800 - mgScreenWidth / 2;
    mgScreenOffy = 0x800 - mgScreenHeight / 2;
    mgScreenLeft = mgScreenOffx;
    mgScreenRight = mgScreenOffx + mgScreenWidth;
    mgScreenTop = mgScreenOffy;
    mgScreenBottom = mgScreenOffy + mgScreenHeight;
    mgScreenDepth = 32;
    mgScreenZDepth = 32;

    // What sceGsSetDefDBuff fills for SCE_GS_PSMCT32 and SCE_GS_PSMZ24, with the frame bases and
    // the z buffer address retail patches in right after: the second frame sits past the
    // height-aligned first one, and the z buffer past both.
    std::memset(&mgDBuff, 0, sizeof(mgDBuff));
    int frame_buf1 = mgScreenDepth * (mgScreenWidth * aligned_height / 2048) / 32;
    mgDBuff.draw0.frame1.FBW = mgScreenWidth / 64;
    mgDBuff.draw0.frame1.PSM = SCE_GS_PSMCT32;
    mgDBuff.draw1.frame1.FBW = mgScreenWidth / 64;
    mgDBuff.draw1.frame1.PSM = SCE_GS_PSMCT32;
    mgDBuff.draw0.frame1.FBP = frame_buf1;
    mgDBuff.draw1.frame1.FBP = 0;
    mgDBuff.draw0.zbuf1.bits.zbp = mgDBuff.draw1.zbuf1.bits.zbp = frame_buf1 * 2;
    mgDBuff.draw0.zbuf1.bits.psm = mgDBuff.draw1.zbuf1.bits.psm = SCE_GS_PSMZ24 & 0xF;
    mgBackColor[0] = 0.0f;
    mgBackColor[1] = 0.0f;
    mgBackColor[2] = 0.0f;
    mgBackColor[3] = 128.0f;
    mgClearBackFlag = true;
    mgDBuff.clear0.rgbaq.bytes.red = (int) mgBackColor[0];
    mgDBuff.clear0.rgbaq.bytes.green = (int) mgBackColor[1];
    mgDBuff.clear0.rgbaq.bytes.blue = (int) mgBackColor[2];
    mgDBuff.clear0.rgbaq.bytes.alpha = (int) mgBackColor[3];
    mgDBuff.clear1.rgbaq.bytes.red = (int) mgBackColor[0];
    mgDBuff.clear1.rgbaq.bytes.green = (int) mgBackColor[1];
    mgDBuff.clear1.rgbaq.bytes.blue = (int) mgBackColor[2];
    mgDBuff.clear1.rgbaq.bytes.alpha = (int) mgBackColor[3];
    *(u_long *) &mgTEX1_1 = 0x261;
    mgTEX1_2 = mgTEX1_1;
    *(u_long *) &mgTEST_1 = 0x5000B;
    mgTEST_2 = mgTEST_1;
    mgZBUF_1 = mgZBUF_2 = mgDBuff.draw0.zbuf1;
    *(u_long *) &mgALPHA_1 = 0x44;
    mgALPHA_2 = mgALPHA_1;
    *(u_long *) &mgTEXA_1 = 0x100400000ULL;
    mgTEXA_2 = mgTEXA_1;
    mgDrawManager.texture_manager = &mgTexManager;
    mgDrawManager.render_info = &mgRenderInfo;
    mgRenderInfo.Initialize();
    mgRenderInfo.draw_env[0].zbuf = mgZBUF_1;
    mgRenderInfo.draw_env[1].zbuf = mgZBUF_2;
    mgFrameRate = 2;
    // The dither presets and the register shadows swept into VRAM were for the GS alone.

    ClockSetTickCallback(nullptr);
    ClockReset();
    g_old_vcount = 0;
    g_display_performance_meter = 0;
    mgCreateSinTable();
}

void mgInitVif1Packet(u_long128 *buffer_a, u_long128 *buffer_b, int size) {
    // The two VIF1 cursors retail built here wrote GS packets; the host renderer builds its own
    // state, so there is nothing to initialize. The callers keep their shape.
    (void) buffer_a;
    (void) buffer_b;
    (void) size;
}

void mgSetPacketBuffer(mgCMemory *pool_a, mgCMemory *pool_b) {
    g_packet_buf[0] = *pool_a;
    g_packet_buf[1] = *pool_b;
    g_packet_buf[0].stack_used = 0;
    g_packet_buf[0].lock = 0;
    g_packet_buf[1].stack_used = 0;
    g_packet_buf[1].lock = 0;
}

void mgSetDataBuffer(mgCMemory *pool_a, mgCMemory *pool_b, int skip_header) {
    u_char *base = (u_char *) pool_a->stAllocTest(1);
    int     size = pool_a->stack_size - pool_a->stack_used;

    if (skip_header != 0) {
        base += 0x4000;
        size -= 0x800;
    }

    g_data_buf[0].stSetBuffer((u_long128 *) base, size);
    base = (u_char *) pool_b->stAllocTest(1);
    size = pool_b->stack_size - pool_b->stack_used;

    if (skip_header != 0) {
        base += 0x4000;
        size -= 0x800;
    }

    g_data_buf[1].stSetBuffer((u_long128 *) base, size);
    g_data_buf[0].stack_used = 0;
    g_data_buf[0].lock = 0;
    g_data_buf[1].stack_used = 0;
    g_data_buf[1].lock = 0;
}

mgCMemory *mgGetDataBuffer() {
    return &g_data_buf[g_data_id];
}

// The frame buffer the current display buffer shows, decoded as the texture description the game
// reads it as. The packet retail also built here had no reader without a GS.
void mgSetPkFrameBuffer(int fbp, int width, int height, int psm) {
    sceGsFrame *default_frame;
    int         screen_width;
    int         screen_height;
    int         aligned_width;
    int         width_shift;
    int         height_shift;
    int         size;
    int         bpp;
    int         bit;

    default_frame = g_display_buffer != 0 ? &mgDBuff.draw0.frame1 : &mgDBuff.draw1.frame1;

    if (fbp < 0) {
        fbp = default_frame->FBP;
    }

    if (psm < 0) {
        psm = default_frame->PSM;
    }

    GetScreenSize(mgScreenMode, &screen_width, &screen_height, &mgScreenNX, &mgScreenNY, &mgScreenMX, &mgScreenMY);

    if (width < 0) {
        width = screen_width;
    }

    if (height < 0) {
        height = screen_height;
    }

    mgScreenWidth = width;
    mgScreenHeight = height;
    mgScreenNX = -width / 2;
    mgScreenNY = -height / 2;
    mgScreenMX = mgScreenNX + width;
    mgScreenMY = mgScreenNY + height;
    aligned_width = width;

    if (aligned_width % 64 != 0) {
        aligned_width += 64 - aligned_width % 64;
    }

    sceGsFrame frame = *default_frame;
    frame.FBP = fbp;
    frame.FBW = aligned_width / 64;
    frame.PSM = psm;
    frame.bits.fbmsk = 0;
    mgFRAME_1 = frame;
    mgScreenOffx = 0x800 - width / 2;
    mgScreenOffy = 0x800 - height / 2;
    bpp = 0;

    switch (psm) {
        case SCE_GS_PSMCT32:
            bpp = 32;
            break;
        case SCE_GS_PSMCT24:
            bpp = 24;
            break;
        case SCE_GS_PSMCT16:
            bpp = 16;
            break;
        case SCE_GS_PSMCT16S:
            bpp = 16;
            break;
    }

    g_frame_tex.Initialize();
    g_frame_tex.width = width;
    g_frame_tex.height = height;
    g_frame_tex.bpp = bpp;
    g_frame_tex.vram_size = bpp * (width * height) / 8 / 256;
    g_frame_tex.clut_size = 0;
    g_frame_tex.image_blocks = g_frame_tex.vram_size;
    g_frame_tex.tex0.value = 0;
    g_frame_tex.tex0.TBP0 = fbp << 5;
    g_frame_tex.tex0.TBW = width / 64;
    g_frame_tex.tex0.PSM = psm;
    width_shift = 0;
    height_shift = 0;

    for (size = width; size > 1; size >>= 1) {
        width_shift++;
    }

    size = 1;

    for (bit = 0; bit < width_shift; bit++) {
        size *= 2;
    }

    if (width != size) {
        width_shift++;
    }

    for (size = height; size > 1; size >>= 1) {
        height_shift++;
    }

    size = 1;

    for (bit = 0; bit < height_shift; bit++) {
        size *= 2;
    }

    if (height != size) {
        height_shift++;
    }

    g_frame_tex.tex0.bits.tw = width_shift;
    g_frame_tex.tex0.bits.th = height_shift;
    g_frame_tex.tex0.bits.tcc = 1;
    g_frame_tex.tex0.bits.tfx = 0;
    *(u_long *) &g_frame_tex.tex1 = SCE_GS_SET_TEX1(1, 0, 1, 1, 1, 0, 0);
}

void mgGetFrameBuffer(mgCTexture *texture) {
    *texture = g_frame_tex;
}

void mgGetFrameBackBuffer(mgCTexture *texture) {
    sceGsFrame *draw_env;

    if (g_display_buffer != 0) {
        draw_env = &mgDBuff.draw1.frame1;
    } else {
        draw_env = &mgDBuff.draw0.frame1;
    }

    *texture = g_frame_tex;
    texture->tex0.TBP0 = draw_env->FBP * 32;
}

void mgSetPkClearScreen(u_char red, u_char green, u_char blue, u_char alpha) {
    // The clear's colour and enable are already in mgBackColor and mgClearBackFlag, which the
    // renderer's frame begin reads; the GS registers and vertices retail wrote here are the
    // renderer's own.
    (void) red;
    (void) green;
    (void) blue;
    (void) alpha;
}

void mgSetPkTextureRepeat(int mode) {
    // The sampler's wrap mode is renderer state on the host; the CLAMP register retail wrote is
    // not.
    (void) mode;
}

void mgSetPkTextureRepeat(sceGsClamp clamp) {
    (void) clamp;
}

void mgFlushRenderInfo() {
    // Retail wrote the fog coefficient and guard values into the frame's packet; they live in
    // mgRenderInfo for the renderer, so there is nothing to flush.
}

void mgBeginPacket(mgCDrawManager *manager) {
    if (manager == NULL) {
        manager = &mgDrawManager;
    }

    // The VIF1 builder retail reset here has no host counterpart; the packet cursor parks on the
    // null packet and the draw managers allocate from the stacks the game set up.
    mgVif1Packet = &g_null_packet;
    g_packet_buf[g_data_id].stack_used = 0;
    g_packet_buf[g_data_id].lock = 0;
    g_data_buf[g_data_id].stack_used = 0;
    g_data_buf[g_data_id].lock = 0;
    manager->packet_memory = &g_packet_buf[g_data_id];
    manager->data_memory = &g_data_buf[g_data_id];
    manager->SetSortTable(1);
}

void mgSendPacket(mgCDrawManager *manager) {
    // The DMA send has no host counterpart; the flip to the other frame's stacks still happens.
    (void) manager;
    g_data_id = !g_data_id;
}

void mgEndPacket(mgCDrawManager *manager) {
    // sceVif1PkEnd and sceVif1PkTerminate wrote the packet's DMA tag; there is no host packet.
    (void) manager;
}

void mgBeginFrame(mgCDrawManager *manager) {
    if (manager == NULL) {
        mgDrawManager.texture_manager = &mgTexManager;
        manager = &mgDrawManager;
        mgDrawManager.render_info = &mgRenderInfo;
    }

    // Retail opened the frame's VIF1 packet here; the host counterpart is the renderer's display
    // list, which collects every draw the tick makes until mgEndFrame renders and presents it.
    if (!gfx::Recording() && !gfx::InFrame()) {
        gfx::BeginRecording();
    }

    // Retail sampled TIMER0 here to price the frame at its end and opened the frame's VIF1
    // packet; the frame's time comes from the clock now, and the packet has no host counterpart.
    int red = (int) mgBackColor[0];
    int green = (int) mgBackColor[1];
    int blue = (int) mgBackColor[2];
    int alpha = (int) mgBackColor[3];
    mgDBuff.clear0.rgbaq.bytes.red = red;
    mgDBuff.clear0.rgbaq.bytes.green = green;
    mgDBuff.clear0.rgbaq.bytes.blue = blue;
    mgDBuff.clear1.rgbaq.bytes.red = red;
    mgDBuff.clear1.rgbaq.bytes.green = green;
    mgDBuff.clear1.rgbaq.bytes.blue = blue;
    mgDBuff.clear0.rgbaq.bytes.alpha = alpha;
    mgDBuff.clear1.rgbaq.bytes.alpha = alpha;
    mgBeginPacket(manager);
    mgSetPkTextureRepeat(1);
    sceGsFrame *draw_env;

    if (g_display_buffer != 0) {
        draw_env = &mgDBuff.draw0.frame1;
    } else {
        draw_env = &mgDBuff.draw1.frame1;
    }

    mgFRAME_1.value = draw_env->value;
    mgSetPkFrameBuffer(-1, -1, -1, -1);
    mgSetPkClearScreen(mgDBuff.clear0.rgbaq.bytes.red, mgDBuff.clear0.rgbaq.bytes.green,
                       mgDBuff.clear0.rgbaq.bytes.blue, mgDBuff.clear0.rgbaq.bytes.alpha);
    mgFlushRenderInfo();
}

void mgEndFrame(mgCDrawManager *manager) {
    static int   count = 1;
    static float free_ratio = 0.0f;
    int          sample;

    mgWaitFrame();

    if (g_display_performance_meter != 0) {
        float packet_free = 100.0f * (float) (mgDrawManager.packet_memory->stack_size - mgDrawManager.packet_memory->stack_used) / (float) mgDrawManager.packet_memory->stack_size;
        float data_free = 100.0f * (float) (mgDrawManager.data_memory->stack_size - mgDrawManager.data_memory->stack_used) / (float) mgDrawManager.data_memory->stack_size;
        // No root counter to price the tick against, so the CPU bar stays empty.
        float cpu_ratio = 0.0f;
        mgCDrawPrim prim;

        prim.Initialize(NULL, NULL);
        prim.DepthTestEnable(0);
        prim.TextureMapEnable(0);
        prim.AlphaBlendEnable(1);
        prim.ZMask(MG_Z_MASK_MASKED);
        prim.Begin(MG_PRIM_SPRITE);
        prim.Color(128, 128, 128, 128);
        int top = mgScreenHeight - 40;

        if (free_ratio <= 0.0f) {
            prim.Color(255, 0, 0, 64);
        } else {
            prim.Color(128, 128, 128, 64);
        }

        prim.Vertex(mgScreenWidth, top, 0);
        prim.Vertex(mgScreenWidth - 100, top + 8, 0);
        prim.Vertex(mgScreenWidth, top + 12, 0);
        prim.Vertex(mgScreenWidth - 100, top + 16, 0);

        if (!(cpu_ratio <= 100.0f)) {
            prim.Color(255, 0, 0, 64);
        } else if (!(cpu_ratio <= 50.0f)) {
            prim.Color(0, 64, 64, 64);
        } else {
            prim.Color(0, 0, 128, 64);
        }

        prim.Vertex(mgScreenWidth, top, 0);
        prim.Vertex((float) mgScreenWidth - cpu_ratio, (float) (top + 8), 0.0f);
        prim.Vertex((float) mgScreenWidth - (100.0f - free_ratio), (float) (top + 12), 0.0f);
        prim.Vertex(mgScreenWidth - 100, top + 16, 0);
        prim.Color(0, 128, 0, 64);
        prim.Vertex(mgScreenWidth, top + 20, 0);
        prim.Vertex((float) mgScreenWidth - packet_free, (float) (top + 24), 0.0f);
        prim.Vertex(mgScreenWidth, top + 28, 0);
        prim.Vertex((float) mgScreenWidth - data_free, (float) (top + 32), 0.0f);
        prim.End();
    }

    mgEndPacket(NULL);

    // Retail read an 8x8 block of the depth buffer back from the GS for every enabled pick; the
    // renderer's own readback is where those samples have to come from, so until it is wired in,
    // nothing is reported under the cursor.
    for (sample = 0; sample < 4; sample++) {
        if (mgPickZBuff[sample].enable != 0) {
            mgPickZBuff[sample].z = -1;
        }
    }

    // A frame spans mgFrameRate ticks; waiting pumps the clock, which pumps the platform with it.
    WaitVSync(g_old_vcount, mgFrameRate);
    g_old_vcount = mgGetVSyncCount();

    if (mgAntialiasing != 0) {
        mgDBuff.disp[g_display_buffer].pmode = 0x7F23;
    } else {
        mgDBuff.disp[g_display_buffer].pmode = 0xFF23;
    }

    // Retail measured the frame from TIMER0; the port runs the cadence the game asks for, which is
    // what mgGetNowFrameRate reports.
    mgNowFrameRate = (float) mgFrameRate;
    free_ratio = 100.0f;

    count++;

    if (count > 60 / mgFrameRate) {
        count = 0;
    }

    mgSendPacket(NULL);
    g_display_buffer = !g_display_buffer;

    // The tick is complete: its display list becomes the canonical image (what kPreviousFrame and
    // the screenshot read) and is presented. Retail got this from the frame packet's DMA; the
    // host renders the list in its place.
    if (gfx::Recording()) {
        gfx::DisplayListRef list = gfx::EndRecording();
        if (list != nullptr) {
            gfx::RenderOptions canonical;
            canonical.canonical = true;
            gfx::RenderList(*list, 1.0f, canonical);
            gfx::PresentCanonical();
        }
    }
}

void mgWaitFrame() {
    // Retail waited for the VIF1 path to drain before touching the frame's geometry; the port
    // records and draws synchronously, so there is nothing in flight to wait for.
}

// The debug frame grab went through the GS store-image path and wrote a TGA to host0:. The host
// renderer's readback is the port's to use, and belongs with the renderer's units; dropped until
// then so the debug key cannot reach hardware-free memory.
void mgStoreFrameImage() {
}

// Retail reset the frame's VIF1 builder around every direct draw; the builder has no host
// counterpart (the draw goes through the port's visual path, which records renderer draws), so
// the packet calls are skipped and the frame is drawn where it is.
int mgDrawDirect(mgCFrame *frame) {
    if (frame == NULL) {
        return 0;
    }

    return frame->Draw(mgVif1Packet->pCurrent);
}

int mgDrawDirect(mgCVisual *visual, float (*matrix)[4]) {
    if (visual == NULL) {
        return 0;
    }

    return visual->Draw(mgVif1Packet->pCurrent, matrix, NULL);
}

void mgDrawDirectStart() {
    ddraw_size = 0;
}

void mgDrawDirectEnd() {
    // Nothing to reserve without a host packet.
}
