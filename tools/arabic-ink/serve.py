#!/usr/bin/env python3
"""Serve the Arabic ink capture page to a phone on the local network and
keep the samples written on it.

Purpose:  collect Arabic handwriting as pen strokes (points with time and
          pressure) from a stylus phone or tablet, for replaying through and
          training the reconstruction's handwriting recognisers.
Inputs:   tools/arabic-ink/capture.html (the page), served at /.
Outputs:  one JSON file per saved sample in the samples directory
          (default build/arabic-ink/samples/), named <time>-<n>.json, with
          the fields capture.html documents (word, set, strokes as flat
          [x, y, t, pressure, ...] arrays per stroke, slate size and guide
          lines, device pixel ratio, pointer types, user agent, createdAt).
Usage:    python tools/arabic-ink/serve.py [--port 8642] [--samples DIR]
          then open one of the printed http://<this PC>:<port>/ addresses on
          the phone (same Wi-Fi).  Windows may ask whether to let Python
          accept connections on private networks: allow it for this to work.
          Stop with Ctrl+C.

The page talks to this server through three routes:
  GET    /api/samples       the saved samples' ids and words
  POST   /api/samples       save one sample (the JSON body); answers its id
  DELETE /api/samples/<id>  delete one sample (the page's "Delete last save")

Only the standard library is used.  The server listens on every interface
of this machine, so anyone on the same network can reach it while it runs;
it serves only the page and the samples directory's own files.
"""

import argparse
import json
import os
import re
import socket
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PAGE = os.path.join(HERE, "capture.html")
ID_RE = re.compile(r"^[0-9]{8}T[0-9]{6}-[0-9]+$")
MAX_BODY = 4 * 1024 * 1024

# The published page is written for the artifact host, which wraps it in a
# document skeleton; this server adds the same one.
SKELETON_HEAD = ('<!doctype html><html><head><meta charset="utf-8">'
                 '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
                 '<style>:root{padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}'
                 'body{margin:0}[hidden]{display:none!important}</style></head><body>')
SKELETON_TAIL = '</body></html>'


def lan_addresses():
    """This machine's IPv4 addresses other than loopback, best guess first."""
    found = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))  # no packet is sent; picks the outgoing interface
        found.append(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addr = info[4][0]
            if not addr.startswith("127.") and addr not in found:
                found.append(addr)
    except OSError:
        pass
    return found


def make_handler(samples_dir):
    counter = {"n": 0}

    class Handler(BaseHTTPRequestHandler):
        server_version = "ArabicInk/1"

        def _send(self, code, body, ctype="application/json; charset=utf-8"):
            data = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path in ("/", "/index.html", "/capture.html"):
                with open(PAGE, "rb") as f:
                    page = f.read()
                self._send(200, SKELETON_HEAD.encode() + page + SKELETON_TAIL.encode(), "text/html; charset=utf-8")
            elif path == "/api/samples":
                out = []
                for name in sorted(os.listdir(samples_dir)):
                    if not name.endswith(".json"):
                        continue
                    try:
                        with open(os.path.join(samples_dir, name), encoding="utf-8") as f:
                            out.append({"id": name[:-5], "word": json.load(f).get("word", "")})
                    except (OSError, ValueError):
                        continue
                self._send(200, {"samples": out})
            else:
                self._send(404, {"error": "not found"})

        def do_POST(self):
            if self.path != "/api/samples":
                return self._send(404, {"error": "not found"})
            length = int(self.headers.get("Content-Length") or 0)
            if length <= 0 or length > MAX_BODY:
                return self._send(413, {"error": "sample too large or empty"})
            try:
                doc = json.loads(self.rfile.read(length).decode("utf-8"))
            except ValueError:
                return self._send(400, {"error": "not JSON"})
            if not isinstance(doc, dict) or not isinstance(doc.get("word"), str) or not isinstance(doc.get("strokes"), list):
                return self._send(400, {"error": "a sample needs a word and strokes"})
            counter["n"] += 1
            sid = time.strftime("%Y%m%dT%H%M%S") + "-" + str(counter["n"])
            with open(os.path.join(samples_dir, sid + ".json"), "w", encoding="utf-8") as f:
                json.dump(doc, f, ensure_ascii=False)
            # answer first: the log line is a convenience and must never cost a save
            self._send(200, {"id": sid})
            print("saved %s  %s  (%d strokes)" % (sid, doc["word"], len(doc["strokes"])), flush=True)

        def do_DELETE(self):
            m = re.match(r"^/api/samples/([^/]+)$", self.path)
            if not m or not ID_RE.match(m.group(1)):
                return self._send(404, {"error": "not found"})
            target = os.path.join(samples_dir, m.group(1) + ".json")
            if os.path.exists(target):
                os.remove(target)
                print("deleted %s" % m.group(1), flush=True)
            self._send(200, {"deleted": m.group(1)})

        def log_message(self, fmt, *args):
            pass  # the save/delete lines above are the log

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, default=8642)
    ap.add_argument("--samples", default=os.path.join(REPO, "build", "arabic-ink", "samples"),
                    help="directory the samples are written to (default build/arabic-ink/samples)")
    args = ap.parse_args()
    # A Windows console's code page (cp1252 and the like) has no Arabic: an
    # Arabic word in a log line would raise UnicodeEncodeError mid-request.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    os.makedirs(args.samples, exist_ok=True)
    server = ThreadingHTTPServer(("0.0.0.0", args.port), make_handler(args.samples))
    print("Arabic ink capture: samples go to %s" % os.path.abspath(args.samples))
    for addr in lan_addresses():
        print("  open on the phone:  http://%s:%d/" % (addr, args.port))
    print("  (Ctrl+C stops it)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
