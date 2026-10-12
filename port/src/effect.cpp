#include "mg_texture.hpp"

#include "gfx/gfx.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"

/**
 * The effect units' texture seam.
 *
 * A textured mgCDrawPrim run - the scripted particles, the sword trail and every menu and map
 * primitive drawn through the same builder - resolves its texture through mgport::ResolveTexture.
 * Resolve through the renderer seam so texture-manager images and named frame targets share the
 * same lookup path.
 */

namespace mgport {

bool ResolveTexture(const mgCTexture &texture, gfx::TextureBinding &binding, float &u_scale,
                    float &v_scale) {
    binding = mgPortBinding(&texture);
    if (binding.texture == gfx::kNullTexture) {
        return false;
    }

    u_scale = 1.0f;
    v_scale = 1.0f;

    // An image of the frame is the game's screen of pixels stretched over the renderer's 640
    // logical columns, so a texel coordinate in its space scales with them; the texture manager
    // names such images with the frame handles.
    if (binding.texture == gfx::kMainTarget || binding.texture == gfx::kPreviousFrame) {
        u_scale = gfx::LogicalWidth() / static_cast<float>(mgScreenWidth);
    }

    return true;
}

} // namespace mgport
