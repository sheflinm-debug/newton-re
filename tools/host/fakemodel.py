#!/usr/bin/env python3
"""A pretend language model for the Assistant's engine (docs/modern/assistant.md).

newton's Assistant, with NEWTON_ASSISTANT=1 and NEWTON_ASSIST_ENGINE=http,
hands a sentence its own parse cannot read to an OpenAI-style chat
completions service (host/HostAssist.cpp, host/HostAssist.ns).  This is
one, answering from a table instead of a model, so the ctests need no
model and no network: it listens on 127.0.0.1, runs newton with
NEWTON_ASSIST_URL pointing at itself, and answers each request's user
message by the table - "none" for a sentence it does not know.

Usage:
    python tools/host/fakemodel.py [--answer "SENTENCE=COMMAND"]... [--status N]
        --spawn <newton> [newton's arguments...]

--answer    a sentence and the command to answer it with (repeatable);
            matched without case
--status    answer every request with this HTTP status instead (an
            engine failing)

What it prints, for the ctest's expressions (newton's own output is
passed through):
    fakemodel: listening on 127.0.0.1:PORT
    fakemodel: offered the tasks: schedule, find, ...   (from the system message)
    fakemodel: asked "SENTENCE" -> "COMMAND"
and exits with newton's exit status.
"""

import argparse
import http.server
import json
import os
import re
import subprocess
import sys
import threading


def log(text):
    print("fakemodel: " + text, flush=True)


def make_handler(table, status):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            try:
                request = json.loads(raw.decode("utf-8"))
            except ValueError:
                log("a request that is not JSON")
                self.send_error(400)
                return
            messages = request.get("messages", [])
            system = next((m["content"] for m in messages if m.get("role") == "system"), "")
            user = next((m["content"] for m in messages if m.get("role") == "user"), "")
            m = re.search(r"starts with one of these words: ([^.]*)\.", system)
            log("offered the tasks: " + (m.group(1) if m else "(none)"))
            if status != 200:
                log('asked "%s" -> HTTP %d' % (user, status))
                self.send_response(status)
                self.end_headers()
                self.wfile.write(b'{"error": "pretend failure"}')
                return
            answer = table.get(user.strip().lower(), "none")
            log('asked "%s" -> "%s"' % (user, answer))
            body = json.dumps({
                "id": "fake", "object": "chat.completion", "model": request.get("model", ""),
                "choices": [{"index": 0, "finish_reason": "stop",
                             "message": {"role": "assistant", "content": answer}}],
            }).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--answer", action="append", default=[], help="SENTENCE=COMMAND")
    ap.add_argument("--status", type=int, default=200, help="answer every request with this HTTP status")
    ap.add_argument("--spawn", nargs=argparse.REMAINDER, required=True, help="the newton to run, and its arguments")
    args = ap.parse_args()
    table = {}
    for pair in args.answer:
        sentence, _, command = pair.partition("=")
        table[sentence.strip().lower()] = command.strip()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), make_handler(table, args.status))
    port = server.server_address[1]
    log("listening on 127.0.0.1:%d" % port)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env = dict(os.environ)
    env["NEWTON_ASSIST_ENGINE"] = "http"
    env["NEWTON_ASSIST_URL"] = "http://127.0.0.1:%d/v1/chat/completions" % port
    proc = subprocess.Popen(args.spawn, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors="replace")
    for line in proc.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
    status = proc.wait()
    server.shutdown()
    return status


if __name__ == "__main__":
    sys.exit(main())
