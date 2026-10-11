#include <eekernel.h>
#include <libdma.h>

// The host renderer consumes the game's draw state where the game submits it, so no transfer is
// ever queued on the EE's DMA channels, and with no hardware raising them, no interrupt fires.
// Every entry below belongs to a path the port replaces and says so when it is reached.

sceDmaChan *sceDmaGetChan(int channel) {
    PS2_UNIMPLEMENTED();
}

int sceDmaSend(sceDmaChan *chan, void *data) {
    PS2_UNIMPLEMENTED();
}

int sceDmaSync(sceDmaChan *chan, int mode, int timeout) {
    PS2_UNIMPLEMENTED();
}

int sceDmaReset(int mode) {
    PS2_UNIMPLEMENTED();
}

int sceDmaGetEnv(sceDmaEnv *env) {
    PS2_UNIMPLEMENTED();
}

int sceDmaPutEnv(sceDmaEnv *env) {
    PS2_UNIMPLEMENTED();
}

int DIntr(void) {
    PS2_UNIMPLEMENTED();
}

int EIntr(...) {
    PS2_UNIMPLEMENTED();
}

int AddIntcHandler(int cause, int (*handler)(int), int next) {
    PS2_UNIMPLEMENTED();
}

int RemoveIntcHandler(int cause, int handler) {
    PS2_UNIMPLEMENTED();
}

int EnableIntc(int cause) {
    PS2_UNIMPLEMENTED();
}

int AddDmacHandler(int channel, int (*handler)(int), int next) {
    PS2_UNIMPLEMENTED();
}

int RemoveDmacHandler(int channel, int handler) {
    PS2_UNIMPLEMENTED();
}

int EnableDmac(int channel) {
    PS2_UNIMPLEMENTED();
}

int DisableDmac(int channel) {
    PS2_UNIMPLEMENTED();
}
