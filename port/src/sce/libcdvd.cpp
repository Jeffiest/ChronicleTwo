#include <libcdvd.h>

// The game reads a directory, not a drive: initialising it and choosing the media succeed. The
// sector group is for the calls ps2/src/dataread.cpp still makes until its InitCDFile and ReadBG
// group are replaced: the read reports itself finished at once and the sync and error calls let
// the polling loops end. No sectors are fetched, so a buffer handed to sceCdRead stays as it was.

int sceCdInit(int mode) {
    return 1;
}

int sceCdMmode(int media) {
    return 1;
}

int sceCdRead(u_int lsn, u_int sectors, void *buffer, sceCdRMode *mode) {
    return 1;
}

int sceCdSync(int mode) {
    return 0;
}

int sceCdGetError(void) {
    return 0;
}

int sceCdBreak(void) {
    return 0;
}
