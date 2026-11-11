#!/usr/bin/env python3
"""fetch-717260.py - Apple's System Update 2.1 (717260) and its rebuilt source, into build/updates/.

The update's package (`mp717260.pkg`), Paul Guyot's 710031 (the same package
with a fix, the one third-party system update that shipped), and Jonathan
Kipling Knight's 2000-01 rebuild of 717260 from its disassembly - the
assembler source of its pages, the NTK scripts of its three parts, and
Apple's original streams (`ROMPatch.stream`, `RExPatch.stream`, the
`PatchMunger` installer) which the rebuild reproduces byte for byte - from
the MIT mirror of the United Network of Newton Archives.  None of it is
committed (it is Apple's code); `docs/packages/system-patches.md` reads it.

Usage:   python tools/patch/fetch-717260.py [--out build/updates]
Output:  <out>/mp717260.pkg, 710031.pkg, mp717260.txt, nospatch.hqx and
         <out>/nospatch/<the source files> and <out>/nospatch/includes/
"""
import argparse
import os
import urllib.parse
import urllib.request

MIRROR = 'https://web.mit.edu/unna'
PATCHES = 'apple/software/SystemPatches/english/2x00'
SOURCE = 'development/source/AppleSysPatch'
FILES = {
    '': ['%s/mp717260.pkg' % PATCHES, '%s/710031.pkg' % PATCHES, '%s/mp717260.txt' % PATCHES,
         '%s/nospatch.hqx' % SOURCE],
    'nospatch': ['%s/NOS Patch/%s' % (SOURCE, f) for f in (
        '526167.txt', '717260.txt', 'Makefile', 'Patch1.s', 'Patch2.s', 'Patch3.s', 'Patch4.s',
        'Patch5.s', 'Patch6.s', 'Patch717260.s', 'PatchMunger', 'PatchMungerOriginal.stream',
        'RExMMU.s', 'RExPatch', 'RExPatch.make', 'RExPatch.pkg', 'RExPatch.s', 'RExPatch.stream',
        'RExPatch.txt', 'RExPatchOriginal.stream', 'RExStrmHead.s', 'ROMPatch', 'ROMPatch.make',
        'ROMPatch.pkg', 'ROMPatch.stream', 'ROMPatch.txt', 'ROMPatchOriginal.stream',
        'ROMStrmHead.s', 'RegisterPatch', 'RegisterPatch.aif', 'RegisterPatch.exp',
        'RegisterPatch.exp.a', 'RegisterPatch.ntkc', 'RegisterPatch.s', 'RegisterPatch.sym',
        'RegisterPatchOriginal.stream', 'System Update 2.1 (717260).pkg', 'common.txt', 'rommmu.s')],
    'nospatch/includes': ['%s/NOS Patch/includes/%s' % (SOURCE, f) for f in (
        'Bytecode.a', 'package.a', 'page.a', 'project.a', 'sound.a', 'stream.a')],
}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', default=os.path.join('build', 'updates'))
    a = ap.parse_args()
    for sub, paths in FILES.items():
        d = os.path.join(a.out, sub)
        os.makedirs(d, exist_ok=True)
        for p in paths:
            name = p.rsplit('/', 1)[1]
            dest = os.path.join(d, name)
            if os.path.exists(dest):
                continue
            url = MIRROR + '/' + urllib.parse.quote(p)
            with urllib.request.urlopen(url) as r:
                data = r.read()
            if data[:15].startswith(b'<!DOCTYPE'):
                print('not there: %s' % url)
                continue
            open(dest, 'wb').write(data)
            print('%s: %d bytes' % (dest, len(data)))


if __name__ == '__main__':
    main()
