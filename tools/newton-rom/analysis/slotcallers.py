"""slotcallers.py - who branches to a ROM function's body, and who to its jump-table slot.

A system patch replaces a page of the patchable jump table (docs/packages/
system-patches.md), so it catches every call that goes through a function's
slot and misses every direct branch to its body.  This scans the whole image
for `b`/`bl` instructions (vtable entries are `b`s) whose target is the body
or the slot of each function named, and names the function each one is in.
A function with no direct callers can be replaced by patching its slot's page.

Inputs:  <build>/rom.bin and <build>/symbols.txt (extract_rom.py, dump_symbols.py)
Output:  per function: the body's callers and the slot's callers, one per line
Usage:   python tools/newton-rom/analysis/slotcallers.py build/MP2x00US NAME... [--max N]
         NAME is a mangled symbol, a demangled one's leading part
         (`OffsetToCaret__14TParagraphView`), or a hex body address.
"""
import argparse
import bisect
import os
import struct


def load_symbols(build):
    code, slots = [], {}
    with open(os.path.join(build, 'symbols.txt'), encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) < 3:
                continue
            try:
                addr = int(p[0], 16)
            except ValueError:
                continue
            if p[1] != 'code':
                continue
            if '[jump table slot' in line:
                body = int(line.rsplit('->', 1)[1].strip(' ]\n'), 16)
                slots[body] = addr
            else:
                code.append((addr, p[2]))
    code.sort()
    return code, slots


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('build')
    ap.add_argument('names', nargs='+')
    ap.add_argument('--max', type=int, default=12, help='callers listed per target (default 12)')
    a = ap.parse_args()
    rom = open(os.path.join(a.build, 'rom.bin'), 'rb').read()
    code, slots = load_symbols(a.build)
    keys = [addr for addr, _ in code]

    def who(addr):
        i = bisect.bisect_right(keys, addr) - 1
        return '%s+0x%x' % (code[i][1], addr - code[i][0]) if i >= 0 else '?'

    targets = {}			# address -> label
    for name in a.names:
        try:
            body = int(name, 16)
            label = who(body)
        except ValueError:
            found = [(addr, sym) for addr, sym in code if sym == name or sym.startswith(name)]
            if len(found) != 1:
                print('%s: %s' % (name, 'not found' if not found else 'ambiguous: ' + ' '.join(s for _, s in found[:6])))
                continue
            body, label = found[0]
        targets[body] = '%s body' % label
        if body in slots:
            targets[slots[body]] = '%s slot' % label
        else:
            print('%s: no jump-table slot (not exported)' % label)

    hits = {t: [] for t in targets}
    for off in range(0, min(len(rom), 0x800000), 4):
        w = struct.unpack_from('>I', rom, off)[0]
        if (w >> 24) & 0xff not in (0xea, 0xeb):		# b / bl, always
            continue
        imm = w & 0xffffff
        if imm & 0x800000:
            imm -= 0x1000000
        t = off + 8 + imm * 4
        if t in hits:
            hits[t].append('%08x %s in %s' % (off, 'bl' if (w >> 24) == 0xeb else 'b ', who(off)))
    for t, label in targets.items():
        print('%s (0x%08x): %d direct' % (label, t, len(hits[t])))
        for h in hits[t][:a.max]:
            print('    ' + h)
        if len(hits[t]) > a.max:
            print('    ... %d more' % (len(hits[t]) - a.max))


if __name__ == '__main__':
    main()
