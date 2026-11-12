# Permissions: what a package may read and send

**Status: a first version built (2026-11-12) that records and shows
grants without enforcing them; the enforcement question below is open and
decides how much more is possible.**

## The problem

Any package can read and change every soup, and send anything anywhere.
That openness is what lets Find, filing and the Assistant work across all
applications, and it was acceptable on a device with no network. With a
network, an assistant and sync, it is not.

## The aim

Keep the shared data. Add consent: a package says what it needs, the user
grants it at install, and the system refuses the rest.

## What a package declares

A slot in its part frame, read by the installer:

    permissions: {
        soups: {|Names|: 'read, |Notes|: 'readWrite},   // by soup name
        ownSoups: ["MyApp:SIG"],                         // made by it, always allowed
        network: ['inet],                                // endpoints it may open
        send: true,                                      // may route through transports
        camera: nil, location: nil, assistant: nil,
    }

- A package always has full use of soups it creates itself.
- **A package with no declaration** (everything written before now) gets
  one install-time question: "This application was written for older
  Newtons and can read all your information. Install?" It then runs with
  everything but the network, which is asked for on first use.
- Grants are shown and changed in Extras, per package.

## Enforcement

The checks belong at few places: the soup lookups (`GetUnionSoup`,
`store:GetSoup` and what they return), endpoint instantiation, the routing
interface, and the host natives for the camera and the like.

**Open: who is calling?** All NewtonScript runs in one task with one heap,
so the system must work out which package the running code belongs to.
Two candidate ways, neither tried:
- By where the function lives: a package's objects lie in its own range of
  the store, so the interpreter's call stack names the package of each
  frame. A package function called by ROM code on the package's behalf is
  still found on the stack.
- By context: the base view of the application that is being sent the
  message. Simpler, but a deferred call or a soup-change callback loses it.

What this cannot stop: a package that replaces a ROM function, or modifies
a shared proto, runs later as "the system". So function replacement
(`DefGlobalFn` over an existing name, writes to ROM-proto slots held in
RAM) must itself be a permission. A package's ARM code runs only in the
interpreter (`src/armcpu/`), which already answers every system call it
makes, so it can be held to the same grants.

The honest level to claim: protection against careless and nosy packages,
and a clear record of what was granted; not containment of a determined
attacker until the caller question has a sound answer.

## Tests

A fixture package for each rule: one that reads Names without the grant
(refused, with the standard exception), one that makes and uses its own
soup (allowed), an undeclared one (the question shown), one that replaces
a global function (refused without the grant).

## State (2026-11-12, branch `modern`)

Built: the record and its display, the first version the owner chose
(decision 2 below), behind the Host panel's "Package permissions"
(`host/Features.h`'s `kFeaturePermissions`, `NEWTON_PERMISSIONS`).
`src/host/HostPermissions.ns` (the global `HostPermissions:host`, loaded
by `host/HostMarks.cpp` beside Marked):
- a package's declaration is the `permissions` slot of the first of its
  parts' frames that has one (`GetPkgRefInfo(pkgRef).parts`), as above;
- every package on a store gets one record in the soup
  `"Permissions:host"` of the internal store - `{package, declared,
  items: [{id, label, on, fixed}], recorded}` - made when it is installed
  (`RegSoupChange` on the Extras drawer's soup, @307's "Packages", whose
  package entries are those with a `pkgRef`) and, for packages installed
  while this was off, two seconds after boot and whenever the list opens;
  the record goes when the package does;
- what is declared is granted as installed; a package without a
  declaration is recorded as "written for older Newtons": every soup
  (`"all"`) granted, the network not (to be asked on first use);
- the Permissions application in Extras lists the packages and their
  grants, says at its top that nothing is refused yet, and a tap on a
  grant's box takes it away or gives it back;
- `Granted(packageName, id)` answers for the checks to come (ids:
  `"soup:"` and a soup name, `"ownSoups"`, `"network"`, `"send"`,
  `"camera"`, `"location"`, `"assistant"`, `"replaceFunctions"`).

Demo `src/host/demo/permissions.ns`, ctests `host.NewtonPermissions` and
`.off` (with the Monaco font package, which declares nothing).

Not built: any enforcement (the caller question above); the install-time
question for undeclared packages (decision 1 - with nothing enforced its
answer would change nothing, so it waits for enforcement); a fixture
package that declares permissions (the tests turn a declaration into
grants with `ItemsOf` directly: the repository has no tool that writes a
form package yet); the four fixture packages of Tests above.

Found on the way: the soup @307 names, "Packages", is the Extras drawer's,
not a list of packages - its package entries are the ones with a `pkgRef`
and a `packageName`; a symbol keeps the case of whoever interned it first
(`'|Names|` prints as "names", the ROM having `'names`), so a label asks
the soup for its own name.

## Owner decisions

1. Undeclared packages: the single question above, or silent full access
   as today.  (Open; nothing asks yet.)
2. Whether a first version that only records and displays grants, without
   enforcing, is worth shipping while the caller question is studied.
   (Decided 2026-11-12: yes - built, see State.)
