#!/usr/bin/env python3
r"""A pretend mail server for the Mail transport (docs/modern/transports.md).

Purpose: newton's Internet mail (src/host/HostMail.ns, host/HostMailEngine.cpp)
sends by SMTP and fetches new mail by IMAP.  This is a server of both, on
127.0.0.1, so the ctests need no mail account and no network: it runs
newton with the main account pointed at itself (NEWTON_MAIL_ADDRESS,
_USER, _PASSWORD, _SMTP, _IMAP; NEWTON_MAIL_KEYSTORE=memory, so nothing
reaches the host's real key store; NEWTON_MAIL_POLL=2).

Each account has an In Box.  Mail sent to an account's address is put in
its In Box, so a test can send and receive its own mail.  Messages put
there at the start are made with Python's own email package
(multipart/alternative: the text quoted-printable or base64 as it
chooses, an HTML part, the subject encoded when it is not ASCII) - an
independent writer of what the Newton reads.

Usage:
    python tools/host/fakemail.py --account USER:PASSWORD:ADDRESS...
        [--message "USER=FROM|SUBJECT|TEXT"]... --spawn <newton> [arguments...]

--account   an account (repeatable); the first is newton's main account
--message   a message waiting in that account's In Box (repeatable; \n a new
            line, \uXXXX a character)

What it prints, for the ctest's expressions (newton's output is passed
through):
    fakemail: smtp on 127.0.0.1:PORT, imap on 127.0.0.1:PORT
    fakemail: USER logged in (smtp|imap)      (the first time only)
    fakemail: USER refused (smtp|imap)
    fakemail: from ADDRESS to ADDRESS, ...: "SUBJECT"   (a message sent)
    fakemail: USER fetched "SUBJECT"
and exits with newton's exit status.
"""

import argparse
import base64
import email
import email.message
import email.policy
import email.utils
import shlex
import os
import re
import socketserver
import subprocess
import sys
import threading


def log(text):
    print("fakemail: " + text, flush=True)


class Mailboxes:
    def __init__(self):
        self.lock = threading.Lock()
        self.accounts = {}          # user -> {password, address, box: [[uid, bytes, seen]]}
        self.next_uid = 1
        self.logged = set()

    def first_login(self, user, protocol):
        # (logged once each: newton checks every few seconds)
        with self.lock:
            if (user, protocol) in self.logged:
                return
            self.logged.add((user, protocol))
        log("%s logged in (%s)" % (user, protocol))

    def add_account(self, user, password, address):
        self.accounts[user] = {"password": password, "address": address, "box": []}

    def deliver(self, user, raw):
        with self.lock:
            self.accounts[user]["box"].append([self.next_uid, raw, False])
            self.next_uid += 1

    def user_of(self, address):
        for user, a in self.accounts.items():
            if a["address"].lower() == address.lower():
                return user
        return None


def unescape(text):
    """\\n a new line, \\uXXXX that character (so a ctest's arguments stay ASCII)"""
    text = re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), text)
    return text.replace("\\n", "\n")


def make_message(sender, subject, text):
    m = email.message.EmailMessage(policy=email.policy.SMTP)
    m["From"] = sender
    m["Subject"] = subject
    m["Date"] = email.utils.formatdate(localtime=True)
    m.set_content(text)
    html = "<html><body><p>" + text.replace("&", "&amp;").replace("<", "&lt;").replace("\n", "<br>") + "</p></body></html>"
    m.add_alternative(html, subtype="html")
    return m.as_bytes()


def smtp_handler(boxes):
    class Handler(socketserver.StreamRequestHandler):
        def say(self, line):
            self.wfile.write((line + "\r\n").encode())

        def handle(self):
            try:
                self.serve()
            except (ConnectionError, OSError):
                pass        # (newton gone)

        def serve(self):
            self.say("220 fakemail ESMTP")
            user = None
            sender = None
            recipients = []
            while True:
                line = self.rfile.readline()
                if not line:
                    return
                command = line.decode(errors="replace").rstrip("\r\n")
                verb = command.split(" ", 1)[0].upper()
                if verb in ("EHLO", "HELO"):
                    self.say("250-fakemail")
                    self.say("250-AUTH PLAIN")
                    self.say("250 8BITMIME")
                elif verb == "AUTH":
                    parts = command.split()
                    try:
                        _, name, password = base64.b64decode(parts[2]).decode().split("\0")
                    except Exception:
                        self.say("501 bad AUTH")
                        continue
                    a = boxes.accounts.get(name)
                    if a and a["password"] == password:
                        user = name
                        boxes.first_login(name, "smtp")
                        self.say("235 accepted")
                    else:
                        log("%s refused (smtp)" % name)
                        self.say("535 5.7.8 authentication failed")
                elif verb == "MAIL":
                    if user is None:
                        self.say("530 authentication required")
                        continue
                    sender = command[command.index("<") + 1:command.index(">")]
                    recipients = []
                    self.say("250 ok")
                elif verb == "RCPT":
                    recipients.append(command[command.index("<") + 1:command.index(">")])
                    self.say("250 ok")
                elif verb == "DATA":
                    self.say("354 go ahead")
                    data = bytearray()
                    while True:
                        l = self.rfile.readline()
                        if l in (b".\r\n", b".\n") or not l:
                            break
                        if l.startswith(b".."):
                            l = l[1:]
                        data += l
                    message = email.message_from_bytes(bytes(data), policy=email.policy.default)
                    log('from %s to %s: "%s"' % (sender, ", ".join(recipients), message["Subject"]))
                    for r in recipients:
                        u = boxes.user_of(r)
                        if u:
                            boxes.deliver(u, bytes(data))
                    self.say("250 queued")
                elif verb == "QUIT":
                    self.say("221 bye")
                    return
                else:
                    self.say("502 not here")

    return Handler


def imap_handler(boxes):
    class Handler(socketserver.StreamRequestHandler):
        def say(self, line):
            self.wfile.write((line + "\r\n").encode())

        def handle(self):
            try:
                self.serve()
            except (ConnectionError, OSError):
                pass        # (newton gone)

        def serve(self):
            self.say("* OK fakemail IMAP4rev1 ready")
            user = None
            while True:
                line = self.rfile.readline()
                if not line:
                    return
                text = line.decode(errors="replace").rstrip("\r\n")
                tag, _, rest = text.partition(" ")
                words = rest.split(" ")
                verb = words[0].upper()
                if verb == "LOGIN":
                    try:
                        name, password = shlex.split(rest[len("LOGIN "):])
                    except ValueError:
                        self.say(tag + " BAD")
                        continue
                    a = boxes.accounts.get(name)
                    if a and a["password"] == password:
                        user = name
                        boxes.first_login(name, "imap")
                        self.say(tag + " OK LOGIN done")
                    else:
                        log("%s refused (imap)" % name)
                        self.say(tag + " NO [AUTHENTICATIONFAILED] wrong password")
                elif user is None and verb != "LOGOUT":
                    self.say(tag + " NO log in first")
                elif verb == "SELECT":
                    box = boxes.accounts[user]["box"]
                    self.say("* %d EXISTS" % len(box))
                    self.say(tag + " OK [READ-WRITE] SELECT done")
                elif verb == "UID" and words[1].upper() == "SEARCH":
                    with boxes.lock:
                        uids = [str(m[0]) for m in boxes.accounts[user]["box"] if not m[2]]
                    self.say("* SEARCH" + ("" if not uids else " " + " ".join(uids)))
                    self.say(tag + " OK SEARCH done")
                elif verb == "UID" and words[1].upper() == "FETCH":
                    uid = int(words[2])
                    with boxes.lock:
                        found = [m for m in boxes.accounts[user]["box"] if m[0] == uid]
                    if found:
                        raw = found[0][1]
                        subject = email.message_from_bytes(raw, policy=email.policy.default)["Subject"]
                        log('%s fetched "%s"' % (user, subject))
                        self.wfile.write(("* 1 FETCH (UID %d BODY[] {%d}\r\n" % (uid, len(raw))).encode() + raw + b")\r\n")
                    self.say(tag + " OK FETCH done")
                elif verb == "UID" and words[1].upper() == "STORE":
                    uid = int(words[2])
                    with boxes.lock:
                        for m in boxes.accounts[user]["box"]:
                            if m[0] == uid:
                                m[2] = True
                    self.say(tag + " OK STORE done")
                elif verb == "LOGOUT":
                    self.say("* BYE")
                    self.say(tag + " OK LOGOUT done")
                    return
                else:
                    self.say(tag + " BAD not here")

    return Handler


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--account", action="append", required=True, help="USER:PASSWORD:ADDRESS")
    ap.add_argument("--message", action="append", default=[], help="USER=FROM|SUBJECT|TEXT")
    ap.add_argument("--spawn", nargs=argparse.REMAINDER, required=True, help="the newton to run, and its arguments")
    args = ap.parse_args()
    boxes = Mailboxes()
    for a in args.account:
        user, password, address = a.split(":", 2)
        boxes.add_account(user, password, address)
    for m in args.message:
        user, _, rest = m.partition("=")
        sender, subject, text = rest.split("|", 2)
        boxes.deliver(user, make_message(unescape(sender), unescape(subject), unescape(text)))
    smtp = Server(("127.0.0.1", 0), smtp_handler(boxes))
    imap = Server(("127.0.0.1", 0), imap_handler(boxes))
    threading.Thread(target=smtp.serve_forever, daemon=True).start()
    threading.Thread(target=imap.serve_forever, daemon=True).start()
    sp, ip = smtp.server_address[1], imap.server_address[1]
    log("smtp on 127.0.0.1:%d, imap on 127.0.0.1:%d" % (sp, ip))
    main_user, main_password, main_address = args.account[0].split(":", 2)
    env = dict(os.environ)
    env.update({
        "NEWTON_MAIL_ADDRESS": main_address, "NEWTON_MAIL_USER": main_user,
        "NEWTON_MAIL_PASSWORD": main_password, "NEWTON_MAIL_NAME": "Newton Tester",
        "NEWTON_MAIL_SMTP": "smtp://127.0.0.1:%d" % sp, "NEWTON_MAIL_IMAP": "imap://127.0.0.1:%d" % ip,
        "NEWTON_MAIL_KEYSTORE": "memory", "NEWTON_MAIL_POLL": "2"})
    spawn = [os.path.abspath(args.spawn[0])] + args.spawn[1:]
    proc = subprocess.Popen(spawn, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors="replace")
    for line in proc.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
    status = proc.wait()
    smtp.shutdown()
    imap.shutdown()
    return status


if __name__ == "__main__":
    sys.exit(main())
