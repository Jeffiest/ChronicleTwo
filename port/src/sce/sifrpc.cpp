#include <sifrpc.h>

#include <cstdlib>

// There is no IOP: rebooting it, syncing with it and loading its modules
// succeed at once. The RPC and IOP heap calls stay stubs until the audio
// replacement removes their callers.

void sceSifInitRpc(int mode) {}

int sceSifRebootIop(const char *path) {
    return 1;
}

int sceSifSyncIop() {
    return 1;
}

int sceSifLoadModule(const char *path, int arg_len, const char *args) {
    static int next_id;
    return ++next_id;
}

// "IOP memory" is host memory while no IOP exists: the game's own ownership rules (every
// allocation is freed once, with the pointer it was given) let malloc and free stand in for the
// heap service. An RPC has nowhere to run, so a request is reported delivered and a completion
// callback, when one is given, is called at once.

void *sceSifAllocSysMemory(int mode, unsigned int size, void *address) {
    return std::malloc(size != 0 ? size : 1);
}

int sceSifFreeSysMemory(void *address) {
    std::free(address);
    return 0;
}

int sceSifInitIopHeap(void) {
    return 0;
}

int sceSifCallRpc(struct sceSifClientData *client, unsigned int number, unsigned int mode,
                  void *send, int send_size, void *receive, int receive_size,
                  void (*end_callback)(void *), void *end_parameter) {
    if (end_callback != nullptr) {
        end_callback(end_parameter);
    }
    return 0;
}
