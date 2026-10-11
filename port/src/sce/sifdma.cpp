#include <sifdma.h>

// Nothing runs on the far side of the SIF, so a transfer is accepted and reported finished as
// soon as it is asked about: callers poll sceSifDmaStat until it turns negative, and answering
// that at once leaves sendToIOP's loop without a pass through it.

int sceSifSetDma(struct sceSifDmaData *transfer, int count) {
    return 1;
}

int sceSifDmaStat(int id) {
    return -1;
}
