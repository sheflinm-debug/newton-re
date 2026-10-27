# System patches: how the ROM is patched in place, and what an installable Arabic package needs

How a MessagePad 2x00 running the stock 2.1 ROM takes new code for its own
functions - the mechanism behind Apple's System Updates - established from
the ROM's disassembly (`analysis/romdisasm.py`, addresses below are the US
ROM's), the DDK's `OS600/VirtualMemory.h` and `OS600/ROMExtension.h`, and
`analysis/slotcallers.py`.  Why: the Arabic support
(`docs/recognition/arabic.md`) is wanted as a package that installs on any
Newton (Einstein first), not only as this reconstruction's built-in code.
Written 2026-10-27.

## The patch domain

Every exported ROM function is called through its slot in the **patchable
jump table** (`gTheROMPatchTable`, ROM 0x2000-0x1285c), which the MMU maps
at virtual 0x01A00000 with 32 slots to a 4 KB page (the diagonal aliasing
`tools/newton-rom/README.md` describes): the 4 KB of second-level page
tables at ROM 0x16000 (`gROMPatchTablePageTable`) map the 4 MB
0x01A00000-0x01E00000.  The whole **patch domain** is 8 MB from 0x01A00000
(`GetPatchDomainBase` 0x182fc8, `GetPatchDomainSize` 0x182fd0): the jump
table in the lower half, and from 0x01E00000 four 1 MB windows, one per ROM
extension (`kMaxROMExtensions` 4), for code the REx supplies.

`UseROMJumpTables` (0x1832e8, from `InitCGlobals` at boot) builds it:
`BuildPatchTablePageTable(gROMPatchTablePageTable, kBaseROMId)` for the jump
table, then for each REx id 0..3 its `'ptpt'` config entry (`kPatchTablePageTableTag`,
a 1 KB second-level table; `'glpt'`, the "Gelato" table, instead when
`fROMBanksAreSplit`), or the ROM's default table at 0x17000 + id * 0x400
when the REx has none, for the window at 0x01E00000 + id MB.  The US ROM's
REx (id 0) maps its whole window to one page, its `'jump'` entry
(`kCPatchTableTag`, ROM 0x7ee000, 72 bytes): eighteen `b` instructions into
the REx's own code (0x7a5690..0x7a5ea4).  Nothing in the base ROM addresses
those entries except `PatchLoaderStub` (0x3ae15c, 12 bytes: `add r1,pc,#4;
mov r2,#0x1e00000; add pc,r2,#0xc` - entry 3), which nothing in the base ROM
references either; what calls them is open.

## A patch, as the ROM keeps and installs it

A patch lives in the internal flash's **reserved block** (the first erase
region, `TReservedBlockAccessor`, `docs/stores/README.md`), after the
block's 0x10c-byte header, whose word at +0x5c is the patch's size
(`PatchSize` 0x1a74ec) and +0x60 its checksum.  `InstallPatch` (0x1830a0,
from `InitCGlobals` at every boot, after `ReadCalibration`) reads it:

| offset | size | what |
|---|---|---|
| 0 | 0x14 | five words, one per patch slot (base ROM + 4 REx): the patch's **version** there, copied to `gGlobalsThatLiveAcrossReboot.fPatchArray[i].fPatchVersion` (the "System Update" number Memory Info shows, through `kGestalt_PatchInfo`) |
| 0x14 | 0x2000 | the patch domain's **second-level page tables**: 0x800 entries, 4 KB each, for the 8 MB from 0x01A00000 - the jump table's 4 KB of tables, then the four REx windows' 1 KB each (`MapInPatch` 0x183064 hands them to `BuildPatchTablePageTable` exactly as `UseROMJumpTables` does the ROM's) |
| 0x2014 | n × 4 KB | the **pages** |

A size must be more than 0x2000, at most 0x23000 (`kMaxPatchPageCount` 34
pages, the two of tables among them), and 0x14 mod 4 KB.  A page-table
entry with bit 31 set is relative to the patch: `(entry & ~bit31) >> 12` is
a page index into the whole image (the tables are pages 0 and 1, the first
code page is 2), the low 12 bits its MMU flags; `InstallPatch` rewrites it
to the physical page it got from `AllocatePatchRAM` (0x182fe0: RAM pages
tagged `'PDt0'`, kept out of the heap, mapped read-only at a temporary
address while the patch is copied in).  Other entries are taken as they
are (the ROM's own pages).  Then `MapInPatch`, the page count into
`fTotalPatchPageCount`, and `FirstCallAfterPatch` (slot 8812) - a `mov
pc,lr` in this ROM, there for a patch to replace.

So **to replace a function, a patch replaces the jump-table page its slot is
on**: a copy of the ROM's 4 KB page with that one slot's `b` retargeted to
code in another patch page, which the same page table maps somewhere in the
patch domain (a REx window, say).  A page holds 32 slots, so one page serves
every patched function whose slots share it.  The ROM keeps patches and
their pages in `gGlobalsThatLiveAcrossReboot` (0x0c1061c4;
`fPatchArray[5]`, `fPatchPageArray[34]`, both cleared by `ClearPatchInfo`
0x183384 to -1/0 and the sentinel 0xc0edb00b), which a warm reboot keeps.

A virgin block (`PatchSize` reads 0xdeadbeef) gets an empty patch written
first (`InitializeForWrite`, `WritePatch(nil, 0, nil)`).  **`WritePatch`**
(0x1a7914, slot 0x01a673ec: `(const void* data, long size, void* block)`)
is the only writer: it saves the block through its second copy, erases,
writes `size` bytes of `data` at +0x10c, the size and checksum into the
header, and verifies each step, rebooting on a mismatch; `block` is a
buffer the size of the reserved block.  Nothing else in the ROM calls it.

Does a patch survive a hard reset?  The reserved block is not the store the
reset erases, and the flash's `IsVirgin`/`InitForReservedBlock` path does
not touch it: it should.  Not yet confirmed on Einstein (below).

## The package path, and why it cannot work on this ROM as shipped

A package part of type `'ptch'` is the manager's own (`LoadNextPart` →
`CheckAndInstallPatch`, 0x15e578; `docs/packages/README.md`).  Its layout,
read off `CheckAndInstallPatch` and `BackupPatches` (0x161130, which
*writes* one - the Docker backup of an installed patch - with part flags
0x1b2 and the part named "Patch"):

- the part's **info** (`PartInfo.info`, the directory entry's info bytes)
  is an `SSavedPatchInfo`, 0x84 bytes: two words identifying the ROM, five
  words and five more per patch slot (REx ids and versions, presumably),
  the total page count at +0x30, and from +0x38 sixteen bytes per slot:
  version, page count, ...;
- the part's **data** is the pages, patch by patch.

`CheckAndInstallPatch` fills a block for this machine with
`FillInSavedPatchInfoBlock` (slot 8811), accepts the part when the ROM
words and the per-slot ids match and no slot's version is older and one is
newer, copies the data into a heap of the `'prot` domain, and registers
each slot's run of pages with `RegisterPatch` (0x11dffc, SWI 47 code 0
into `fPatchArray[i].fPatchPtr/fPatchCodeSize/fPatchNewVersion` - "registered
but not installed yet") and sets `fPatchInstalled` so the machine reboots.
But `FillInSavedPatchInfoBlock` is `mov pc,lr` in this ROM (0x1833fc; so
are `FirstCallAfterPatch` 0x1833f8 and `BackupPatch` 0x183400): the block
is never filled, the comparison is against whatever the stack held, and
nothing in the ROM turns a registered patch into a written one (no path
reaches `WritePatch`).  The stubs are slots meant to be replaced by the
first patch for later ones to use.  How Apple's own update got in first -
its package's own code, or a part the ROM handles another way - needs an
actual System Update package to answer (`docs/next-steps.md`).

`BackupPatches`/`SizeOfPatches` (Docker: the patch sent to the desktop as a
package) and `RestorePatchFromPipe`/`FRestorePatchPackage` (one put back on
the internal store as a 1.x package, `NewPackage`) are the backup side; the
reconstruction answers them as a machine with no patch.

## Can the Arabic support be a patch?

Yes.  `analysis/slotcallers.py build/MP2x00US MeasureGlyphWidths DrTextChunk
__ct__8LineLoop HandleWord__14TParagraphView OffsetToBounds__14TParagraphView
PointToOffset__14TParagraphView OffsetToCaret__14TParagraphView
PointInMarginsToOffset RegisterDomain__11TController` finds **no direct
branch to any of the bodies**: every call, the vtable entries included (the
`b`s in the class tables near 0x1d000, which the tool names after the
nearest symbol), goes through the slot.  The nine slots lie on seven
jump-table pages (0x1a88000, 0x1a98000, 0x1a9c000, 0x1a9d000, 0x1b4a000,
0x1b4b000, 0x1b62000), so a patch of seven replaced table pages plus the
code's pages, within the 32 the format allows (128 KB), covers the shaping,
the right-to-left paragraph and the caret.  The recogniser's network
(400 KB and more) does not belong in a patch: it is a native part of the
same package, registering its domain with `RegisterDomain` as the host's
engine does - the way Graffiti for Newton installed its recogniser.

The route, then:

1. A **patch image builder** (`tools/patch/`, to write): from the ROM's
   page tables (0x16000, 4 KB, and the REx's `'ptpt'`), the slots to
   retarget and the ARM code to add, the image above - checked by a test
   that re-reads it as `InstallPatch` would.
2. The ARM code: the host's `qd/ArabicText.cpp` and the `ParagraphView`
   changes built for `armv4` with zig/clang (`src/cmake/zig-arm-linux.cmake`
   is the nearest), calling the ROM through its slots (`armcpu/PublicJumpTable.cpp`
   has their addresses).
3. The **installer**: a package whose native part writes the image with
   `WritePatch` and reboots - since the ROM's own `'ptch'` path cannot accept
   one (above) - then stays on the store for Docker's backup.
4. Tested on **Einstein**: the ROM under emulation, the flash a file.
   This reconstruction cannot run the patch's ARM code; it can check the
   image (1) and, once `InstallPatch` is reconstructed over a host reserved
   block, the plumbing.
