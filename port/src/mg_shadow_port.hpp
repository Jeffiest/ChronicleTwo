#pragma once

#include <vector>

#include "mglib_port.hpp"
#include "vu1/sdw_geom.hpp"

// Appends the shadow prism of one caster triangle, or nothing when retail's clip rejects it: a corner
// (top or drop, eye space, before the drop bias) outside the projection's near/far range or the GS guard
// band. bias_dir lifts the drop corners off the receiving plane.
void AppendShadowPrism(const dc::vu1::Vec4 top[3], const dc::vu1::Vec4 drop[3], const dc::vu1::Vec4 &bias_dir,
                       const mgPortProjection &projection, std::vector<dc::vu1::SdwPrismTriangle> &out);
