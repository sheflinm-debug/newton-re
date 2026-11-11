# Software the user can look inside and reshape

**Status: a proposal. The safety rules matter more than the features.**

## What Newton has

- Everything on the screen is a NewtonScript frame: a view's template, its
  proto chain, its methods. All of it can be read and changed while the
  system runs.
- The ROM has the compiler, and source compiled at run time is how the
  host's own panels load (`src/host/HostSettings.ns`).
- The reconstruction has a decompiler that turns any of the ROM's 5507
  functions back into source that compiles to the same function
  (`docs/frames/decompiler.md`), and the ROM's objects as editable source
  (`romsrc/`).
- An application is a few kilobytes.

No phone system offers this; on Newton it only needs tools.

## The aim

On the device, a user can ask "what is this?" of anything on the screen,
see how it works, change it, keep the change, undo it, and share it. And
ask for a change in words.

## The pieces

**1. Inspect.** A mode from the Extras drawer: tap any view and a slip shows
what it is - its name, its proto chain, its slots and values, and for a
method its source (decompiled where it came from the ROM or a package).
Values that are frames open in turn. Read-only.

**2. A workspace.** A note stationery, "Script": NewtonScript written or
typed in a note, a Run button, the result printed below. It runs in a
frame that inherits from the view last inspected, so `self` is that view.
This is the Newton Toolkit's inspector, on the device.

**3. Tweaks.** A change kept: an entry in a soup `Tweaks` with a name, what
it applies to (a global function, a proto's slot, an application's
template), the source, and whether it is on. Applied at start-up after the
ROM's objects are loaded, in order. Each records what it replaced, so
turning it off restores it without a restart where that is possible.
- A tweak is source, readable and editable in the Script stationery.
- The Action button routes a tweak like any note: beam it, mail it.
- A received tweak shows its source and what it touches before it can be
  turned on.

**4. Ask for it.** With the assist engine (`assistant.md`) a sentence -
"make the Names overview show the company under each name" - yields a
proposed tweak: its source and a plain statement of what it changes, for
the user to read, try and keep or discard. The model is given the
inspected view's frame and the decompiled source it would change. This is
the piece with the most promise and the least certainty: how well a model
writes NewtonScript is not yet known and needs measuring before anything is
promised.

**5. Small applications.** The same route for something new: a base view
built from the ROM's protos, kept as a tweak that adds an icon to Extras.
Packaging it as a real `.pkg` for a MessagePad is a later step
(`tools/cards/streamedpkg.py` builds packages on the desktop today).

## Safety

- **Safe start.** Holding the pen down at start (and `--no-tweaks`) loads
  no tweaks. A tweak that throws while being applied is turned off and
  reported, and the start goes on. The restart after a crash counts: three
  failed starts in a row is a safe start.
- **Try before keep.** "Try" applies a tweak until the next restart only.
- **Nothing in the ROM's data is overwritten**; tweaks are applied to the
  running copies, so removing every tweak is the original system.
- **Permissions** (`permissions.md`): replacing a function or a proto's
  slot is the strongest grant there is. A tweak the user wrote has it; a
  received one asks.
- **The 64-bit flavour and a MessagePad** may differ in what a tweak can
  assume; a tweak names the system version it was written on.

## Tests

Inspect: a known view answers its proto chain and a method's source that
compiles back to the same function. A tweak that replaces a global
function, on, off (the original restored), and across a restart. A tweak
that throws at start (turned off, the start completes). Three bad starts
(safe start). A tweak received by beam between two `newton` processes: not
on until accepted.

## Owner decisions

1. Whether the inspector is always available or behind a "developer"
   switch in the Host panel.
2. Whether "Ask for it" is in scope before the model's NewtonScript has
   been measured.
