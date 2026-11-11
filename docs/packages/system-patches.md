# System patches: how the ROM is patched in place, and what an installable Arabic package needs

How a MessagePad 2x00 running the stock 2.1 ROM takes new code for its own
functions - the mechanism behind Apple's System Updates - established from
the ROM's disassembly (`analysis/romdisasm.py`; addresses are the US
ROM's), the DDK's `OS600/VirtualMemory.h` and `OS600/ROMExtension.h`,
`analysis/slotcallers.py`, and Apple's own System Update 2.1 (717260): its
package, and Jonathan Kipling Knight's 2000-01 rebuild of it from
disassembly, whose streams match Apple's byte for byte
(`tools/patch/fetch-717260.py` brings both to `build/updates/`, never
committed).  Why: the Arabic support (`docs/recognition/arabic.md`) is
wanted as a package that installs on any Newton (Einstein first), not only
as this reconstruction's built-in code.  Written 2026-10-27, the update
read 2026-11-11.

## The patch domain

Every exported ROM function is called through its slot in the **patchable
jump table** (`gTheROMPatchTable`, ROM 0x2000-0x1285c), which the MMU maps
at virtual 0x01A00000 with 32 slots to a 4 KB page: the 4 KB of second-level
page tables at ROM 0x16000 (`gROMPatchTablePageTable`) map each ROM page of
the table at 32 consecutive virtual pages, and at virtual page *i* the
128-byte **window** *i* mod 32 of that ROM page is the live one - slot
0x01A9CE44 is at ROM 0x2000 + (0x9C / 32) × 0x1000 + 0xE44.  The whole
**patch domain** is 8 MB from 0x01A00000 (`GetPatchDomainBase` 0x182fc8,
`GetPatchDomainSize` 0x182fd0): the jump table's 4 MB, in which the one
page of the **magic pointer table** (ROM 0x3AF000, 874 pointers to the
ROM's NewtonScript objects) is mapped the same windowed way at
0x01D80000-0x01D9F000 (pointer @n is word n + 1); then from 0x01E00000 four
1 MB windows, one per ROM extension (`kMaxROMExtensions` 4), for code the
REx supplies.

`UseROMJumpTables` (0x1832e8, from `InitCGlobals` at boot) builds it:
`BuildPatchTablePageTable(gROMPatchTablePageTable, kBaseROMId)` for the jump
table, then for each REx id 0..3 its `'ptpt'` config entry (`kPatchTablePageTableTag`,
a 1 KB second-level table; `'glpt'`, the "Gelato" table, instead when
`fROMBanksAreSplit`), or the ROM's default table at 0x17000 + id × 0x400
(every entry 0x0BAD0000) when the REx has none, for the window at
0x01E00000 + id MB.  The US ROM's REx (id 0) maps its whole window to one
page, its `'jump'` entry (`kCPatchTableTag`, ROM 0x7ee000, 72 bytes):
eighteen `b` instructions into the REx's own code (0x7a5690..0x7a5ea4,
the ScreenBuffer protocol's, the update's source says).  `PatchLoaderStub`
(0x3ae15c, 12 bytes: `add r1,pc,#4; mov r2,#0x1e00000; add pc,r2,#0xc`)
jumps to entry 3; nothing in the base ROM references the stub.

## The image, as the ROM keeps and installs it

A patch lives in the internal flash's **reserved block** (the first erase
region, `TReservedBlockAccessor`, `docs/stores/README.md`), after the
block's 0x10c-byte header, whose word at +0x5c is the patch's size
(`PatchSize` 0x1a74ec) and +0x60 its checksum.  `InstallPatch` (0x1830a0,
from `InitCGlobals` at every boot, after `ReadCalibration`) reads it:

| offset | size | what |
|---|---|---|
| 0 | 0x14 | five words, one per patch slot (base ROM + 4 REx): the patch's **version** there, copied to `gGlobalsThatLiveAcrossReboot.fPatchArray[i].fPatchVersion` (the "System Update" number Memory Info shows, through `kGestalt_PatchInfo`; 717260's is 0x00010005) |
| 0x14 | 0x2000 | the patch domain's **second-level page tables**: 0x800 entries, 4 KB each, for the 8 MB from 0x01A00000 - the jump table's 4 KB of tables, then the four REx windows' 1 KB each (`MapInPatch` 0x183064 hands them to `BuildPatchTablePageTable` exactly as `UseROMJumpTables` does the ROM's) |
| 0x2014 | n × 4 KB | the **pages** |

A size must be more than 0x2000, at most 0x23000 (`kMaxPatchPageCount` 34
pages, the two of tables among them), and 0x14 mod 4 KB.  A table entry is
an MMU small-page descriptor (flags 0xAAE: cacheable, bufferable, user
read-only).  One with bit 31 set is **a page of the image**: `(entry &
~bit31) >> 12` is the page's index in the whole image - the tables are
pages 0 and 1, the first code page is 2 - and `InstallPatch` rewrites it to
the physical page it got from `AllocatePatchRAM` (0x182fe0: RAM pages
tagged `'PDt0'`, kept out of the heap, mapped at a temporary address while
the patch is copied in), keeping the low 12 bits.  Any other entry is taken
as it is: the ROM's own page.  Then `MapInPatch`, the page count into
`fTotalPatchPageCount`, and `FirstCallAfterPatch` (slot 8812) - a `mov
pc,lr` in this ROM, which 717260 retargets to its own code.

**One image page can serve many virtual pages.**  Since the live window of
a jump-table page is the one its virtual page selects, a 4 KB patch page
holds up to 32 windows for 32 *different* virtual pages (windows 0..31 of
one page), each a copy of the ROM's window with some slots' `b`
retargeted: 717260's six code pages replace 57 jump-table pages (107
slots), ten windows of the magic pointer table (14 pointers, among them
the count: 874 for 873, a pointer added) and carry the new code, which the
same tables map at addresses such as 0x01AE0000, 0x01B40000, 0x01C11000
(`docs/packages/update-717260.md`, the listing).  The ROM keeps the
patches and their pages in `gGlobalsThatLiveAcrossReboot` (0x0c1061c4;
`fPatchArray[5]`, `fPatchPageArray[34]`, cleared by `ClearPatchInfo`
0x183384 to -1/0 and the sentinel 0xc0edb00b), which a warm reboot keeps.

A virgin block (`PatchSize` reads 0xdeadbeef) gets an empty patch written
first (`InitializeForWrite`, `WritePatch(nil, 0, nil)`).  **`WritePatch`**
(0x1a7914, slot 0x01a673ec: `(const void* data, long size, void* block)`)
is the only writer: it saves the block through its second copy, erases,
writes `size` bytes of `data` at +0x10c, the size and checksum into the
header, and verifies each step, rebooting on a mismatch; `block` is a
buffer of the erase region's size.  Nothing in the ROM calls it but
`InstallPatch`'s first-time write: the writing is the update package's.

## How a System Update installs: the package

717260 is a package of three NewtonScript (`'auto`) parts, "Base", "Rex0"
and "Trailer" (`Patch717260.s`; flags `kAutoRemovePkgFlag` - the package
goes once installed):

1. **Base** and **Rex0** each hold a stream binary of class `'code`
   (`ROMPatch.stream`, 0x8000 bytes: the 0x2000 of tables and six pages;
   `RExPatch.stream`, 0x2000: 4 KB of tables for the four REx windows and
   one page) and an `InstallScript` that, after checking the ROM
   (`Gestalt(kGestalt_SystemInfo)`: manufacturer 0x01000000, machine type
   0x10003000, ROM version 0x00020002, stage 0x8000; the REx's version
   0x00020000), calls a native `RegisterPatch(code, version, length,
   index)` (`RegisterPatch.s`, 1.9 KB) which locks a copy of the binary and
   writes its address, length and version straight into
   `gGlobalsThatLiveAcrossReboot.fPatchArray[index].fPatchPtr/fPatchCodeSize/fPatchNewVersion`
   (index 0 the base ROM, 1 the REx) - "registered but not installed yet".
   In their tables `0xFFFFFAAE` means "the ROM's entry" and `n << 12 |
   0xAAE` page *n* of this part (`page.a`'s `DontLoadPages`/`LoadPage`;
   `rommmu.s` is the whole 8 MB laid out).
2. **Trailer**'s `DoNotInstall` reads `Gestalt(kGestalt_PatchInfo)` and
   refuses an update already installed or older than the installed one
   ("The Newton can have only one system update installed": the image is
   the whole table), confirms "New System software is about to be
   installed. Please Tap Restart", and its `InstallScript` calls the native
   **`PatchMunger`** (1.3 KB) and, on success, `Reboot` five seconds later.

`PatchMunger` is the installer, in user mode through exported slots: it
sizes the image (0x14 + each registered part's length, less the 4 KB
table page of each part after the first), allocates it, writes the five
versions, copies the base part's tables and pages, each later part's
pages and its 1 KB window table into the master table, then **merges**:
each `0xFFFFF` page entry is replaced by the ROM's (`gROMPatchTablePageTable`
for the jump table, the REx's `'ptpt'` or the ROM's default for a
window), every other entry has its page index offset by the pages before
it and **bit 31 set**; clears the registered fields; and writes the image
with `TNewInternalFlash::ClassInfo()->MakeAt`, `InitForReservedBlock`,
`TReservedBlockAccessor::Init` and `WritePatch` (a buffer of
`GetEraseRegionSize` for the block).  The ROM's own `'ptch'` part path
(`CheckAndInstallPatch`, 0x15e578, keyed on `FillInSavedPatchInfoBlock` - a
stub in this ROM, like `FirstCallAfterPatch` and `BackupPatch`) is not used
by Apple's update; it is the path a *patched* ROM offers later updates,
and `BackupPatches`/`RestorePatchFromPipe` (Docker) its backup side.

Paul Guyot's 710031 (the one third-party system update that shipped) is
717260's package with 251 bytes changed - a fix inside a page.

## Can the Arabic support be a patch?

Yes.  `analysis/slotcallers.py build/MP2x00US MeasureGlyphWidths DrTextChunk
__ct__8LineLoop HandleWord__14TParagraphView OffsetToBounds__14TParagraphView
PointToOffset__14TParagraphView OffsetToCaret__14TParagraphView
PointInMarginsToOffset RegisterDomain__11TController` finds **no direct
branch to any of the bodies**: every call, the vtable entries included (the
`b`s in the class tables near 0x1d000, which the tool names after the
nearest symbol), goes through the slot.  The nine slots lie on seven
jump-table pages, whose windows fit in one image page; the code's pages are
what the image needs beyond 717260's.  The recogniser's network (400 KB
and more) does not belong in a patch: it is a native part of the same
package, registering its domain with `RegisterDomain` as the host's engine
does - the way Graffiti for Newton installed its recogniser.  A magic
pointer window can replace a ROM NewtonScript object (717260 replaces
fourteen): the Handwriting Recognition slip's form, say, for the "Arabic"
choice.

Because one image is the whole table, **the Arabic patch must carry
717260's content** (or 710031's) and add to it, or a Newton with the update
would lose it.  Apple's streams give that content exactly.

The route, then:

1. A **patch image builder** (`tools/patch/`, to write): from 717260's two
   streams, the slots to retarget and the ARM code to add, the three
   parts' streams (or the merged image for a check) - checked by
   `patchimage.py report` and a test that merges as `PatchMunger` does.
2. The ARM code: the host's `qd/ArabicText.cpp` and the `ParagraphView`
   changes built for `armv4` with zig/clang (`src/cmake/zig-arm-linux.cmake`
   is the nearest), calling the ROM through its slots (`armcpu/PublicJumpTable.cpp`
   has their addresses) - as 717260's pages do.
3. The **installer**: the update's own shape - two parts registering the
   streams with `RegisterPatch`, a trailer running `PatchMunger` and
   rebooting.  Apple's two natives (1.9 KB and 1.3 KB, ROM-specific through
   `0x0c1061c4` and `0x16000`) can be reused as they are, or rewritten in C
   from the reading above; the recogniser's part and the font come in the
   same package, which stays installed.
4. Tested on **Einstein**: the ROM under emulation, the flash a file.
   This reconstruction cannot run the patch's ARM code; it can check the
   image (1) and, once `InstallPatch` is reconstructed over a host reserved
   block, the plumbing.
