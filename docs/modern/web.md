# Browsing the web

**Status: a proposal; tier 2 built (2026-11-12) - pages as Newton text
(State).** Facts about the engines here are from general
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

## State (2026-11-12, branch `modern`)

Built: tier 2, behind the Host panel's "Web" (`host/Features.h`'s
`kFeatureWeb`, `NEWTON_WEB`). The owner had said to carry on without
asking, so decision 1 was taken as this page suggests: tier 2 first.

- **Fetching** (`host/HostWeb.cpp` over the IPP printers' socket,
  `print/host/HostIPPSocket.h`): HTTP/1.1 GET with `Connection: close`,
  plain or over TLS. An https site must be vouched for by the system's
  store (`NEWTON_WEB_TRUST_ANY` for a test server). Redirects are
  followed, five at most. A page may be 16 MB at most, and a fetch gives
  up after 30 s without progress.
- **What comes back:** an HTML or text answer is made a page. Anything
  else - a CSV, a PDF, a picture - is saved into the Files folder's
  Downloads (`files.md`), a number added when the name is taken.
- **Reading a page** (`host/HostWebPage.cpp`, plain C++, ctest
  `host.WebPage`):
  - The HTTP answer is taken apart (chunked or not; the type and charset).
    UTF-8, or ISO 8859-1 and Windows-1252 made UTF-8 - the header's
    charset, else the page's own `<meta>`.
  - The HTML is read into a small tree as browsers read it: implied ends
    of paragraphs, items and cells; raw script and style; entities.
  - The article is found as reader views find it (the idea of Mozilla's
    Readability, written here from scratch). Navigation, headers,
    footers, sidebars, forms, scripts and whatever is classed as a menu,
    comment, advertisement, cookie banner and the like are left out. Each
    paragraph of some length scores its parent, and half its grandparent,
    by its length and commas. The best element, with its siblings that
    score near it, is the article; the whole body is used when nothing
    scores.
  - The page is written out as headings (with their level), paragraphs,
    list items, quotations, preformatted text, a table's rows joined with
    " | ", and "[picture: ...]" for each picture. Each link becomes "[n]"
    after its words, made absolute (`<base>` honoured; `javascript:`,
    `mailto:` and in-page links left out).
- **The Web application** (`src/host/HostWeb.ns`, Extras):
  - An address line that also searches (`HostWebAddressOf`): an address
    is taken as it is ("http://" added), a numeric host, `localhost` or
    one with a port too, and anything else goes to the search engine,
    `NEWTON_WEB_SEARCH` (DuckDuckGo's HTML page by default).
  - The page is drawn as Newton text: the heading sizes, items with a
    bullet, quotations in italic, link lines underlined.
  - A tap on a line with a link goes there, or opens a pop-up when the
    line has several; the scroll arrows page; Back goes back.
  - **Keep** makes the page a Notepad note: its paragraphs, headings in
    bold, and the links listed at the end with `fromURL`. So it can be
    filed, found, written on, marked and synced.
  - Every page shown goes into the soup "Web History:host".
- Demo `src/host/demo/web.ns`, ctests `host.NewtonWeb` and `.off`, against
  `tools/host/httpserve.py` serving `src/host/demo/web` (a news page with
  menus, a cookie banner, a sidebar, an advertisement and a footer around
  its article; a second page; ISO 8859-1; plain text; a CSV). It covers a
  redirect, the article without the site's own parts, a link tapped,
  Back, the Latin-1 page, a note kept, a download, and a search address.
  The tests point the search engine at a dead local address, so nothing
  leaves the machine.

Not built (NOT YET): tiers 1 and 3 (decision 2, the engine, is still the
owner's); pictures shown (they are named); forms and cookies; bookmarks
(history only); a page saved as PDF; compressed answers (`identity` is
asked for); the helper that does the fetching for the ESP32.

Found on the way:
- A symbol keeps the case it was first interned with. The ROM's own
  `'Web` made `HostFeatureOn('web)` and the Host panel's setter miss the
  feature; both now compare without case.
- An application that lays its children out by their justification is
  not drawn whole at its first drawing (the strips over the Notepad's
  header and bar are left to the Notepad), so `HostWeb.ns` asks for a
  redraw once it is open (`AddDeferredSend(self, 'Dirty, [])`).
- An earlier version of the address rule (a letter after the last dot)
  took "127.0.0.1:PORT/articles" for words to search for, and one test
  run sent that string to DuckDuckGo; hence the dead local search engine
  in the tests.
