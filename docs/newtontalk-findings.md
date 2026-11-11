# What the NewtonTalk list knows that bears on the reconstruction

Notes from the public NewtonTalk mailing list (newtontalk.net; web archive
at lists.newtontalk.net/pipermail/newtontalk/). Each item gives the thread's
subject and date so it can be checked. These are community statements, not
ROM evidence. Where the repo has confirmed or contradicted one, that is
noted. The Wi-Fi history is in `docs/comms/wireless-history.md`.

## Design intent from Apple engineers

* **MMU domains were designed for NewtonOS.** Walter Smith of the Newton team
  said (Hacker News, 2022-03-21, relayed in "ARM MMU support for NewtonOS",
  2022-03-22) that Apple helped ARM define the first ARM MMU and added
  *domains*. Domains are protection settings that can be swapped quickly at
  a context switch, so tasks share one address space with different
  read/write rights. Most other ARM customers did not use them, and later
  ARM architectures dropped them. This is why the kernel writes the domain
  access register on every task switch (`docs/host-runtime.md`). On a modern
  CPU it would have to become separate page tables or address spaces.
* **Landon Dyer** worked on the kernel and storage on the original team
  ("In search of ... Landon Dyer", 2026-07-25). He could be asked about
  facts the ROM cannot settle.

## ROM extensions (REx) and licensees

From "ROM patches, ROM Extensions, and Watson" (Matthias Melcher, 2022-03-21):
* NewtonOS 2 was split for licensing. The core ROM has no apps or drivers.
  *REx0* holds Apple's apps and the drivers for the MP2x00/eMate. A licensee
  adds REx1-3, and a driver of the same name in a higher-numbered REx
  replaces the lower one. Einstein's REx works this way: it replaces the
  screen driver to allow larger screens.
* Apple gave licensees a paper listing the addresses where the core looks
  for extensions. Matthias's flash ROM boards put flash at exactly those
  addresses.
* Schlumberger's Watson ROM: Apple did the French translation in the core,
  and Schlumberger's REx1 replaces only the check that decides which
  packages may be installed. Matthias gives a two-instruction patch at
  0x007F3284 that turns it off (Watson ROM only).
* "Einstein and newton-re roadmap ideas" (2026-10-24) notes that Einstein's
  REx is a working example of a hardware abstraction layer.

## System patches

* Eckhart Köppen's Y2K10/712000 patches redirect jump-table entries, overlay
  code blocks, remap memory and patch data. His write-up is at
  40hz.org/Pages/newton/hacking/newton-patches/. The groundwork was Jonathan
  Kipling Knight's (Prism Research) reverse engineering of the 717260 patch
  ("712000 patch", 2025-01-12). This agrees with `docs/packages/system-patches.md`.
* **Network cards fail on an unpatched US ROM.** Matthias reported
  ("Einstein: New desktop release 2026.10.11", 2026-10-11) that NIE network
  cards fail with error -48807 on a US ROM 2.1 (717006) that lacks the 717260
  update, and work once Apple's mp717260.pkg is installed. The cause is in the ROM
  (`docs/curiosities.md`, "A card the ROM does not know reads a variable
  that is not there"): `WeirdCardInserted` reads an undefined variable
  `cardType`, and 717260's replacement reads the card type instead. The
  `InstallFormPart` -48807 bug, which also involves cards, is *not* fixed
  by 717260: none of the update's objects mentions `InstallFormPart` or
  `deviceNumber`.

## Memory

From "Question about Einstein (again)..." (Matthias Melcher, 2026-10-13):
the ROM is 8 MB and Einstein's REx adds 512 KB. RAM is 1 MB on the MP2000 and
4 MB on the MP2100; Einstein is gaining 8 and 15 MB. Internal flash is 4 MB.
NewtonOS also supports a second flash bank the hardware never fitted, which
Einstein uses for 8 MB in all. There is a hidden flash page that can switch
on another 4 or 8 MB. Linear PC Cards are limited to 64 MB.

## Debugging the ROM on a real device

"Hammer (Mac) and Debugger Connection (Newton)" (2026-01-08): Apple's Hammer
debugger talks to a MessagePad over serial. The Newton runs the *Debugger
Connection* package (called TMChooser in the documentation). The Mac needs
the "Senior CirrusNoDebug" image and its "high" file, from the Lantern DDK.
Its preliminary manual is on UNNA in the developer section. Einstein's
"Dumping the ROM" wiki page describes the same setup. This is how a real
device could serve as a reference for checking behaviour.

## Where the community sees this project going

In "Einstein and newton-re roadmap ideas" (2026-10-24 to 10-29):
* Eckhart Köppen and Matthias Melcher see Einstein as the faithful emulator,
  the "museum", and newton-re as the platform for daily use on new hardware.
  Eckhart plans a slimmed-down, hardware-faithful emulator based on Einstein.
* Matthias suggests treating NewtonOS like a bytecode platform, as Java is:
  the host OS supplies the operating system, and NewtonScript gets access to
  it. A `.pkg` could run on its own in a host window, with SDL as the only
  layer underneath. He also raises soups as a security issue: should each
  app have its own soups? Eckhart suggests storing soups in SQLite.
* Tools: Matthias's `newtc` compiler, decompiler and debugger (with VS Code
  debugging through DAP) and VSNewt are in github.com/MatthiasWM/newton-framework
  and github.com/MatthiasWM/VSNewt. `newtc` connects to real devices and to
  Einstein.
* Jeff Sheldon proposes that newton-re be renamed and kept separate from
  earlier efforts to avoid legal trouble, and that the Assistant be a
  starting point for modernization, given today's AI.
* The ROM source has been requested from Apple several times. Apple is said
  not to be unwilling, but legal issues outside its control stand in the way
  ("Can the Newton's ARM processor ever be upgraded?", 2025-11-30).
