#include <eekernel.h>

#include <cstdlib>

extern "C" {

// The EE's Exit kills every thread at once; the host exits the process the same way.
void Exit(int status) {
    std::exit(status);
}

void Exit__2(int status) {
    Exit(status);
}

// Host caches are coherent with game data; these SDK operations need no flush.
void FlushCache(int operation) {}
void iFlushCache(int operation) {}
void iSyncDCache(void *start, void *end) {}

// The game puts this in ThreadParam::gpReg; no host thread needs a global pointer register.
void *_gp = nullptr;

} // extern "C"
