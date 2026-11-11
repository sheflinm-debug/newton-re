#!/usr/bin/env python3
"""patchimage.py - a Newton system patch image read, and what it changes.

A system patch (docs/packages/system-patches.md) is the image the ROM keeps in
the internal flash's reserved block: five version words, then 0x2000 bytes of
second-level page tables for the 8 MB patch domain at 0x01A00000 (the jump
table's 4 MB, then four 1 MB ROM-extension windows), then the patch's pages.
A table entry is an MMU small-page descriptor; one whose page number is
0xFFFFF means "the ROM's own entry", any other names a page of the image
(0 and 1 are the tables themselves, the first code page is 2) - as the
update's installer (PatchMunger) merges them; the installer then sets bit 31
on those, and InstallPatch at boot moves them to the RAM pages it allocates.
One page of the image may be mapped at many virtual pages: the jump table
maps each ROM page 32 times, a different 128-byte window of 32 slots live
at each (the diagonal aliasing), so one patch page can replace a window of
up to 32 different table pages.

    patchimage.py report <build> <image> [--rex-only]
        <image> is a raw image (versions + tables + pages) or an NTK stream
        file holding one NSOF binary (`ROMPatch.stream`: tables + pages,
        no versions; `RExPatch.stream`: a 1 KB window table + pages).
        Lists each patched virtual page, which image page serves it, and
        for a jump-table page every slot whose branch differs from the
        ROM's, named from symbols.txt; for a magic-pointer-table page
        (0x01D80000 + n, ROM 0x3AF000 + n) the pointers that differ.
    patchimage.py raw <stream> <out>
        the NSOF binary's bytes written out.

Inputs:  <build>/rom.bin and <build>/symbols.txt
"""
import argparse
import bisect
import os
import struct

PATCH_BASE = 0x01A00000
PATCH_SIZE = 0x00800000
REX_BASE = PATCH_BASE + 0x400000		# the four 1 MB REx windows
ROM_TABLE = 0x16000				# gROMPatchTablePageTable: the jump table's 4 KB of tables
ROM_REX_TABLES = 0x17000			# the ROM's default REx window tables, 1 KB each
JUMP_TABLE = 0x2000				# gTheROMPatchTable, physical
MAGIC_TABLE = 0x3AF000				# the magic pointer tables, mapped at 0x01D80000
MAGIC_VBASE = 0x01D80000
NO_PAGE = 0xFFFFF
TABLE_BYTES = 0x2000
HEADER_BYTES = 0x14


def read_nsof_binary(data):
    """The one binary object of an NTK stream file: (class name, bytes)."""
    if data[0] != 2 or data[1] != 3:
        raise ValueError('not an NSOF stream holding a binary object')
    pos = 2
    size = data[pos]
    pos += 1
    if size == 0xFF:
        size = struct.unpack_from('>I', data, pos)[0]
        pos += 4
    if data[pos] != 7:
        raise ValueError('no class symbol')
    n = data[pos + 1]
    cls = data[pos + 2:pos + 2 + n].decode('ascii')
    pos += 2 + n
    return cls, data[pos:pos + size]


def load_image(path):
    """(versions, table words, pages bytes) from a raw image or a stream file."""
    data = open(path, 'rb').read()
    versions = None
    if data[:2] == b'\x02\x03':
        _, data = read_nsof_binary(data)
    else:
        versions = struct.unpack_from('>5I', data)
        data = data[HEADER_BYTES:]
    table = list(struct.unpack_from('>%dI' % (min(len(data), TABLE_BYTES) // 4), data))
    return versions, table, data


def load_symbols(build):
    code = []
    with open(os.path.join(build, 'symbols.txt'), encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 3 and p[1] == 'code' and '[jump table slot' not in line:
                try:
                    code.append((int(p[0], 16), p[2]))
                except ValueError:
                    pass
    code.sort()
    return code


def branch_target(word, at):
    if (word >> 24) & 0xFF not in (0xEA, 0xEB):
        return None
    imm = word & 0xFFFFFF
    if imm & 0x800000:
        imm -= 0x1000000
    return (at + 8 + imm * 4) & 0xFFFFFFFF


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('report')
    r.add_argument('build')
    r.add_argument('image')
    r.add_argument('--rex', action='store_true',
                   help='the image is a REx part: 4 KB of window tables, then its pages')
    w = sub.add_parser('raw')
    w.add_argument('stream')
    w.add_argument('out')
    a = ap.parse_args()

    if a.cmd == 'raw':
        cls, data = read_nsof_binary(open(a.stream, 'rb').read())
        open(a.out, 'wb').write(data)
        print('%s: %d bytes of class %s' % (a.out, len(data), cls))
        return

    rom = open(os.path.join(a.build, 'rom.bin'), 'rb').read()
    code = load_symbols(a.build)
    keys = [addr for addr, _ in code]

    def who(addr):
        i = bisect.bisect_right(keys, addr) - 1
        if i < 0:
            return '?'
        return code[i][1] + ('' if code[i][0] == addr else '+0x%x' % (addr - code[i][0]))

    versions, table, data = load_image(a.image)
    if versions:
        print('versions: ' + ' '.join('0x%08x' % v for v in versions))
    if a.rex:
        # a REx stream: 4 KB of tables for the four windows, then its pages (page 1 the first)
        entries = table[:0x400]
        vbase = REX_BASE
        page_bytes = 0x1000
    else:
        entries = table[:0x800]
        vbase = PATCH_BASE
        page_bytes = TABLE_BYTES
    rom_table = struct.unpack_from('>%dI' % 0x800, rom, ROM_TABLE)
    patched = 0
    for i, e in enumerate(entries):
        page = (e >> 12) & 0x7FFFF
        if page in (NO_PAGE, NO_PAGE & 0x7FFFF):		# (bit 31 set or not, the ROM's entry)
            continue
        patched += 1
        vaddr = vbase + i * 0x1000
        start = page * 0x1000
        new = data[start:start + 0x1000]
        if len(new) < 0x1000:
            print('0x%08x: image page %d (beyond the image: %d bytes)' % (vaddr, page, len(new)))
            continue
        if vbase == PATCH_BASE and i < 0x400:
            rom_entry = rom_table[i]
            rom_page = rom_entry >> 12
            old = rom[rom_page * 0x1000:rom_page * 0x1000 + 0x1000]
            window = (i % 32) * 128		# the window of this page live at this virtual page
            if rom_page * 0x1000 == MAGIC_TABLE:
                # the magic pointer table, windowed the same way: word 0 is the
                # count, pointer @n is word n + 1
                changed = []
                for k in range(32):
                    off = window + k * 4
                    o = struct.unpack_from('>I', old, off)[0]
                    n = struct.unpack_from('>I', new, off)[0]
                    if o != n:
                        changed.append((off // 4 - 1, o, n))
                print('0x%08x: image page %d, window %d of the magic pointer table (ROM 0x%x): %d pointers changed'
                      % (vaddr, page, i % 32, MAGIC_TABLE, len(changed)))
                for at, o, n in changed:
                    print('    @%d: 0x%08x -> 0x%08x%s' % (at, o, n, '' if at >= 0 else ' (the count)'))
                continue
            if not (JUMP_TABLE <= rom_page * 0x1000 < MAGIC_TABLE):
                diff = sum(1 for k in range(0, 0x1000, 4) if old[k:k + 4] != new[k:k + 4])
                print('0x%08x: image page %d replaces ROM page 0x%x (%d words differ)' % (vaddr, page, rom_page * 0x1000, diff))
                continue
            lines = []
            for k in range(32):
                off = window + k * 4
                o = struct.unpack_from('>I', old, off)[0]
                n = struct.unpack_from('>I', new, off)[0]
                if o != n:
                    slot = vaddr + off
                    ot, nt = branch_target(o, slot), branch_target(n, slot)
                    lines.append('    slot 0x%08x %s: -> 0x%08x %s' % (
                        slot, who(ot) if ot is not None else '(unused slot)', nt if nt is not None else n,
                        ('(ROM ' + who(nt) + ')') if nt is not None and nt < PATCH_BASE else ''))
            print('0x%08x: image page %d, window %d of ROM page 0x%x: %d slots retargeted'
                  % (vaddr, page, i % 32, rom_page * 0x1000, len(lines)))
            for l in lines:
                print(l)
        else:
            print('0x%08x: image page %d (a code page%s)' % (vaddr, page, ' of the REx windows' if a.rex else ''))
    print('%d virtual pages patched' % patched)


if __name__ == '__main__':
    main()
