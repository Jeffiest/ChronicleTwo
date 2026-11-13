#!/usr/bin/env python3
"""Serves a directory (the web build, port/build/web) with the headers the game needs (docs/WEB.md).

    tools/web/serve.py [--port N] [--bind HOST] [--data DIR] [--tls DIR [--tls-port N]] [directory]

The game's threads need SharedArrayBuffer, which a page has only when it is cross-origin isolated: every response
carries Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp. A host that cannot
send them (GitHub Pages) relies on the page's service worker (port/web/site/sw.js) instead; this server shows the
page as a host that can. .wasm is served as application/wasm so the browser compiles it as it streams.

--data DIR serves DIR's data.zip and data.json (as dc2data extract wrote them) at gamedata/: the page then plays
with them in place of data the player stored, the game reading the archive a range at a time over the network
(port/web/src/storage.cpp), so a phone on the same network plays without copying gigabytes first.

A browser gives threads and its storage only to a secure page: localhost, or https. --tls DIR also serves the page
over https on --tls-port (default --port + 1), for the other devices on the network, with DIR/cert.pem and
DIR/key.pem; when they are missing they are made with openssl, self-signed for this machine's addresses, so each
device shows a warning to accept once.
"""
import argparse
import email.utils
import gzip
import http.server
import ipaddress
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time

DATA_PREFIX = "/gamedata/"
DATA_FILES = ("data.zip", "data.json")
# Where the page posts its log lines.
CLIENT_LOG = "/client-log"


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript", ".mjs": "text/javascript", ".data": "application/octet-stream",
                      ".zip": "application/zip", ".json": "application/json"}
    data_directory = None
    # One connection carries a device's requests, one after another: the game reads data.zip in hundreds of
    # ranges, and over https a connection for each costs a phone a handshake every time.
    protocol_version = "HTTP/1.1"
    timeout = 300
    # Each answer leaves at once. Held back for the device's acknowledgement of what went before (Nagle), an
    # answer's last packet waited out the phone's delayed one: a fifth of a second for every range, where the
    # range itself took a millisecond, and a phone read four ranges a second.
    disable_nagle_algorithm = True

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        # Kept by the browser and asked about each time (If-Modified-Since): a build that changed is fetched
        # again, one that did not costs a 304 in place of the game's 20 MiB.
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Accept-Ranges", "bytes")
        super().end_headers()

    def unchanged(self, path):
        """Whether the browser's copy of path, by its If-Modified-Since, is still the file."""
        since = self.headers.get("If-Modified-Since")
        if not since or self.headers.get("If-None-Match"):
            return False
        try:
            when = email.utils.parsedate_to_datetime(since).timestamp()
        except (TypeError, ValueError, IndexError, OverflowError):
            return False
        return int(os.path.getmtime(path)) <= when

    def translate_path(self, path):
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean.startswith(DATA_PREFIX):
            name = clean[len(DATA_PREFIX):]
            if self.data_directory and name in DATA_FILES:
                return os.path.join(self.data_directory, name)
            return os.path.join(self.directory, "gamedata-not-served")
        return super().translate_path(path)

    def send_head(self):
        # Range requests (one range), which the game's reads of data.zip are; anything else as before.
        self.range_left = None
        requested = self.headers.get("Range")
        path = self.translate_path(self.path)
        if not requested and os.path.isfile(path + ".gz") and "gzip" in self.headers.get("Accept-Encoding", ""):
            # A compressed copy beside the file (tools/web/serve.py --compress made it): the game's code and its
            # preloaded files are a third the size, which a phone on Wi-Fi notices.
            return self.send_compressed(path)
        if not requested or not os.path.isfile(path):
            return super().send_head()
        match = re.fullmatch(r"\s*bytes=(\d*)-(\d*)\s*", requested)
        size = os.path.getsize(path)
        if not match or match.group(1) == match.group(2) == "":
            return super().send_head()
        if match.group(1) == "":
            start, end = max(0, size - int(match.group(2))), size - 1
        else:
            start = int(match.group(1))
            end = min(int(match.group(2)), size - 1) if match.group(2) else size - 1
        if start >= size or end < start:
            self.send_response(416)
            self.send_header("Content-Range", "bytes */%d" % size)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return None
        source = open(path, "rb")
        source.seek(start)
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Last-Modified", self.date_time_string(os.path.getmtime(path)))
        self.end_headers()
        self.range_left = end - start + 1
        self.range_start = start
        return source

    def send_compressed(self, path):
        if os.path.getmtime(path + ".gz") < os.path.getmtime(path):
            return super().send_head()
        if self.unchanged(path):
            self.send_response(304)
            self.send_header("Vary", "Accept-Encoding")
            self.end_headers()
            return None
        source = open(path + ".gz", "rb")
        self.send_response(200)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Encoding", "gzip")
        self.send_header("Vary", "Accept-Encoding")
        self.send_header("Content-Length", str(os.fstat(source.fileno()).st_size))
        self.send_header("Last-Modified", self.date_time_string(os.path.getmtime(path)))
        self.end_headers()
        return source

    def do_POST(self):
        """The page's log (site/shell.js sends its lines here), so a phone's run is read beside its requests."""
        length = int(self.headers.get("Content-Length") or 0)
        if self.path.split("?", 1)[0] != CLIENT_LOG or not 0 <= length <= 1 << 18:
            self.send_error(404)
            return
        for line in self.rfile.read(length).decode("utf-8", "replace").splitlines():
            if line.strip():
                self.log_message("client: %s", line[:500])
        self.send_response(204)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def copyfile(self, source, outputfile):
        if self.range_left is None:
            return super().copyfile(source, outputfile)
        left = self.range_left
        started = time.monotonic()
        while left > 0:
            chunk = source.read(min(left, 1 << 20))
            if not chunk:
                break
            outputfile.write(chunk)
            left -= len(chunk)
        outputfile.flush()
        # How much each read asked for and how long the device took to take it: a slow load is the network when
        # these are long, and the device's own work when they are short with long gaps between them.
        sent = self.range_left - left
        took = time.monotonic() - started
        self.log_message("range %d MiB + %d KiB, %.2f s, %.1f MiB/s", self.range_start >> 20, sent >> 10, took,
                         sent / 1048576 / max(took, 0.001))

    def log_request(self, code="-", size="-"):
        # The page's log arrives every two seconds; its lines are the news, not the request.
        if self.command == "POST" and code == 204:
            return
        super().log_request(code, size)

    def log_message(self, format, *args):
        if os.environ.get("CT_SERVE_QUIET"):
            return
        super().log_message(format, *args)


def addresses():
    """This machine's IPv4 addresses other devices may reach it at."""
    found = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            found.add(info[4][0])
    except OSError:
        pass
    try:
        # The address the default route leaves from (no packet is sent).
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("192.0.2.1", 9))
        found.add(probe.getsockname()[0])
        probe.close()
    except OSError:
        pass
    return sorted(a for a in found if not ipaddress.ip_address(a).is_loopback and not a.startswith("169.254."))


def openssl():
    found = shutil.which("openssl")
    if found:
        return found
    for candidate in (r"C:\Program Files\Git\usr\bin\openssl.exe", r"C:\Program Files\Git\mingw64\bin\openssl.exe"):
        if os.path.isfile(candidate):
            return candidate
    return None


def certificate(directory):
    """DIR/cert.pem and DIR/key.pem, made self-signed for this machine's addresses when missing."""
    os.makedirs(directory, exist_ok=True)
    cert = os.path.join(directory, "cert.pem")
    key = os.path.join(directory, "key.pem")
    if os.path.isfile(cert) and os.path.isfile(key):
        return cert, key
    tool = openssl()
    if tool is None:
        sys.exit("--tls: no cert.pem and key.pem in %s, and no openssl to make them" % directory)
    names = ["DNS:localhost", "DNS:" + socket.gethostname(), "IP:127.0.0.1"] + ["IP:" + a for a in addresses()]
    subprocess.run([tool, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256", "-days", "825",
                    "-subj", "/CN=Dark Chronicle on " + socket.gethostname(),
                    "-addext", "subjectAltName=" + ",".join(names),
                    "-addext", "basicConstraints=critical,CA:FALSE",
                    "-addext", "extendedKeyUsage=serverAuth",
                    "-keyout", key, "-out", cert], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("made a self-signed certificate for %s in %s" % (", ".join(names), directory), flush=True)
    return cert, key


def compress(directory):
    """Writes NAME.gz beside each .wasm, .data and .js in directory whose copy is missing or older."""
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if not name.endswith((".wasm", ".data", ".js")) or not os.path.isfile(path):
            continue
        target = path + ".gz"
        if os.path.isfile(target) and os.path.getmtime(target) >= os.path.getmtime(path):
            continue
        with open(path, "rb") as source, gzip.open(target + ".tmp", "wb", compresslevel=6) as out:
            shutil.copyfileobj(source, out, 1 << 20)
        os.replace(target + ".tmp", target)
        print("compressed %s: %d -> %d bytes" % (name, os.path.getsize(path), os.path.getsize(target)), flush=True)


class TlsServer(http.server.ThreadingHTTPServer):
    """Shakes hands on the request's thread, so one slow device does not hold up the others."""
    context = None

    def get_request(self):
        sock, address = super().get_request()
        return self.context.wrap_socket(sock, server_side=True, do_handshake_on_connect=False), address

    def finish_request(self, request, client_address):
        try:
            request.do_handshake()
        except (ssl.SSLError, OSError):
            # A device that has not accepted the certificate yet closes the connection here.
            return
        super().finish_request(request, client_address)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--data", help="a folder holding data.zip and data.json to play with, served at gamedata/")
    parser.add_argument("--tls", help="a folder for cert.pem and key.pem: also serve over https (made when missing)")
    parser.add_argument("--tls-port", type=int, help="the https port (default --port + 1)")
    parser.add_argument("directory", nargs="?", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                     "..", "..", "port", "build", "web"))
    parser.add_argument("--compress", action="store_true",
                        help="first write a gzip copy of each .wasm, .data and .js that lacks a current one")
    options = parser.parse_args()
    if options.compress:
        compress(options.directory)
    if options.data:
        Handler.data_directory = os.path.abspath(options.data)
        missing = [name for name in DATA_FILES if not os.path.isfile(os.path.join(Handler.data_directory, name))]
        if missing:
            sys.exit("--data: %s has no %s" % (Handler.data_directory, " or ".join(missing)))
    os.chdir(options.directory)
    servers = [http.server.ThreadingHTTPServer((options.bind, options.port), Handler)]
    print("serving %s at http://%s:%d/" % (os.getcwd(), options.bind, servers[0].server_address[1]), flush=True)
    if options.data:
        print("game data from %s at gamedata/" % Handler.data_directory, flush=True)
    if options.tls:
        cert, key = certificate(options.tls)
        TlsServer.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        TlsServer.context.load_cert_chain(cert, key)
        port = options.tls_port or options.port + 1
        servers.append(TlsServer((options.bind, port), Handler))
        hosts = addresses() if options.bind in ("0.0.0.0", "") else [options.bind]
        for host in hosts:
            print("on the network: https://%s:%d/" % (host, port), flush=True)
    for server in servers[1:]:
        threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        servers[0].serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
