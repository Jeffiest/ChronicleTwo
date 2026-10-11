// The game headers keep MWCC's pragmas, which clang names but ignores.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#include "dng_main.hpp"

#include "main.hpp"
#pragma clang diagnostic pop

// The dungeon's work buffer and event camera are dng_main's own objects; the retail symbol
// table knows each by its __2 name (the decompilation's dng_main notes list BuffWorkData and
// EventCamera among them), and that is the spelling the outside readers use: dng_object.hpp
// declares BuffWorkData__2 for the buffer CPullItem::Step carves its collision polygons from,
// dng_main.hpp declares EventCamera__2 for the camera sphida.cpp copies the follow camera
// into. These definitions replace ps2/src/dng_main.cpp's, and the aliases
// make both spellings one object, as they are in retail.
// BuffWorkData, EventCamera and their __2 aliases live in port/src/kernel.cpp;
// Vu_prog_wtr lives in port/src/title.cpp.

// VU1 microprogram that draws water surfaces (main.hpp), which mainloop.cpp loads into the
// user program table. No host code runs it - water.cpp only writes its address into a DMA
// call tag that nothing on the host follows - but the symbol has to exist for that caller.
// The image is not in the decompilation, so it is zeroed like the built-in programs in
// port/src/sce/vuprog.cpp; its extent is the retail block 0x329FD0 up to __data_start in
// ps2/config/pal/main.symbols.txt, the water program's own labels (__INIT_DATA__4,
// __MAIN_PROG__4, __CPwtr) included.
