#include "mg_drawprim.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

#include <unordered_map>
#include <vector>

#include "gfx/gfx.hpp"
#include "mglib.hpp"

/**
 * The immediate primitive builder and the draw manager on the host.
 *
 * Retail builds DMA/VIF/GIF packets that the GS executes; the port has no GS, so the same calls
 * build gfx draws instead. A primitive run (Begin/BeginPrim2 to End/EndPrim2/Flush) accumulates
 * the vertices the game writes and submits one gfx::Draw2D call when the run closes, with the
 * draw state the prim's registers describe. The GS registers that outlive a packet (TEX0/TEX1,
 * TEXA, SCISSOR) live here as globals, as they do on the GS.
 *
 * The draw manager sorted packets by texture group for the GS's benefit; the renderer records
 * draws in the order they are made, so its bookkeeping is reduced to what the callers read back
 * (which groups exist).
 */

// What mg_drawenv.cpp provides: the GS units in the renderer's logical space and the draw state
// the registers describe. The manager's textures resolve through mg_texture.cpp's own binding.
namespace mgport {
float LogicalX(int gs_x);
float LogicalY(int gs_y);
float Depth(unsigned gs_z);
gfx::LogicalRect ScissorRect(u_long scissor);
gfx::DrawState DrawStateOf(const sceGsTest &test, const sceGsZbuf &zbuf, const sceGsAlpha &alpha,
                           const sceGsPrim &prim, bool texa_aem, u_char texa_ta0,
                           const gfx::LogicalRect *scissor);
} // namespace mgport

bool PortMgTextureBinding(const mgCTexture *texture, gfx::TextureBinding &out);

namespace {

// 12.4 fixed point, rounding to nearest, as the VU's vftoi4 made of a pixel coordinate.
int Fixed4(float value) {
    return static_cast<int>(std::lroundf(value * 16.0f));
}

// The GS registers that outlive one packet, in the renderer's terms. Begin's packet head copies
// the prim's draw environment in; Direct writes land on top; every draw afterwards reads them.
struct GsRegisters {
    bool       env_seen = false;
    sceGsTest  test{};
    sceGsZbuf  zbuf{};
    sceGsAlpha alpha{};

    bool   texa_seen = false;
    u_char texa_ta0 = 0x80;
    bool   texa_aem = true;

    bool             scissor_seen = false;
    gfx::LogicalRect scissor{0.0f, 0.0f, gfx::LogicalWidth(), gfx::LogicalHeight()};

    // The image-transfer registers the last Direct writes assembled; TRXDIR runs the transfer.
    u_long transfer_bitbltbuf = 0;
    u_long transfer_trxpos = 0;
    u_long transfer_trxreg = 0;

    bool                texture_resolved = false;
    gfx::TextureBinding binding{};
    float               u_scale = 1.0f;
    float               v_scale = 1.0f;
};

GsRegisters g_regs;

// One builder's open run and the vertex registers, which the GS keeps between runs.
struct PrimState {
    int                 type = MG_PRIM_SPRITE;
    bool                open = false;
    bool                packed = false;
    int                 roles[16] = {};
    int                 role_count = 0;
    int                 role_index = 0;
    std::vector<gfx::Vertex2D> vertices;
    // Packed-mode words whose values are not yet known when they are queued: Data's quad is
    // recorded as it arrives, and DirectData hands the caller memory it only fills afterwards.
    // Both are fed through the role cycle when the run closes, in the order the caller wrote.
    std::vector<u_long128> raw_quads;
    std::vector<int>       raw_roles;
    bool                sprite_have_first = false;
    gfx::Vertex2D       sprite_first{};
    float               u = 0.0f;
    float               v = 0.0f;
    u_char              color[4] = {0, 0, 0, 0};
};

std::unordered_map<const mgCDrawPrim *, PrimState> &States() {
    static std::unordered_map<const mgCDrawPrim *, PrimState> states;
    return states;
}

PrimState &State(const mgCDrawPrim *prim) {
    return States()[prim];
}

gfx::DrawState CurrentState(const mgCDrawPrim &owner) {
    sceGsTest  test;
    sceGsZbuf  zbuf;
    sceGsAlpha alpha;

    if (g_regs.env_seen) {
        test = g_regs.test;
        zbuf = g_regs.zbuf;
        alpha = g_regs.alpha;
    } else {
        const mgCDrawEnv &env = mgRenderInfo.draw_env[0];
        test = env.test;
        zbuf = env.zbuf;
        alpha = env.alpha;
    }

    // ZMask is a member of the builder, applied to the depth buffer every time a packet opens.
    zbuf.bits.zmsk = owner.z_mask == MG_Z_MASK_WRITE ? 0 : 1;

    const bool   aem = g_regs.texa_seen ? g_regs.texa_aem : mgTEXA_1.AEM != 0;
    const u_char ta0 = g_regs.texa_seen ? g_regs.texa_ta0 : static_cast<u_char>(mgTEXA_1.TA0);

    return mgport::DrawStateOf(test, zbuf, alpha, owner.prim, aem, ta0,
                               g_regs.scissor_seen ? &g_regs.scissor : nullptr);
}

void AddVertex(PrimState &state, int x, int y, int z, float u, float v, const u_char color[4],
               u_char fog) {
    gfx::Vertex2D vertex = {};
    vertex.x = mgport::LogicalX(x);
    vertex.y = mgport::LogicalY(y);
    vertex.z = mgport::Depth(static_cast<unsigned>(z) & 0xFFFFFFu);
    vertex.u = u;
    vertex.v = v;
    vertex.color[0] = color[0];
    vertex.color[1] = color[1];
    vertex.color[2] = color[2];
    vertex.color[3] = color[3];
    vertex.fog = fog;

    if (state.type != MG_PRIM_SPRITE) {
        state.vertices.push_back(vertex);
        return;
    }

    if (!state.sprite_have_first) {
        state.sprite_first = vertex;
        state.sprite_have_first = true;
        return;
    }

    // A GS sprite is two opposite corners; the renderer's quads want all four. The missing
    // corners take each texture coordinate from the corner that owns its axis, so a texture maps
    // axis-aligned as it did on the GS. Colours follow the horizontal edge.
    const gfx::Vertex2D a = state.sprite_first;
    const gfx::Vertex2D b = vertex;

    gfx::Vertex2D top_left = a;
    gfx::Vertex2D top_right = b;
    top_right.y = a.y;
    top_right.v = a.v;
    gfx::Vertex2D bottom_right = b;
    gfx::Vertex2D bottom_left = a;
    bottom_left.y = b.y;
    bottom_left.v = b.v;

    state.vertices.push_back(top_left);
    state.vertices.push_back(top_right);
    state.vertices.push_back(bottom_right);
    state.vertices.push_back(bottom_left);
    state.sprite_have_first = false;
}

void OpenRun(PrimState &state, int type, bool packed, int unit_count, u_int data_a, u_int data_b) {
    state.type = type;
    state.packed = packed;
    state.open = true;
    state.role_index = 0;
    state.role_count = 0;
    state.vertices.clear();
    state.raw_quads.clear();
    state.raw_roles.clear();
    state.sprite_have_first = false;

    if (!packed) {
        return;
    }

    const uint64_t regs = static_cast<uint64_t>(data_a) | (static_cast<uint64_t>(data_b) << 32);
    state.role_count = unit_count < 16 ? unit_count : 16;

    for (int i = 0; i < state.role_count; i++) {
        state.roles[i] = static_cast<int>((regs >> (4 * i)) & 0xF);
    }
}

// The packed-mode layouts: each field sits in the low bits of its own 32-bit word (this is
// exactly what the game's vectors hold, so Data/Data0/Data4 read straight out of them).
void FeedRole(PrimState &state, int role, const int *words) {
    switch (role) {
        case SCE_GS_RGBAQ:
            state.color[0] = static_cast<u_char>(words[0] & 0xFF);
            state.color[1] = static_cast<u_char>(words[1] & 0xFF);
            state.color[2] = static_cast<u_char>(words[2] & 0xFF);
            state.color[3] = static_cast<u_char>(words[3] & 0xFF);
            break;
        case SCE_GS_UV:
            state.u = static_cast<float>(words[0] & 0x3FFF) / 16.0f;
            state.v = static_cast<float>(words[1] & 0x3FFF) / 16.0f;
            break;
        case SCE_GS_XYZF2: {
            const int      x = words[0] & 0xFFFF;
            const int      y = words[1] & 0xFFFF;
            const int      z = (words[2] >> 4) & 0xFFFFFF;
            const u_char   fog = static_cast<u_char>((words[3] >> 4) & 0xFF);
            AddVertex(state, x, y, z, state.u, state.v, state.color, fog);
            break;
        }
        case SCE_GS_XYZ2: {
            const int x = words[0] & 0xFFFF;
            const int y = words[1] & 0xFFFF;
            const int z = words[2] & 0xFFFFFF;
            AddVertex(state, x, y, z, state.u, state.v, state.color, 0xFF);
            break;
        }
        default:
            PS2_UNIMPLEMENTED();
    }
}

// Records one packed-mode quad whose register role the run's cycle names.
void EnqueueRaw(PrimState &state, int role, const int *words) {
    u_long128 quad = 0;
    std::memcpy(&quad, words, sizeof(quad));
    state.raw_quads.push_back(quad);
    state.raw_roles.push_back(role);
}

// Feeds the run's queued quads through its register cycle; the run closes only after the caller
// has written the memory DirectData handed out.
void FlushRawStream(PrimState &state) {
    for (size_t index = 0; index < state.raw_quads.size(); index++) {
        int words[4];
        std::memcpy(words, &state.raw_quads[index], sizeof(words));
        FeedRole(state, state.raw_roles[index], words);
    }

    state.raw_quads.clear();
    state.raw_roles.clear();
}

// The manager's texture placed at a TBP0, or NULL when none is. The engine's other units still
// speak in these GS placements, which is what a transfer register carries.
mgCTexture *FindVramTexture(mgCTextureManager *manager, u_int tbp) {
    if (manager == NULL || manager->blocks == NULL) {
        return NULL;
    }

    for (int block = 0; block < manager->block_max; block++) {
        for (mgCTexture *texture = manager->blocks[block].texture; texture != NULL;
             texture = texture->next) {
            if ((texture->tex0.value & 0x3FFF) == tbp) {
                return texture;
            }
        }
    }

    for (mgCTexture *texture = manager->fix_block.texture; texture != NULL; texture = texture->next) {
        if ((texture->tex0.value & 0x3FFF) == tbp) {
            return texture;
        }
    }

    return NULL;
}

// The GS's VRAM-to-VRAM image copy, the only transfer direction the game asks for (the item icon
// moved between rectangles of one texture in menudraw.cpp): the registers decode to one
// gfx::CopyTexture over the two textures TBP0 names.
void StartImageTransfer(u_int direction) {
    switch (direction & 0x3) {
        case 3:
            // Deactivates the transfer.
            return;
        case SCE_GS_LOCAL_LOCAL:
            break;
        default:
            // The host-to-local and local-to-host directions moved pixel data through the packet
            // stream, which the port replaced with the texture manager's own decode.
            PS2_UNIMPLEMENTED();
    }

    mgCTextureManager *manager =
        mgDrawManager.texture_manager != NULL ? mgDrawManager.texture_manager : &mgTexManager;
    const u_int source_base = static_cast<u_int>(g_regs.transfer_bitbltbuf & 0x3FFF);
    const u_int dest_base = static_cast<u_int>((g_regs.transfer_bitbltbuf >> 32) & 0x3FFF);
    mgCTexture *source = FindVramTexture(manager, source_base);
    mgCTexture *dest = FindVramTexture(manager, dest_base);

    if (source == NULL || dest == NULL) {
        PS2_UNIMPLEMENTED();
    }

    gfx::TextureBinding source_binding;
    gfx::TextureBinding dest_binding;

    if (!PortMgTextureBinding(source, source_binding) || !PortMgTextureBinding(dest, dest_binding)) {
        PS2_UNIMPLEMENTED();
    }

    const int source_x = static_cast<int>(g_regs.transfer_trxpos & 0x7FF);
    const int source_y = static_cast<int>((g_regs.transfer_trxpos >> 16) & 0x7FF);
    const int dest_x = static_cast<int>((g_regs.transfer_trxpos >> 32) & 0x7FF);
    const int dest_y = static_cast<int>((g_regs.transfer_trxpos >> 48) & 0x7FF);
    const int width = static_cast<int>(g_regs.transfer_trxreg & 0xFFF);
    const int height = static_cast<int>((g_regs.transfer_trxreg >> 32) & 0xFFF);

    if (!gfx::CopyTexture(source_binding.texture, {source_x, source_y, width, height},
                          dest_binding.texture, dest_x, dest_y)) {
        PS2_UNIMPLEMENTED();
    }
}

void SubmitRun(PrimState &state, const mgCDrawPrim &owner) {
    state.open = false;
    state.sprite_have_first = false;
    FlushRawStream(state);

    if (state.vertices.empty()) {
        return;
    }

    gfx::Primitive primitive;

    switch (state.type) {
        case MG_PRIM_LINE:
            primitive = gfx::Primitive::Lines;
            break;
        case MG_PRIM_LINE_STRIP:
            primitive = gfx::Primitive::LineStrip;
            break;
        case MG_PRIM_TRIANGLE:
            primitive = gfx::Primitive::Triangles;
            break;
        case MG_PRIM_TRIANGLE_STRIP:
            primitive = gfx::Primitive::TriangleStrip;
            break;
        case MG_PRIM_TRIANGLE_FAN:
            primitive = gfx::Primitive::TriangleFan;
            break;
        case MG_PRIM_SPRITE:
            primitive = gfx::Primitive::Quads;
            break;
        case MG_PRIM_POINT:
            // Points only ever carry register writes (a scissor, an image transfer).
            PS2_UNIMPLEMENTED();
        default:
            PS2_UNIMPLEMENTED();
    }

    gfx::TextureBinding binding;

    if (owner.prim.TME != 0) {
        if (!g_regs.texture_resolved) {
            // Nothing to sample with; drop the run as the 2D units drop their sprites.
            state.vertices.clear();
            return;
        }

        binding = g_regs.binding;

        for (gfx::Vertex2D &vertex : state.vertices) {
            vertex.u *= g_regs.u_scale;
            vertex.v *= g_regs.v_scale;
        }
    }

    gfx::Draw2D(primitive, state.vertices, binding, CurrentState(owner));
    state.vertices.clear();
}

} // namespace

PORT_OVERRIDE mgCDrawPrim::mgCDrawPrim() {
    memory = NULL;
    vif_packet = NULL;
    draw_manager = NULL;
    disabled = 1;
    q = 1.0f;
    *(u_long *) &prim = MG_GS_PRIM_FST;
    bilinear = 1;
    z_mask = MG_Z_MASK_WRITE;
    coord = 0;
    offset_x = 0;
    offset_y = 0;
}

PORT_OVERRIDE void mgCDrawPrim::Initialize(mgCMemory *memory, sceVif1Packet *vif_packet) {
    if (vif_packet == NULL) {
        vif_packet = mgVif1Packet;
    }

    if (memory == NULL) {
        memory = mgGetDataBuffer();
    }

    this->memory = memory;
    this->vif_packet = vif_packet;
    detached = 0;

    if (draw_manager == NULL) {
        draw_manager = &mgDrawManager;
    }

    disabled = 1;
    texture.Initialize();
    draw_env.Initialize(0);

    PrimState &state = State(this);
    state = PrimState();
}

PORT_OVERRIDE void mgCDrawPrim::Begin(int prim_type) {
    this->prim.PRIM = prim_type;
    this->q = 1.0f;
    this->disabled = 0;
    Begin2();
    BeginDma();
}

PORT_OVERRIDE void mgCDrawPrim::BeginDma() {
    PrimState &state = State(this);
    OpenRun(state, this->prim.PRIM, false, 0, 0, 0);
}

PORT_OVERRIDE void mgCDrawPrim::EndDma() {
    PrimState &state = State(this);

    if (state.open) {
        SubmitRun(state, *this);
    }
}

PORT_OVERRIDE void mgCDrawPrim::Flush() {
    EndDma();
    BeginDma();
}

PORT_OVERRIDE void mgCDrawPrim::End() {
    EndDma();
    End2();
}

PORT_OVERRIDE void mgCDrawPrim::Begin2() {
    this->disabled = 0;

    // The packet head sends the draw environment; on the GS those registers then hold until the
    // next packet, and Direct writes land on top of them.
    g_regs.test = draw_env.test;
    g_regs.zbuf = draw_env.zbuf;
    g_regs.alpha = draw_env.alpha;
    g_regs.env_seen = true;
}

PORT_OVERRIDE void mgCDrawPrim::BeginPrim2(int type) {
    this->prim.PRIM = type;
    this->q = 1.0f;
    PrimState &state = State(this);
    OpenRun(state, type, false, 0, 0, 0);
}

PORT_OVERRIDE void mgCDrawPrim::BeginPrim2(int prim_type, u_int data_a, u_int data_b, int unit_count) {
    this->prim.PRIM = prim_type;
    this->q = 1.0f;
    PrimState &state = State(this);
    OpenRun(state, prim_type, true, unit_count, data_a, data_b);
}

PORT_OVERRIDE void mgCDrawPrim::EndPrim2() {
    PrimState &state = State(this);

    if (state.open) {
        SubmitRun(state, *this);
    }
}

PORT_OVERRIDE void mgCDrawPrim::End2() {
    PrimState &state = State(this);

    if (state.open) {
        SubmitRun(state, *this);
    }
}

void mgCDrawPrim::Data0(float *data) {
    int words[4] = {static_cast<int>(data[0]), static_cast<int>(data[1]), static_cast<int>(data[2]),
                    static_cast<int>(data[3])};
    PrimState &state = State(this);
    FeedRole(state, SCE_GS_RGBAQ, words);
}

void mgCDrawPrim::Data4(float *data) {
    // Retail converts all four with vftoi4; the packed XYZF2 layout shifts Z and F back down.
    int words[4] = {Fixed4(data[0]), Fixed4(data[1]), Fixed4(data[2]), Fixed4(data[3])};
    PrimState &state = State(this);
    FeedRole(state, SCE_GS_XYZF2, words);
}

PORT_OVERRIDE void mgCDrawPrim::Data(int *data) {
    PrimState &state = State(this);

    if (!state.open || !state.packed || state.role_count == 0) {
        PS2_UNIMPLEMENTED();
    }

    EnqueueRaw(state, state.roles[state.role_index], data);
    state.role_index = (state.role_index + 1) % state.role_count;
}

PORT_OVERRIDE u_long128 *mgCDrawPrim::DirectData(int count) {
    PrimState &state = State(this);

    if (!state.open || !state.packed || state.role_count == 0 || count <= 0) {
        // Raw quads only make sense inside a packed run, whose register cycle names what each one
        // means; nothing outside outline.cpp's chunked sprites hands out packet memory.
        PS2_UNIMPLEMENTED();
    }

    // The caller writes the quads here and the run feeds them through the role cycle when it
    // closes (FlushRawStream); the roles reserved are the ones the next quads would have taken.
    const size_t base = state.raw_quads.size();
    state.raw_quads.resize(base + static_cast<size_t>(count));

    for (int i = 0; i < count; i++) {
        state.raw_roles.push_back(state.roles[state.role_index]);
        state.role_index = (state.role_index + 1) % state.role_count;
    }

    return state.raw_quads.data() + base;
}

PORT_OVERRIDE void mgCDrawPrim::Vertex(int x, int y, int z) {
    Vertex4(x << 4, y << 4, z);
}

PORT_OVERRIDE void mgCDrawPrim::Vertex(float x, float y, float z) {
    float pos[4] = {x, y, z, 0.0f};
    Vertex(pos);
}

void mgCDrawPrim::Vertex(float *pos) {
    Vertex4(Fixed4(pos[0]), Fixed4(pos[1]), static_cast<int>(pos[2]));
}

PORT_OVERRIDE void mgCDrawPrim::Vertex4(int x, int y, int z) {
    PrimState &state = State(this);

    if (!state.open) {
        return;
    }

    int offset_x = 0;
    int offset_y = 0;
    GetOffset(&offset_x, &offset_y);
    AddVertex(state, x + offset_x, y + offset_y, z, state.u, state.v, state.color, 0xFF);
}

PORT_OVERRIDE void mgCDrawPrim::Vertex4(int *pos) {
    Vertex4(pos[0], pos[1], pos[2]);
}

PORT_OVERRIDE void mgCDrawPrim::Color(int r, int g, int b, int a) {
    PrimState &state = State(this);
    state.color[0] = static_cast<u_char>(r);
    state.color[1] = static_cast<u_char>(g);
    state.color[2] = static_cast<u_char>(b);
    state.color[3] = static_cast<u_char>(a);
}

void mgCDrawPrim::Color(float *color) {
    Color(static_cast<int>(color[0]), static_cast<int>(color[1]), static_cast<int>(color[2]),
          static_cast<int>(color[3]));
}

PORT_OVERRIDE void mgCDrawPrim::TextureCrd4(int u, int v) {
    PrimState &state = State(this);
    // 12.4 texel coordinates, signed 16-bit fields on the GS.
    state.u = static_cast<float>(static_cast<s16>(u)) / 16.0f;
    state.v = static_cast<float>(static_cast<s16>(v)) / 16.0f;
}

PORT_OVERRIDE void mgCDrawPrim::TextureCrd(int u, int v) {
    TextureCrd4(u << 4, v << 4);
}

PORT_OVERRIDE void mgCDrawPrim::Direct(u_long reg, u_long data) {
    switch (reg) {
        case SCE_GS_TEXFLUSH:
            // The GS's sampler had a cache; the renderer samples the live image.
            break;
        case SCE_GS_TEXA: {
            sceGsTexa texa;
            *(u_long *) &texa = data;
            g_regs.texa_seen = true;
            g_regs.texa_ta0 = static_cast<u_char>(texa.TA0);
            g_regs.texa_aem = texa.AEM != 0;
            break;
        }
        case SCE_GS_TEST_1:
        case SCE_GS_TEST_2:
            g_regs.test.value = data;
            g_regs.env_seen = true;
            break;
        case SCE_GS_ZBUF_1:
        case SCE_GS_ZBUF_2:
            g_regs.zbuf.value = data;
            g_regs.env_seen = true;
            break;
        case SCE_GS_ALPHA_1:
        case SCE_GS_ALPHA_2:
            g_regs.alpha.value = data;
            g_regs.env_seen = true;
            break;
        case SCE_GS_SCISSOR_1:
            g_regs.scissor = mgport::ScissorRect(data);
            g_regs.scissor_seen = true;
            break;
        case SCE_GS_BITBLTBUF:
            g_regs.transfer_bitbltbuf = data;
            break;
        case SCE_GS_TRXPOS:
            g_regs.transfer_trxpos = data;
            break;
        case SCE_GS_TRXREG:
            g_regs.transfer_trxreg = data;
            break;
        case SCE_GS_TRXDIR:
            StartImageTransfer(static_cast<u_int>(data));
            break;
        default:
            PS2_UNIMPLEMENTED();
    }
}

PORT_OVERRIDE void mgCDrawPrim::Texture(mgCTexture *source) {
    if (source != 0) {
        // A plain data copy, as retail made of the texture fields: sceGsTex0's assignment is a
        // link-time function in the texture unit, the copy in the packet never called it.
        memcpy(&this->texture, source, sizeof(mgCTexture));
        this->texture.Bilinear(bilinear);

        g_regs.u_scale = 1.0f;
        g_regs.v_scale = 1.0f;
        // The manager's registry is keyed by the texture the game hands around, so the binding
        // resolves from the source; the copy above stays the retail-shaped record.
        g_regs.texture_resolved = PortMgTextureBinding(source, g_regs.binding);
        // Bilinear() lands on the copy's TEX1, never the source's, and the packet sent that copy;
        // the renderer's filter follows the same MMAG bit the resolver reads, so the override
        // still chooses it.
        g_regs.binding.filter =
            (this->texture.tex1_bits >> 5) & 1 ? gfx::Filter::Linear : gfx::Filter::Nearest;
    }
}

PORT_OVERRIDE void mgCDrawPrim::AlphaBlendEnable(int enable) {
    prim.ABE = enable;
}

PORT_OVERRIDE void mgCDrawPrim::AlphaBlend(int mode) {
    draw_env.SetAlpha(mode);
}

PORT_OVERRIDE void mgCDrawPrim::AlphaTestEnable(int enable) {
    draw_env.test.bits.ate = enable;
}

PORT_OVERRIDE void mgCDrawPrim::AlphaTest(int method, int ref) {
    draw_env.test.bits.atst = method;
    draw_env.test.bits.aref = ref;
}

PORT_OVERRIDE void mgCDrawPrim::DAlphaTest(int enable, int mode) {
    draw_env.test.bits.date = enable;
    draw_env.test.bits.datm = mode;
}

PORT_OVERRIDE void mgCDrawPrim::DepthTestEnable(int enable) {
    sceGsTest *test = &draw_env.test;

    if (enable == 0) {
        test->bits.zte = 1;
        test->bits.ztst = 1;
    } else {
        DepthTest(MG_DEPTH_TEST_GEQUAL);
    }
}

PORT_OVERRIDE void mgCDrawPrim::DepthTest(int mode) {
    sceGsTest *test = &draw_env.test;
    test->bits.zte = 1;

    switch (mode) {
        case -1:
            test->bits.ztst = 1;
            break;
        case 1:
            test->bits.ztst = 2;
            break;
        case 2:
            test->bits.ztst = 3;
            break;
    }
}

PORT_OVERRIDE void mgCDrawPrim::ZMask(int mask) {
    z_mask = mask;
}

PORT_OVERRIDE void mgCDrawPrim::TextureMapEnable(int enable) {
    prim.TME = enable;
}

PORT_OVERRIDE void mgCDrawPrim::Bilinear(int enable) {
    bilinear = enable;
}

PORT_OVERRIDE void mgCDrawPrim::Shading(int enable) {
    prim.IIP = enable;
}

PORT_OVERRIDE void mgCDrawPrim::AntiAliasing(int enable) {
    prim.AA1 = enable;
}

PORT_OVERRIDE void mgCDrawPrim::FogEnable(int enable) {
    prim.FGE = enable;
}

PORT_OVERRIDE void mgCDrawPrim::Coord(int coord) {
    this->coord = coord;
}

PORT_OVERRIDE void mgCDrawPrim::GetOffset(int *x, int *y) {
    *y = 0;
    *x = 0;

    if (coord == 0) {
        *x = mgScreenOffx << 4;
        *y = mgScreenOffy << 4;
    }

    *x += offset_x;
    *y += offset_y;
}

PORT_OVERRIDE mgCDrawManager::mgCDrawManager() {
    unk_68 = 0x40;
    unk_6c = 0;
    unk_70 = 0;
}

PORT_OVERRIDE void mgCDrawManager::SetSortTable(int num) {
    float near_dist = 1.0f;
    float far_dist = 2.0f;

    if (render_info != NULL) {
        near_dist = render_info->clip_min[2];
        far_dist = render_info->clip_max[2];
    }

    sort_num = num;
    sort_max = sort_num - 1;
    sort_num_f = (float) num;
    near_clip = near_dist;
    far_clip = far_dist;
    clip_range = far_dist - near_clip;
    sort_near = near_clip;
    sort_ratio = near_clip / far_clip;
    sort_scale = sort_num_f;
}

PORT_OVERRIDE void mgCDrawManager::BeginDraw(mgCMemory *new_memory, int *id_list) {
    memory = new_memory;

    if (memory == 0) {
        memory = packet_memory;
    }

    group_max = texture_manager != NULL ? texture_manager->block_max : 0;
    group_num = group_max;
    order_index = 0;
    draw_order = 0;

    if (id_list != 0) {
        int used_count = 0;

        while (id_list[used_count] >= 0) {
            used_count++;
        }

        group_num = used_count;
    }
}

PORT_OVERRIDE void mgCDrawManager::ClearTable() {}

PORT_OVERRIDE void mgCDrawManager::PreEndDraw() {}

PORT_OVERRIDE int mgCDrawManager::ReloadTexture(int group, sceVif1Packet *vif_packet) {
    // The renderer's textures are the game's already; only the group's existence matters to the
    // callers, which use the result to decide whether a block is drawn this pass.
    (void) vif_packet;

    if (texture_manager == NULL || group < 0 || group >= texture_manager->block_max) {
        return 0;
    }

    return 1;
}

PORT_OVERRIDE int mgCDrawManager::Draw(int group, sceVif1Packet *vif_packet) {
    (void) vif_packet;

    if (texture_manager == NULL || group < 0 || group >= texture_manager->block_max) {
        return 0;
    }

    return 1;
}

PORT_OVERRIDE void mgCDrawManager::EndDraw(sceVif1Packet *vif_packet) {
    (void) vif_packet;
}

PORT_OVERRIDE void mgCDrawManager::AddPacket(int group, u_long128 *common, u_long128 *packet, int vu_program) {
    // Retail registered the packet in its texture group's sort list and the frame's DMA chain
    // called it on the GS. A VU1 program has no host executor, and the units that built these
    // packets (the visual ones) draw through the renderer directly now, so registration keeps the
    // only decision retail's callers could observe: a packet for a group this pass does not draw
    // is dropped, one for a drawn group is accepted with nothing left to run.
    (void) common;
    (void) packet;
    (void) vu_program;

    if (group >= group_max) {
        return;
    }

    if (order_index != 0) {
        const int index = group < 0 ? order_index[*draw_order] : order_index[group];

        if (index < 0) {
            return;
        }
    }
}
