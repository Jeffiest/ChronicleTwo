#pragma once

#include "mg_memory.hpp"

struct mgMemoryUsage {
    const char *name;
    int         capacity; // Quadwords the arena was given by SetHeapMem or stSetBuffer.
    int         peak;     // Quadwords, see MemoryUsage.
};

// Stack arenas: the largest stack_used reached since the arena was last set up.
// Heap arenas: the largest sum of the sizes of the blocks linked into the heap, each block's header
// included, measured at the moment a block is claimed or grown. The heap's sentinel header is not counted.
mgMemoryUsage MemoryUsage(const mgCMemory &memory);

// Prints one line per arena that has a peak entry. Entries outlive their arenas; the log reads only the copies.
void MemoryLogPeaks();

void MemoryForget(const mgCMemory &memory);
