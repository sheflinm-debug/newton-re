# Software the user can look inside and reshape

**Status: a proposal; a first version built (2026-11-12) - Inspect, the
Script line and tweaks with the safe start (State). The safety rules matter
more than the features.**

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

## State (2026-11-12, branch `modern`)

Built, behind the Host panel's "Look inside" (`host/Features.h`'s
`kFeatureLookInside`, `NEWTON_LOOK_INSIDE`): `src/host/HostLookInside.ns`
(the global `HostLookInside:host`) over `host/HostLookInside.cpp`. The
owner had said to carry on without asking, so both decisions were taken
the cautious way: the inspector is behind a switch, off unless asked for
(decision 1), and "Ask for it" is left out until a model's NewtonScript
has been measured (decision 2).

- **Inspect** (Extras): the next tap on anything picks the deepest view
  under it, and a slip shows what it is:
  - a line saying what it is (a view or a frame, its application, view
    class, title);
  - its proto chain, each proto described with its slot count;
  - its slots and their values in a few words.
  A frame, an array or a function underlined opens in turn when tapped,
  and the title line goes back. A function shows its source: for the
  ROM's, the decompiled source from the checkout's `romsrc/` tree
  (`HostSourceOf`; read on the first ask - 5480 functions, matched by the
  ROM address each file is headed with, through `TranslateROMRef`), and a
  tweak's own source. The ROM's `StandardFind` comes back as its source,
  which compiles again. Read-only.
- **The Script line** at the foot of the slip runs NewtonScript against
  the thing inspected: its slots and methods by their bare names
  (`viewBounds`, `:ChildViewFrames()`), the thing itself as `inspected`.
  The answer, or the error, is shown below.
- **Tweaks:** entries of the soup "Tweaks:host" - `{name, kind, target,
  slot, source, on, order, received, lastError, written}`.
  - `'function` replaces or adds a global function (`DefGlobalFn`).
  - `'slot` sets a slot of the frame its target's source evaluates to.
  - They are applied at start, in order, to the running system. A slot of
    a read-only ROM frame cannot be set, so such a tweak fails rather than
    change the ROM's data.
  - Each keeps what it replaced, so turning it off puts that back at once
    (`UnDefGlobalFn` for a function that was new).
  - The API: `AddTweak`, `SetOn`, `TryOut` (until the next start),
    `RemoveTweak`. `ReceiveTweak` keeps a tweak from elsewhere off, saying
    what it changes, until `Accept`.
- **Safety:**
  - A tweak that throws while it is applied is turned off with its reason
    in `lastError`, and the start goes on.
  - `newton --no-tweaks` and `NEWTON_NO_TWEAKS` load none.
  - Three starts in a row that never finished make a safe start. The
    count is kept in `NEWTON_STARTS_FILE`, else `.newton-starts` in the
    home folder; a start counts as finished three seconds after its
    tweaks are applied.
- **Tests:** ctests `host.NewtonLookInside.1` ... `.4` and `.off`
  (`src/host/demo/look-common.ns` describes the steps). One store, four
  runs:
  - the inspector on the Notepad, and a tap through the catcher;
  - a ROM method's source;
  - the Script line;
  - tweaks: on, off with the original back, a refused one, a received
    one kept off until accepted;
  - across a restart, one that throws at start turned off;
  - three unfinished starts, then a safe start;
  - `--no-tweaks`.

Not built (NOT YET):
- the pen held down at start as a safe start;
- the Script stationery in the Notepad (a line in the inspector instead);
- routing and beaming a tweak, and the test between two `newton`
  processes;
- "Ask for it";
- small applications kept as tweaks;
- the permissions a received tweak should ask for (`permissions.md`'s
  `replaceFunctions`);
- the source of a package's or a native function;
- tweaks to an application's template before it opens;
- the system version a tweak names is recorded but not compared.

Found on the way, not yet looked into:
- In code that `Compile` makes at run time, the `self` keyword is not
  bound to the receiver of a message, though bare slot names do find the
  receiver's slots. A function literal in compiled source bound it
  correctly. This may be a difference in the reconstruction's compiler,
  to be checked against the ROM's.
- `try` and `to` are NewtonScript keywords, so neither can name a method
  or a local.
