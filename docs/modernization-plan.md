# Modernizing NewtonOS: a phased plan

**Status: a proposal (2026-11-11), nothing in it built yet.** The owner's
goal is NewtonOS running natively on today's computers, phones and tablets,
with modern networking. This page orders the work into phases. It is based
on what the tree already does (`docs/next-steps.md`, the `docs/host-*.md`
pages) and on the NewtonTalk list's discussion (`docs/newtontalk-findings.md`).
The decisions marked **Owner** are the owner's to make; each has a
recommendation. Effort is given as small, medium or large, not in days.

## The approach, and why no new kernel

NewtonOS's own kernel (`os600`) stays, running as a program on the host's
operating system: one host thread per Newton task, one running at a time
(`docs/host-runtime.md`). The host OS supplies drivers, Wi-Fi and its
security, storage, power and the TCP/IP stack. A UNIX-type kernel written
for NewtonOS would have to supply all of those again, and the kernel's
memory protection rests on ARM MMU domains, which current ARM chips no
longer have (`docs/newtontalk-findings.md`). On a dedicated device the same
program runs as the only visible application of a small Linux.

## Where it stands

| Target | State |
|---|---|
| Windows, Linux (x86-64, 32-bit x86) | runs; the full test suite passes |
| reMarkable Paper Pro (ARM64 Linux, e-paper, pen) | runs from AppLoad at 2x |
| LilyGO T-Deck Pro (ESP32-S3, FreeRTOS) | boots; Wi-Fi, cellular and mesh in progress |
| macOS | planned (`docs/host-macos.md`); builds headless in principle, never compiled on a Mac |
| Android, iOS/iPadOS | nothing |

Already modern: networking through the host's sockets with the NIE built in
(`docs/comms/README.md`), docking over TCP, IPP printing with TLS, a 64-bit
NewtonScript flavour, colour behind `--colour`, host fonts on Windows.

## Phase 1: what every target needs

No new platform; each item removes something that would otherwise be
redone per port.

1. **One portable host layer (medium).** Today each host has its own window,
   input and sound code (`src/host/win32`, `x11`, `alsa`, `remarkable`,
   `esp32`). Add one host built on SDL3: a pixel canvas, pen and touch
   events with pressure, the keyboard, sound, the clipboard. SDL3 runs on
   macOS, Android, iOS and Linux (X11 and Wayland), so the three missing
   platforms share one implementation. The existing hosts stay.
   **Owner:** SDL3 as a vendored dependency, or a native layer per platform.
   Recommended: SDL3; a native layer can replace it on a platform later,
   behind the same seam.
2. **TLS for endpoints (medium).** A script's TCP endpoint is plain TCP, so
   nothing on the Newton can reach an https site or a modern mail server.
   Add a host option on the `inet` endpoint that wraps the connection in
   TLS, reusing the print area's TLS socket and its trust policy
   (`docs/print/README.md`, "ipps://"). Then teach the pieces that take a
   URL (Newt's Cape, NetHopper's HTTP, the mail transports) to ask for it,
   by host patches like the NIE's one (`romsrc/rex/inetenbl.patches.tsv`).
3. **Fingers as well as a pen (medium).** The Newton assumes a pen: a touch
   is ink or a tap. On a capacitive screen a finger must tap and scroll
   while a stylus writes, and the palm must be ignored while writing.
   Decide the mapping once (finger = tap and drag-scroll, stylus = pen;
   finger writes when there is no stylus) and implement it in the host's
   pen source, not in the views.
4. **Screen density (medium; a study exists).** `docs/host-hidpi.md` lays
   out the Newton at one size and draws at a finer one. Phones and tablets
   need it. **Owner:** which of the study's two ways; it has waited on this
   decision since 2026-10-02.
5. **Finish what is merged but partial (small each).** Colour's open items
   (`docs/qd/colour.md`, "Open"); host fonts on Linux through FreeType
   (also what Android would use); the wide integers the 64-bit flavour
   still narrows on disk, only if 64-bit times and ids are wanted in soups.
6. **A host's duties written down (small).** One page listing what a new
   host must supply and which tests prove it: the canvas, pen, keys, sound,
   sockets, the store file, the clock, the task threads, sleep and wake,
   restart. The macOS page is most of it already.

Done when: the SDL host passes the walkthrough tests on Windows and Linux
beside the native hosts, and a script fetches an https page.

## Phase 2: the desktops completed

1. **macOS (medium).** Follow `docs/host-macos.md`, with the SDL host as its
   window and sound. Needs a Mac for the first build.
2. **Releases (small).** Signed, installable builds for Windows, macOS and
   Linux, made by a script in `tools/`. The list already has a GitHub
   release (v0.0.1: Windows, Linux, reMarkable).
3. **Host integration (small each).** The host's clipboard, a package or a
   text file dropped or opened from the host (a package dropped on the
   X11 window is installed already), the time zone and locale from the
   host.

## Phase 3: a dedicated device

A tablet or handheld that starts straight into the Newton: a small Linux
image whose only visible program is `newton`, on a board with good Linux
support (a Raspberry Pi with a touch screen, a PineNote-class e-paper
tablet). The reMarkable work is the model. New here: the power key and lid
mapped to the Newton's sleep and wake (done on the reMarkable), the battery
gauge from the host, Wi-Fi chosen from Internet Setup as on the T-Deck
(`host/HostWifi.h`) but through the Linux network service, updates of the
image. Medium, mostly integration. **Owner:** which device, if any; this
phase can be skipped without affecting the others.

## Phase 4: Android

Large, mostly new ground:
- The build: the Android NDK's clang, ARM64, LP64 like Linux, so
  `docs/host-lp64.md` applies. To check first: C++ exceptions and the task
  threads under the NDK's runtime, and the restart, which is fork/execv on
  POSIX hosts and must become a restart inside the process.
- The application: SDL's Android activity around the Phase 1 host.
- The life cycle: Android stops and kills background applications. Pausing
  must put the Newton to sleep with its store flushed, and alarms must be
  handed to Android to wake it.
- Storage and sharing: the store in the application's private storage, a
  `.pkg` opened from a file manager installed, backup through the dock.
- The stylus: pressure and palm rejection from Android's stylus events.

Done when: the walkthrough passes on a phone and a pen tablet, a package
installs from the file manager, and an alarm fires with the app closed.

## Phase 5: iOS and iPadOS

Large, and it needs a Mac, an Apple developer account and Phase 2's macOS
work first. What differs from Android:
- **No generated code.** iOS forbids it; the ARM interpreter for packages'
  native code (`src/armcpu/`) is an interpreter, so it is allowed.
- **Restart in-process**, as on Android.
- **Apple Pencil** through SDL or UIKit, with its hover and pressure.
- **Distribution is the hard part**, not the port: see the next section.
  Without the App Store, a build can still be installed by its owner
  through Xcode or TestFlight.

## In parallel: what may be distributed

**Owner, and not a technical question.** The OS boots from `romsrc/`, which
is the ROM's NewtonScript, resources and fonts recovered from Apple's ROM,
and `src/` reproduces Apple's code function by function. Whether that can
be published in an app store, or under what name, is a legal question this
page cannot answer. The list reports that Apple has been asked for the ROM
source more than once and that legal issues stand in the way, and one
member proposes a new name for the project to keep clear of trouble
(`docs/newtontalk-findings.md`). Options to weigh with advice: distribute
the program without the ROM-derived data and have the user supply a ROM
image they own, as Einstein does (`--rom` already boots one); a new name
and artwork; asking Apple. This gates Phases 4 and 5 for public release,
not for the owner's own devices.

## Phase 6: a Newton for today's use

Specified in `docs/modern/` (its README is the index and gives an order of
building). Each is an opt-in DEVIATION, so the faithful machine stays
available:
- ink kept after recognition, searchable, shown as handwriting or text;
- structure and live details recognised from what is written;
- marking a line, and one list of everything marked;
- a Camera and a Gallery with albums in a folder tree;
- permissions for packages;
- a language model behind the Assist button;
- new transports, and sync between Newtons with no cloud owner;
- inspecting and changing the running system on the device.

Dropped after discussion (2026-11-11): system themes, a simplified mode for
each user, and OpenDoc-style documents beyond a picture or a soup entry
shown in a note.

**Owner:** each page ends with its open decisions. None blocks a port.

## Order and dependencies

Phase 1 comes first; items 1 to 3 of it unblock everything else. Phases 2,
3, 4 and 6 can then run in any order or side by side; Phase 5 needs Phase
2's macOS work. Recommended order if one thing is done at a time: Phase 1,
macOS, Android, then the Assistant.

Decisions waiting on the owner, in the order they block work:
1. SDL3 or native layers (Phase 1.1).
2. The HiDPI way (Phase 1.4).
3. Which platform after the desktops: Android, a dedicated device, or iOS.
4. The distribution question, before any public phone release.
