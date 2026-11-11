# Browsing the web

**Status: a proposal.** Facts about the engines here are from general
knowledge, not checked for this page: their state, sizes and licences must
be confirmed before one is chosen.

## What Newton has

Period browsers (Newt's Cape, NetHopper) that run on the reconstruction
(`docs/comms/README.md`). They speak HTTP without TLS and an HTML of the
1990s, so almost no current site loads. They stay, for period content and
for pages written for Newtons.

## The aim

A page a person needs today opens on the Newton, and what they find there
can be kept and worked on as Newton data. Writing a web engine is out of
the question; one is borrowed. Which one depends on the device, so there
are three tiers behind one browser application.

## Tier 1: a full engine, on capable devices

A current engine runs as a separate process, draws pages into a buffer
the Newton shows in a view, and takes pen, finger and keyboard events back.

| Engine | For | Against |
|---|---|---|
| **WPE WebKit** | made for embedded Linux with no desktop; draws to a buffer; full modern web; permissive and LGPL parts | Linux only; a large build |
| Chromium (CEF or similar) | the most compatible; off-screen drawing | the largest by far; heavy on memory |
| Servo | small, embeddable by design | still incomplete: many sites break |
| Ladybird | independent and new | not ready to embed |

Recommended: **WPE WebKit** where the Newton is the system on a Linux
kernel. Where the Newton is an application on another system, that
system's own web view (WKWebView on Apple's, WebView on Android), which on
iOS is the only one allowed.

What the Newton adds around the engine:
- **Handwriting into pages.** A text field that takes the focus opens the
  Newton's own input (the recogniser, the keyboard), and the result is
  typed into the page.
- **Downloads** become files (`files.md`).
- **The Action button** on a page: send its address, print it, save it as
  PDF, or keep it as a note (the next tier's conversion).
- **Permissions**: the engine's process sees the network and the downloads
  folder, nothing else. Passwords stay in the engine's store, never in a
  soup.

## Tier 2: pages as Newton text, on small or e-paper devices

A page is fetched, its article extracted, and the result shown as a
Newton note: headings, paragraphs, lists, links, pictures in grays. No
scripts run. This is what browsers call a reader view; open
implementations of the extraction exist (Mozilla's Readability is the
usual one) and a small HTML and CSS parser is enough underneath.

It suits a one-bit or e-paper screen far better than a rendered page, costs
little memory, and gives the Newton its own advantage: the page *is* a
note, so it can be filed, searched, written on, marked (`marking.md`) and
synced. It fails on sites that are applications, not documents.

Where the device cannot even do the fetching and extraction (the ESP32),
a helper the user runs on another machine does it and sends the note: the
arrangement the list's FrogFind uses for old machines
(`docs/newtontalk-findings.md`).

## Tier 3: a small engine, as a fallback

NetSurf is a small independent engine in C with a frame-buffer front end;
it shows simple and older sites properly, with little or no script
support. Worth having only if tier 1 is unavailable on a device that wants
more than tier 2.

## One application

"Web" in Extras: an address line that also searches, back and forward,
bookmarks and history in soups (so Find and sync reach them), a button that
switches a page between "as a page" (tier 1) and "as a note" (tier 2). On a
device without tier 1 the first is absent. On e-paper the default is "as a
note".

## Tests

Against a local test server (`tools/host/httpserve.py`, with TLS added):
tier 2's extraction on a fixed set of saved pages, compared with reference
notes; a download landing in `Downloads`; a page kept as a note and found
by Find. Tier 1: a page drawn into the view, a tap on a link, writing
recognised into a form field, the engine's process killed and the Newton
unaffected.

## Owner decisions

1. Whether tier 2 alone is enough for a first version: it is perhaps a
   tenth of the work and fits every device.
2. The engine for tier 1, after its size and licence are confirmed.
