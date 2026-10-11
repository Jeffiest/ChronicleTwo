#include "mg_texture.hpp"

#include "gfx/gfx.hpp"
#include "mglib.hpp"

/**
 * The effect units' texture seam.
 *
 * A textured mgCDrawPrim run - the scripted particles, the sword trail and every menu and map
 * primitive drawn through the same builder - resolves its texture through mgport::ResolveTexture.
 * mg_drawenv.cpp leaves that definition weak so it can return false and the run is dropped;
 * mg_drawprim.cpp's call site names this the strong definition mg_texture.cpp would provide, but
 * the texture unit only exposes PortMgTextureBinding, so it lives here with the effect units
 * that first read it.
 */

// mg_texture.cpp's resolver, declared the way the other units declare it (there is no shared
// header for it yet).
bool PortMgTextureBinding(const mgCTexture *texture, gfx::TextureBinding &out);

namespace mgport {

bool ResolveTexture(const mgCTexture &texture, gfx::TextureBinding &binding, float &u_scale,
                    float &v_scale) {
    if (!PortMgTextureBinding(&texture, binding)) {
        return false;
    }

    u_scale = 1.0f;
    v_scale = 1.0f;

    // An image of the frame is the game's screen of pixels stretched over the renderer's 640
    // logical columns, so a texel coordinate in its space scales with them; the texture manager
    // names such images with the frame handles.
    if (binding.texture == gfx::kMainTarget || binding.texture == gfx::kPreviousFrame) {
        u_scale = gfx::kLogicalWidth / static_cast<float>(mgScreenWidth);
    }

    return true;
}

} // namespace mgport
