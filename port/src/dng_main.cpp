// The game headers keep MWCC's pragmas, which clang names but ignores.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#include "dng_main.hpp"

#include "main.hpp"
#pragma clang diagnostic pop

// The restored retail dng_main.cpp defines the work buffer and event camera. Its stub header
// renames those definitions to the __2 symbols consumed by outside readers.
// Vu_prog_wtr lives in port/src/title.cpp.

// VU1 microprogram that draws water surfaces (main.hpp), which mainloop.cpp loads into the
// user program table. No host code runs it - water.cpp only writes its address into a DMA
// call tag that nothing on the host follows - but the symbol has to exist for that caller.
// The image is not in the decompilation, so it is zeroed like the built-in programs in
// port/src/sce/vuprog.cpp; its extent is the retail block 0x329FD0 up to __data_start in
// ps2/config/pal/main.symbols.txt, the water program's own labels (__INIT_DATA__4,
// __MAIN_PROG__4, __CPwtr) included.
