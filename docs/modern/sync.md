# Sync: several Newtons, one set of data, no cloud owner

**Status: a proposal. The largest piece here; the conflict rule is the
owner's decision.**

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
