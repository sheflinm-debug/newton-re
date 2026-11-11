# tools/patch - Newton system patches

Tools for the system patch mechanism (`docs/packages/system-patches.md`):
the image the ROM keeps in the internal flash's reserved block and maps over
its patchable jump table at boot, which is how Apple's System Updates - and
the Arabic support, as an installable package - change ROM functions.

    fetch-717260.py       Apple's System Update 2.1 (717260), Paul Guyot's 710031, and J. K. Knight's
                          rebuilt source of 717260 (Apple's original streams included) from the MIT
                          mirror of UNNA into build/updates/ (not committed);
                          python tools/patch/fetch-717260.py [--out DIR]
    patchimage.py         a patch image read and reported: each virtual page it replaces, the image
                          page serving it, the jump-table slots retargeted (named from symbols.txt)
                          and the magic pointers changed; `raw` writes an NTK stream's binary out;
                          python tools/patch/patchimage.py report build/MP2x00US IMAGE [--rex]
                          (docs/packages/update-717260.md is its output over 717260)

Needs `build/MP2x00US/rom.bin` and `symbols.txt` (tools/newton-rom); plain
Python 3.
