#include <libsdr.h>

// The SPU2 is part of the IOP, which the host replaces with its own synthesiser: connecting
// succeeds and every register and transfer command is accepted without effect. Calls that read a
// position back (the movie player's buffer cursors) get zero, their start of the area answer.

int sceSdRemoteInit(void) {
    return 0;
}

int sceSdRemote(int arg, int command, ...) {
    return 0;
}
