#include "common.h"

#include <eekernel.h>

#include "main.hpp"
#include "mglib.hpp"

// The retail microcode lives only in the retail ELF's .vutext; the port replaces these programs
// (docs/vu1), and these placeholders exist only so the addresses the game stores resolve. Any
// upload aborts in the DMA stubs.
u_long128 My_dma_start0[1] = {};
u_long128 Vu_progmain[1]    = {};
u_long128 Vu_prog0[1]       = {};
u_long128 Vu_prog_sdw[1]    = {};
u_long128 Vu_prog_3dsp[1]   = {};
u_long128 Vu_prog_wtr[1]    = {};

// Retail's linker script value; the port only keeps the symbol resolvable for the gp fields it fills.
void *_gp = NULL;
