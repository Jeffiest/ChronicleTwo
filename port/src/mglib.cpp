#include "mglib.hpp"

#include <cstring>

#include "mg_math.hpp"
#include "platform/clock.hpp"

// The VSync and timing group of ps2/src/mglib.cpp, moved onto the platform clock
// (platform/clock.hpp) in place of the PS2's root counter and VSync interrupt. The count the game
// reads is the clock's tick count and every wait pumps it, so the platform (window, input, audio)
// keeps moving while the game waits for its next field. The rest of ps2/src/mglib.cpp - the draw
// managers, the packet builders, the projection helpers, the VU program table - is left to it.

namespace {

// A packet cursor with a home, so a write that lands before a mode has initialised the real
// packet buffers cannot reach nothing.
sceVif1Packet g_null_packet;
unsigned int  g_null_packet_words[4];

// mgDBuffID of retail, kept here because both frame functions read and flip it.
int g_display_buffer;

// Vertical-sync count the previous frame ended at, and the mgFrameRate-tick interval the next one
// waits for.
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

void mgBeginFrame(mgCDrawManager *manager) {
    if (manager == NULL) {
        mgDrawManager.texture_manager = &mgTexManager;
        manager = &mgDrawManager;
        mgDrawManager.render_info = &mgRenderInfo;
    }

    // Retail sampled TIMER0 here to price the frame at its end; the frame's time comes from the
    // clock now.
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
    *(u_long128 *) &mgGiftagAD = 0;
    mgGiftagAD.EOP = 1;
    mgGiftagAD.NREG = 1;
    mgGiftagAD.REGS0 = SCE_GIF_PACKED_AD;
    sceVif1PkCnt(mgVif1Packet, 0);
    sceVif1PkOpenDirectCode(mgVif1Packet, 0);
    sceVif1PkOpenGifTag(mgVif1Packet, *(u_long128 *) &mgGiftagAD);
    sceVif1PkAddGsAD(mgVif1Packet, SCE_GS_SCANMSK, 0);
    sceVif1PkAddGsAD(mgVif1Packet, SCE_GS_TEXFLUSH, 0);
    sceVif1PkCloseGifTag(mgVif1Packet);
    sceVif1PkCloseDirectCode(mgVif1Packet);
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
