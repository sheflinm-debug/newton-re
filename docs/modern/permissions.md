# Permissions: what a package may read and send

**Status: a proposal. The enforcement question below is open and decides
how much of this is possible.**

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

## Owner decisions

1. Undeclared packages: the single question above, or silent full access
   as today.
2. Whether a first version that only records and displays grants, without
   enforcing, is worth shipping while the caller question is studied.
