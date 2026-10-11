#include <sifrpc.h>

#include <cstdint>
#include <cstdlib>

// There is no IOP: rebooting it, syncing with it and loading its modules succeed at once, and the
// services it used to own answer on the host's behalf instead.

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

// System memory, unlike the heap below, is held by pointer, so the game's own ownership rules
// (every allocation is freed once, with the pointer it was given) let malloc and free stand in.
// An RPC has nowhere to run, so a request is reported delivered and a completion callback, when
// one is given, is called at once.

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

// The heap service is the caller that does not hold its blocks by pointer: AudioDec stores the
// addresses in ints and does arithmetic on them, so a host pointer would be truncated on the way
// in and the cast back on free would poison the heap. Nothing on the host reads or writes through
// an IOP address - every SIF, DMA and SPU service answers without effect - so a block only needs
// an address and a size: it gets a 64-byte-aligned token in the IOP RAM window, and a host-side
// list lets free find it again.

namespace {

constexpr unsigned int kIopHeapBase = 0x00100000; // IOP RAM, the service's address space
constexpr unsigned int kIopHeapLimit = 0x00200000;
constexpr unsigned int kIopHeapAlign = 64; // the sifman heap's alignment

struct IopHeapBlock {
    unsigned int  address; // IOP address handed to the game
    unsigned int  size;    // bytes, a multiple of kIopHeapAlign
    IopHeapBlock *next;    // the next block, address order
};

IopHeapBlock *g_iop_heap;

} // namespace

void *sceSifAllocIopHeap(unsigned int size) {
    if (size == 0) {
        size = 1;
    }

    if (size > kIopHeapLimit - kIopHeapBase) {
        return nullptr;
    }

    size = (size + kIopHeapAlign - 1) & ~(kIopHeapAlign - 1);

    // First fit between the blocks already handed out.
    unsigned int   address = kIopHeapBase;
    IopHeapBlock **link = &g_iop_heap;

    while (*link != nullptr && address + size > (*link)->address) {
        address = (*link)->address + (*link)->size;
        link = &(*link)->next;
    }

    if (address + size > kIopHeapLimit) {
        return nullptr;
    }

    IopHeapBlock *block = static_cast<IopHeapBlock *>(std::malloc(sizeof(IopHeapBlock)));

    if (block == nullptr) {
        return nullptr;
    }

    block->address = address;
    block->size = size;
    block->next = *link;
    *link = block;
    return reinterpret_cast<void *>(address);
}

int sceSifFreeIopHeap(void *address) {
    if (address == nullptr) {
        return -1;
    }

    unsigned int   value = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(address));
    IopHeapBlock **link = &g_iop_heap;

    while (*link != nullptr && (*link)->address != value) {
        link = &(*link)->next;
    }

    if (*link == nullptr) {
        return -1;
    }

    IopHeapBlock *block = *link;
    *link = block->next;
    std::free(block);
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
