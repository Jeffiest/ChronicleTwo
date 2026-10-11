#include <cstdint>
#include <cstdio>
#include <cstdlib>

// A crash prints nothing on its own and a killed run loses stderr's buffer. DC_CRASH_TRACE=1 installs
// a filter that writes the faulting instruction and a raw backtrace; each frame carries its
// load-relative offset, which llvm-symbolizer resolves against the executable's DWARF symbols.
//
// This unit is deliberately free of the game headers: Windows' (macros B and R, struct RECT) clash
// with the PS2's, so the handler cannot share a translation unit with them.

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

LONG WINAPI CrashFilter(EXCEPTION_POINTERS *info) {
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    uintptr_t fault = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    std::fprintf(stderr, "crash code %#lx fault %p base %p rva %#zx\n",
                 static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode), reinterpret_cast<void *>(fault),
                 reinterpret_cast<void *>(base), fault - base);

    void  *frames[64];
    USHORT count = RtlCaptureStackBackTrace(1, 64, frames, nullptr);

    for (USHORT i = 0; i < count; i++) {
        uintptr_t at = reinterpret_cast<uintptr_t>(frames[i]);
        std::fprintf(stderr, "  #%u %p rva %#zx\n", static_cast<unsigned>(i), reinterpret_cast<void *>(at), at - base);
    }

    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void InstallCrashTrace() {
    if (std::getenv("DC_CRASH_TRACE") != nullptr) {
        SetUnhandledExceptionFilter(CrashFilter);
    }
}

#else

void InstallCrashTrace() {}

#endif
