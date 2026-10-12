#include <libpkt.h>

sceVif1Packet g_null_packet{};

// The host renderer builds its own draw commands. The packet a ps2/src path still writes to is the
// null packet mgBeginPacket parks the builder on; those writes have no consumer, so they pass
// quietly. A call on any other packet is a port gap and says so as before.
static bool NullPacket(const sceVif1Packet *packet) {
    return packet == &g_null_packet;
}

static bool NullPacket(const sceGifPacket *packet) {
    return (const void *) packet == (const void *) &g_null_packet;
}

// The VIF1 and GIF packet constructors wrote DMA tags and registers the host renderer builds for
// itself; the ps2/src units that still reach them are replaced one by one, so hitting one of
// these stubs is a port gap and says so.

void sceVif1PkInit(sceVif1Packet *packet, u_int *base) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkReset(sceVif1Packet *packet) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

u_int *sceVif1PkTerminate(sceVif1Packet *packet) {
    if (NullPacket(packet)) {
        return NULL;
    }

    PS2_UNIMPLEMENTED();
}

u_int *sceVif1PkEnd(sceVif1Packet *packet, u_int next) {
    if (NullPacket(packet)) {
        return NULL;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkReserve(sceVif1Packet *packet, int count) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkCnt(sceVif1Packet *packet, int irq) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkOpenDirectCode(sceVif1Packet *packet, int irq) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkCloseDirectCode(sceVif1Packet *packet) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkOpenGifTag(sceVif1Packet *packet, u_long128 giftag) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkCloseGifTag(sceVif1Packet *packet) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkAddGsAD(sceVif1Packet *packet, int reg, u_long data) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkRefLoadImage(sceVif1Packet *packet, u_short dbp, u_char dpsm, u_short dbw, u_long128 *src, u_int qwc, u_int dsax, u_int dsay, u_int rrw, u_int rrh) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceVif1PkCall(sceVif1Packet *packet, u_long128 *address, u_int count) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceGifPkInit(sceGifPacket *packet, u_long128 *base) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceGifPkReset(sceGifPacket *packet) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

u_int *sceGifPkTerminate(sceGifPacket *packet) {
    if (NullPacket(packet)) {
        return NULL;
    }

    PS2_UNIMPLEMENTED();
}

u_int *sceGifPkEnd(sceGifPacket *packet, int pce, int irq, int pad) {
    if (NullPacket(packet)) {
        return NULL;
    }

    PS2_UNIMPLEMENTED();
}

void sceGifPkCnt(sceGifPacket *packet, int pce, int irq, int pad) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceGifPkOpenGifTag(sceGifPacket *packet, u_long128 giftag) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceGifPkCloseGifTag(sceGifPacket *packet) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceGifPkAddGsAD(sceGifPacket *packet, int reg, u_long data) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}

void sceGifPkRefLoadImage(sceGifPacket *packet, u_short dbp, u_char dpsm, u_short dbw, u_long128 *src, u_int qwc, u_int dsax, u_int dsay, u_int rrw, u_int rrh) {
    if (NullPacket(packet)) {
        return;
    }

    PS2_UNIMPLEMENTED();
}
