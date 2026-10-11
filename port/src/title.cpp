#include "common.h"

// The water microprogram, the title unit's other hardware user: MainLoop installs it as user
// program 0 (mainloop.cpp:523) and every water surface water.cpp builds - the title screen's
// among them - is drawn by a VU1 packet that runs it. No host code executes VU1, and nothing
// reads the image: it only has to exist for the installed pointer table. The extent is the
// retail symbol table's - Vu_prog_wtr (0x329FD0) to the next top-level symbol, __data_start
// (0x32A320) - the boundary convention vuprog.cpp uses for the other microprogram images.
u_long128 Vu_prog_wtr[53] = {};
