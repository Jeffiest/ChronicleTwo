#pragma once

#include "mg_drawprim_port.hpp"

// Replaces mgPortDraw2D as the destination of mgC3DSprite's billboards while set; an empty function restores it. For tests.
void mgPortSet3DSpriteSink(mgPortPrimSink sink);
