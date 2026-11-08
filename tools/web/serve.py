#!/usr/bin/env python3
"""Serves a directory (the web build, port/build/web) with the headers the game needs (docs/WEB.md).

    tools/web/serve.py [--port N] [--bind HOST] [directory]

The game's threads need SharedArrayBuffer, which a page has only when it is cross-origin isolated: every response
carries Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp. A host that cannot
send them (GitHub Pages) relies on the page's service worker (port/web/site/sw.js) instead; this server shows the
page as a host that can. .wasm is served as application/wasm so the browser compiles it as it streams.
"""
import argparse
import http.server
import os
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript", ".mjs": "text/javascript", ".data": "application/octet-stream"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, format, *args):
        if os.environ.get("CT_SERVE_QUIET"):
            return
        super().log_message(format, *args)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("directory", nargs="?", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                     "..", "..", "port", "build", "web"))
    options = parser.parse_args()
    os.chdir(options.directory)
    server = http.server.ThreadingHTTPServer((options.bind, options.port), Handler)
    print("serving %s at http://%s:%d/" % (os.getcwd(), options.bind, server.server_address[1]), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
