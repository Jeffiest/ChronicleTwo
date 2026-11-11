// The page's persistent storage (docs/WEB.md): the browser's origin private file system mounted at /opfs.
// The page copies the player's data.zip to chronicletwo/data.zip there once and starts the game with
// --run-dir /opfs/chronicletwo, so the archive is read in place, a seek at a
// time, and saves outlive the tab. Node, where the smoke run goes, has no OPFS and keeps WasmFS's memory.

#include <emscripten/em_asm.h>
#include <emscripten/wasmfs.h>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

void WebMountStorage() {
    const bool available = EM_ASM_INT({
        return typeof navigator !== 'undefined' && !!navigator.storage && !!navigator.storage.getDirectory;
    });
    if (!available) {
        return;
    }
    // The OPFS backend works only off the browser's main thread; main runs on a pthread (PROXY_TO_PTHREAD).
    backend_t opfs = wasmfs_create_opfs_backend();
    if (wasmfs_create_directory("/opfs", 0777, opfs) != 0) {
        std::fprintf(stderr, "cannot mount the browser's storage at /opfs\n");
    }
}

// The game data the page's server offers (tools/web/serve.py --data), when the page asks for it with
// --env CT_WEB_SERVER_DATA=<the address of gamedata/>: data.zip and data.json appear at /server, read from the
// server a range at a time as the game reads them (src/fetch_cache.js keeps the recent ones), and the page starts
// the game with --data /server/data. Nothing is copied first, so a phone on the network plays at once.
void WebMountServerData() {
    const char *base = std::getenv("CT_WEB_SERVER_DATA");
    if (base == nullptr || *base == '\0') {
        return;
    }
    backend_t memory = wasmfs_create_memory_backend();
    if (wasmfs_create_directory("/server", 0777, memory) != 0 || wasmfs_create_directory("/server/data", 0777, memory) != 0) {
        std::fprintf(stderr, "cannot make /server for the server's game data\n");
        return;
    }
    std::string address(base);
    if (!address.ends_with('/')) {
        address += '/';
    }
    for (const char *name : {"data.zip", "data.json"}) {
        // Kept in 64 KiB chunks (fetch_cache.js): most of what the game reads at a time is a small file, and a
        // chunk of 1 MiB around each made the way to the title screen 80 MiB of network.
        backend_t   fetch = wasmfs_create_fetch_backend((address + name).c_str(), 64u << 10);
        const int   file = wasmfs_create_file((std::string("/server/") + name).c_str(), 0444, fetch);
        if (file < 0) {
            std::fprintf(stderr, "cannot make /server/%s for the server's game data\n", name);
        } else {
            close(file);
        }
    }
    std::printf("data: reading the game data from %s\n", address.c_str());
}
