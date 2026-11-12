# A Newton for today's use: the specifications

**Status: proposals (2026-11-11); built so far: `ink-and-text.md`, a first
version of `marking.md`, checkboxes, lists and live details from `structure.md`, the
record-only first version of `permissions.md`, a first `assistant.md`,
the Files application and files as soup entries of `files.md`, and a first
Gallery and Camera of `camera-gallery.md` (each page's State says what).** These pages
specify the additions the owner chose for a modern NewtonOS. Each builds on
something the ROM already does and is an opt-in DEVIATION: with it off, the
machine is the ROM's. They are Phase 6 of `docs/modernization-plan.md` and
do not depend on which devices the OS runs on, except where a page says so.

| Page | What | Builds on |
|---|---|---|
| `permissions.md` | What a package may read and send, granted at install | the package installer |
| `assistant.md` | A language model behind the Assist button | the Assistant's task templates |
| `sync.md` | Several Newtons sharing one set of data, no cloud owner | soups, entry ids and change notifications |
| `transports.md` | New ways to send, appearing in every application at once | the Action button, the In/Out Box |
| `ink-and-text.md` | Ink kept after recognition: searchable, shown as ink or text | ink words, deferred recognition |
| `structure.md` | Lists, tables, sums, diagrams and live details recognised from what is written | the recogniser arbitration, stationery |
| `marking.md` | A mark beside any line, and one list of everything marked | paragraph gestures, soups |
| `files.md` | Real files as Newton entries, drives as stores, and viewing other systems' formats | stores, large binaries, stationery, the dock's translators |
| `web.md` | Browsing: a borrowed engine on capable devices, pages as notes on small ones | the Notepad, the host's network |
| `camera-gallery.md` | A Camera and a Gallery, with albums as real folders | `files.md`, stationery, the host's services |
| `malleable.md` | Looking inside and changing the running system on the device | frames, the compiler and decompiler |

## Rules common to all of them

1. **Off means the ROM's machine.** Each feature has a switch in the Host
   panel (`src/host/HostSettings.ns`) and an environment variable for tests.
2. **Written as the ROM's own applications are.** NewtonScript on the ROM's
   protos (magic pointers, `analysis/magicpointer.py`), loaded by the host as
   `comms/host/HostMessages.ns` and `HostText.ns` are. Native code only for
   what NewtonScript cannot do, behind a `hal/host/` interface with a
   desktop fake so ctests run everywhere (the model: `HostCellular.h` and
   `tools/modem/fakecell.py`).
3. **Never destructive, never interrupting.** A recognition or a suggestion
   is an offer the user can ignore; what was written stays; a scrub undoes.
4. **Data in soups, in documented frames**, so Find, filing, routing, sync
   and the Assistant reach it without special cases.
5. **Persistent formats are big-endian and 32-bit clean**
   (`toolbox/ByteOrder.h`, `frames/NarrowRef.h`).
6. **Each has a demo and ctests** at the four screen sizes the Messages
   application is tested at.

## Order

`permissions.md` is first in design, because the Assistant, sync and the
transports all move the user's data. `ink-and-text.md` comes before
`structure.md` and `marking.md`, which need ink kept. The transports need
TLS for endpoints (`docs/modernization-plan.md`, Phase 1). The rest are
independent. A suggested order of building:

1. Ink kept and searchable; marking (small, self-contained, daily use).
2. Structure: checkboxes and lists, then live details, then the rest.
3. Permissions, then the Assistant.
4. Files, then Camera and Gallery, then foreign formats.
5. Transports, then sync; the web as notes.
6. The malleable system; a full web engine.

Every page ends with the decisions that are the owner's.
