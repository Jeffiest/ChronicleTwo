// Under Node (the smoke run, tools/web/smoke.sh) there is no document and no OffscreenCanvas, and the runtime
// refuses to start the game's thread without the canvas it transfers to it (-sOFFSCREENCANVASES_TO_PTHREAD=#canvas,
// pthread_create fails with EINVAL). A stand-in canvas, whose "offscreen canvas" is a message port, which Node's
// workers can transfer, lets the thread start; the game runs --offscreen there and never draws to it. Linked as
// --pre-js (web.cmake), so it runs before the runtime looks at Module.canvas, on the main thread only.
if (typeof process === 'object' && typeof document === 'undefined' && typeof require === 'function') {
    if (require('worker_threads').isMainThread && !Module['canvas']) {
        Module['canvas'] = {
            id: 'canvas',
            width: 640,
            height: 480,
            transferControlToOffscreen() {
                return new MessageChannel().port1;
            },
        };
    }
}
