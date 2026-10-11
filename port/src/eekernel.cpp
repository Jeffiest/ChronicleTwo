#include <eekernel.h>

// The semaphores guarded buffers the IOP's decoders shared with the EE. The host's decoders run
// inside the main loop and there is no second thread to synchronise with, so acquiring a
// semaphore costs nothing and releasing it is free; keeping the order of the calls is all that
// matters.

extern "C" {

int WaitSema(int sema_id) {
    return 0;
}

int SignalSema(int sema_id) {
    return 0;
}

} // extern "C"
