// _Exit on the web. std::_Exit from the game's pthread waits on the main thread to exit, which keeps the runtime
// alive for that very pthread (PROXY_TO_PTHREAD), so it never returns and the page never hears the status. This
// ends the process as _Exit means to: the page's onExit gets the status and the threads are stopped, with no
// static destructors run.

#include <emscripten/em_asm.h>

#include <chrono>
#include <cstdio>
#include <thread>

[[noreturn]] void WebQuickExit(int status) {
    MAIN_THREAD_ASYNC_EM_ASM({
        EXITSTATUS = $0;
        ABORT = true;
        Module['onExit']?.($0);
        PThread.terminateAllThreads();
    }, status);
    // This thread is among those the main thread stops.
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// exit() for the game's thread. Emscripten's exit() runs the C atexit handlers on the page's main thread
// (preamble.js, exitRuntime), not on the thread that called it, and the host's shutdown (main.cpp: the renderer,
// the window, the log) is atexit handlers that must run on the game's thread, which owns the WebGPU or GL
// objects. So the handlers run here first (musl's atexit list, which the runtime also calls, finding it empty),
// then the process ends as WebQuickExit does. Static destructors do not run, as with _Exit.
extern "C" void __funcs_on_exit();

[[noreturn]] void WebExit(int status) {
    __funcs_on_exit();
    std::fflush(nullptr);
    WebQuickExit(status);
}
