// mg_drawenv.hpp carries MWCC pragmas clang has no name for; only the declarations are wanted.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#include <mglib.hpp>
#pragma clang diagnostic pop

// The VU1 microprogram images are retail data the decompilation does not carry, and no host code
// executes them: the drawing paths that upload and call them are replaced. The entries only have
// to exist for the microprogram table's pointers, so each holds the extent the retail symbol
// table implies - the image up to the next symbol in ps2/config/pal/main.symbols.txt - and
// nothing else.

u_long128 Vu_prog0[772] = {};

u_long128 Vu_prog_sdw[151] = {};

u_long128 Vu_prog_3dsp[109] = {};
