#include <libpkt.h>

// The GIF packet's reserve and reference entries were left out when libpkt.cpp took the rest of
// the open/close family; the DMA tags and register writes they build are the host renderer's
// work now, so like their neighbours they are port gaps that say so when reached.

u_int *sceGifPkReserve(sceGifPacket *packet, int count) {
    PS2_UNIMPLEMENTED();
}

void sceGifPkRef(sceGifPacket *packet, u_long128 *address, int count, int pce, int irq, int pad) {
    PS2_UNIMPLEMENTED();
}
