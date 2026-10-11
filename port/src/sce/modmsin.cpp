#include <modmsin.h>

// The module queued MIDI to the IOP's synthesiser, which the host owns now. Nothing on this side
// reads the queues, so accepting a message and letting it go is all the callers (which only watch
// for failures) need; sceMSIn_Init sets nothing up and succeeds.

int sceMSIn_Init(sceCslCtx *ctx) {
    return 0;
}

int sceMSIn_PutMsg(sceCslCtx *ctx, unsigned int port, unsigned int msg) {
    return 0;
}

int sceMSIn_PutHsMsg(sceCslCtx *ctx, unsigned int port, unsigned char *msg) {
    return 0;
}
