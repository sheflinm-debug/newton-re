# Sync: several Newtons, one set of data, no cloud owner

**Status: a proposal; a first version built (2026-11-12) - sync through a
folder, the conflict rule as proposed here (State). The conflict rule
remains the owner's decision.**

## What Newton has

- Every soup entry has a `_uniqueID` and a `_modTime`, and applications are
  told of changes (`RegSoupChange`, `XmitSoupChange`).
- Union soups merge the same soup across stores.
- The dock protocol synchronises with a desktop program, one Newton at a
  time, over serial or TCP port 3679 (`docs/comms/README.md`).
- Every object has one external form, NSOF, used for beaming and the dock.

What it lacks: two Newtons that both change data and then agree. The dock's
sync assumes one desktop as the meeting point, and `_uniqueID` is unique
only within one soup on one store.

## The aim

A phone-sized Newton, a tablet and a desktop window all show the same Names,
Dates and Notes. Each works offline. They exchange changes when they can
reach each other, directly or through a folder the user controls. No
account, no server that owns the data.

## Design

**Identity.** A synced entry gets a slot `syncID`: 128 random bits made
when it is created, the same on every device. `_uniqueID` stays local.

**The change log.** Each store keeps a soup `SyncLog`: for each change, the
entry's `syncID`, the soup's name, what kind (add, change, delete), and a
stamp. It is fed by the soup-change notifications, so applications need no
changes. A delete leaves a tombstone, kept until every known device has
seen it.

**Stamps.** A hybrid logical clock: the device's time, a counter, and the
device's id. It orders changes consistently even when a device's clock is
wrong, which `_modTime` alone cannot.

**Exchange.** Two devices compare what each has seen from the other (one
stamp per device) and send the entries changed since, as NSOF. Two carriers:
- direct, over the host's sockets, found on the local network as printers
  are (mDNS);
- through a folder: each device writes its changes as files into a directory
  the user chooses (a shared disk, or any file-sync service's folder) and
  reads the others'. Slower, but works between devices that never meet.

**Conflicts**, when two devices changed the same entry:
- by slot: the later stamp wins for each top-level slot, so one device
  changing a card's phone and another its address both survive;
- a slot both changed: the later wins and the loser's value is kept in a
  `syncConflict` slot, shown as a small marker the user can tap to choose;
- an entry changed on one and deleted on the other: the change wins.

Long text edited on both sides is the hard case. A first version treats a
note's text as one slot (later wins, the other kept as above); merging text
character by character is a later step.

**What is synced.** Chosen per soup in a Sync panel. Default: Names, Dates,
Notes, the to-do list. Never: the In/Out Box, system preferences, the
`SyncLog` itself. Packages are not synced.

**Security.** Devices pair once, by a code shown on one and written on the
other, and exchange keys; every exchange is encrypted and authenticated
with them. Files in a sync folder are encrypted with the same keys.

## Relation to the dock

The dock stays as it is for desktops that speak it (NCX and the like). Sync
is separate; an entry changed by a dock restore is logged like any other.

## Tests

Two `newton` processes with their own stores on one machine: an add each
way, a change to different slots of one card (both kept), to the same slot
(later wins, marker shown), a delete against a change, a device back after
a long absence, a clock set wrong, an interrupted exchange resumed. The
folder carrier with a temporary directory.

## Owner decisions

1. The conflict rule above, or always keep both as two entries.
2. Direct, folder, or both first.
3. Whether 64-bit stamps justify the wide-integer store format that the
   64-bit flavour still lacks (`docs/next-steps.md`).

## State (2026-11-12, branch `modern`)

Built, behind the Host panel's "Sync" (`host/Features.h`'s `kFeatureSync`,
`NEWTON_SYNC`): `src/host/HostSync.ns` (the global `HostSync:host`) over
`host/HostSync.cpp`. The owner said to carry on without asking, so the
open decisions were taken as this page proposes, each easy to change:

- **Decision 1, the conflict rule:** as proposed above. Slot by slot the
  later stamp wins, the loser kept in the entry's `syncConflict`
  (`{slot: {value, stamp}}`). Changed against deleted, the change wins.
- **Decision 2, the carrier:** the folder first.
- **Decision 3, wide stamps:** not forced. A stamp is a string that sorts
  as it should - `SSSSSSSSSSSS.CCCCCC.DEVICE`: the host's seconds since
  1970 (`HostSyncSeconds`), a counter, the device's id - so no integer
  wider than NewtonScript's is needed.

How it works:
- **Identity:** each synced entry gets `syncID` (128 bits from the
  system's random generator, `HostSyncRandomID`), the same on every
  device. Each device gets an id of its own, made at its first sync.
- **The clock:** a hybrid logical clock. A device's next stamp is later
  than every stamp it has made or seen, so changes stay in order when a
  clock is wrong.
- **What each device keeps** (the soup `Sync:host` on its internal store):
  a record per synced entry - its soup, and for each top-level slot (not
  the `_` system slots, `syncID` or `syncConflict`) the value last
  synced, its stamp, and the stamp it replaced (`prev`). Deleted entries
  are kept as tombstones (`deleteStamp`, and `deletePrev`, the newest
  stamp the deleter had).
- **A sync** - when asked (`Sync()`), three seconds after a synced soup
  changes, and every `NEWTON_SYNC_EVERY` seconds (300; 0 never) - does
  three things:
  - it scans the synced soups and gives a new stamp to every slot whose
    value is not the record's. Applications need no change, and changes
    made while sync was off are found too;
  - it reads the other devices' files from the folder, as NSOF unflattened
    without code (`'unflattenNoCode`), skipping a file not rewritten
    since last time, and merges them;
  - it writes this device's whole state as `<device>.nsof` - a temporary
    file renamed over the old one, so another device never reads half of
    it.
- **The merge** is of states, not logs, so its result is the same in
  whatever order and however often it is done, and an exchange cut short
  is simply done again. A slot is taken when its stamp is later. It is a
  conflict when the loser's stamp is newer than the stamp the winner's
  value replaced - both changed it without seeing the other - and the
  values differ. A tombstone deletes the entry unless the entry has a
  stamp newer than the tombstone's `deletePrev`, and the other way round
  an entry comes back.
- **The folder:** `NEWTON_SYNC_FOLDER`, else "Newton Sync" in the home
  folder - a shared disk, or any file-sync service's folder.
- **Soups:** Names, Notes, Calendar, Repeat Meetings, To Do (`soups` in
  `HostSync.ns`).
- **Tests:** ctests `host.NewtonSync.A1` ... `.A3` and `.off`
  (`src/host/demo/sync-common.ns` describes the steps). Two devices - two
  stores - and one folder, five runs in turn:
  - an add each way;
  - a change to different slots of one card (both kept) and to the same
    slot (later wins, the loser in `syncConflict` on both devices);
  - a delete against a change (the change wins, and comes back to the
    deleting device);
  - a clock a day slow, whose change still orders after what that device
    has seen.

Not built (NOT YET):
- the direct carrier (mDNS and sockets);
- pairing and encryption - the folder must be one only the user's devices
  can read;
- change logs in place of whole states - each file holds every entry,
  fine for a personal set of data;
- tombstones forgotten once every device has seen them;
- the Sync panel - the soups are fixed;
- the marker in the applications for choosing a conflict's value;
- merging text character by character;
- an index on `syncID` (finding an entry is a walk of its soup);
- a first sync between devices that already share data. Each device's
  entries are new to the other, so two devices that both went through
  Setup each get the other's owner card, and the same card typed into
  both appears twice;
- a device back after a long absence and an interrupted exchange, which
  are not tested separately.
