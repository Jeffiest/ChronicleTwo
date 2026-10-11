#include <libgraph.h>

#include "platform/clock.hpp"

// The frame is progressive, but the field parity still alternates with every tick as the PAL
// interlace did: CGamePad::Init and main spin until sceGsSyncV reports the odd field.
int sceGsSyncV(int mode) {
    return static_cast<int>(ClockSyncV() & 1);
}

// The GS paths are always drained on the host: the port draws each submission as it is made, so
// a sync finds nothing queued and reports success. mgWaitFrame reads the result to tell a full
// VIF1 packet buffer from a wedged one.
int sceGsSyncPath(int mode, u_short timeout) {
    (void) mode;
    (void) timeout;
    return 0;
}

// The store-image read-back moved a rectangle of GS memory into main RAM. The host renderer owns
// its images now, and the units that read the frame buffer or the Z buffer back are replaced, so
// reaching either entry is a port gap and says so.
void sceGsSetDefStoreImage(sceGsStoreImage *si, short sbp, short sbw, short spsm, short ssax,
                           short ssay, short rrw, short rrh) {
    PS2_UNIMPLEMENTED();
}

void sceGsExecStoreImage(sceGsStoreImage *si, u_long128 *dest) {
    PS2_UNIMPLEMENTED();
}
