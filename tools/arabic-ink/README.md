# Arabic ink tools

Tools for collecting Arabic handwriting as pen strokes and replaying it
through the reconstruction's handwriting recognisers.  The work they serve
is described in [docs/recognition/arabic.md](../../docs/recognition/arabic.md).

| Tool | Purpose |
|---|---|
| `capture.html` | The capture page: prompts a word, takes stylus strokes (points, time, pressure; palm rejection by ignoring touch unless allowed), saves each sample. Served by `serve.py`; also publishable as a claude.ai artifact, where it saves to the artifact's own store. |
| `serve.py` | Serves `capture.html` to a phone or tablet on the local network and writes each sample to `build/arabic-ink/samples/` (git-ignored). |
| `to_ns.py` | Turns the samples into a NewtonScript replay script that hands each one to `Recognize()` with the cursive letter set; `--mirror` flips them left to right first. |
| `dte.py` | Decodes ParaGraph's letter table (DTEMain) into JSON and encodes it back; `check` proves the round trip on the ROM's table is exact; `show` prints one letter's variants and prototypes. |
| `adab.py` | Converts a downloaded copy of the ADAB database (`build/adab/`) into samples (`build/arabic-ink/adab/set_N/`). ADAB is not redistributed: see below. |
| `harvest.py` | Runs a directory of samples through the cursive reader in batches with `NEWTON_XR_DUMP` on, keeping the xrs the reader cut each sample into (`build/arabic-ink/xrs/<name>/`). |
| `xrs.py` | Reads a harvest back, joined to the samples' words (the builder's input); `summary` prints its statistics. |
| `arabic.py` | The registry of Arabic-script characters and the reader's codes for them (letters, hamza seats, vowel marks; other languages' letters reserved); a word's letters in their positional forms. |
| `build_dte.py` | Builds an Arabic letter table (DTEMain) from a harvest: letters aligned to the xrs, counts made nibbles on the ROM's scale. Load it with `NEWTON_DTE_MAIN=<file>`. |
| `train_net.py` | Trains the host's own Arabic word engine - a bidirectional GRU over the pen's trajectory, CTC - on the samples, measures it on held-out words, and exports it to ONNX for the Newton host. Needs `torch` (CPU build: `pip install torch --index-url https://download.pytorch.org/whl/cpu`) and `onnxruntime`. |
| `report.py` | Summarises the logs of replay runs: the reader's result codes and each sample's readings. |

Each tool's header gives its inputs, outputs and exact invocation.

## A complete run, from a clean checkout

```powershell
# 1. collect samples (open the printed address on the phone, same Wi-Fi)
python tools\arabic-ink\serve.py

# 2. make the replay scripts
python tools\arabic-ink\to_ns.py
python tools\arabic-ink\to_ns.py --mirror

# 3. replay them (the build and ROM extraction as in the top-level README)
$env:NEWTON_TRACE_CURSIVE = '1'
build\host\host\newton --rom build/MP2x00US/rom.bin --display 320x480 --script build/arabic-ink/replay.ns --headless 400 *> build\arabic-ink\replay.log
build\host\host\newton --rom build/MP2x00US/rom.bin --display 320x480 --script build/arabic-ink/replay-mirrored.ns --headless 400 *> build\arabic-ink\replay-mirrored.log

# 4. compare
python tools\arabic-ink\report.py build\arabic-ink\replay.log build\arabic-ink\replay-mirrored.log
```

## Sample format

One JSON object per sample: `word`, `set` (words, letters or free),
`strokes` (one flat array per stroke: x, y, t, pressure repeated; x and y in
CSS pixels from the slate's top left, t in milliseconds from the sample's
first point), `slate` (`width`, `height`, and the guide lines `topLine` and
`baseLine`, which `to_ns.py` scales by), `devicePixelRatio`,
`pointerTypes`, `userAgent`, `createdAt`.

## ADAB

The ADAB database (the Arabic handwriting DataBase of the IfN, TU
Braunschweig, and REGIM, University of Sfax) is open access on
[IEEE DataPort](https://ieee-dataport.org/open-access/adab-database) for
non-commercial research.  Downloading it needs a free IEEE account, and
its terms ask that its use be acknowledged by citing the references the
DataPort page lists, among them:

- H. El Abed, M. Kherallah, V. Märgner, A. M. Alimi, "On-line Arabic
  handwriting recognition competition: ADAB database and participating
  systems", International Journal on Document Analysis and Recognition
  (IJDAR), 2011.
- the 2011 ICDAR paper and the 2012 Springer chapter on the same page.

It is not redistributed with this repository.  Unpack `ADAB DATABASE.zip`
so that `build/adab/set_1/inkml/` exists, then:

```powershell
python tools\arabic-ink\adab.py                                   # 15158 samples, about 7 minutes
python tools\arabic-ink\harvest.py build\arabic-ink\adab\set_1    # and set_2, set_3: about 10 minutes each
python tools\arabic-ink\xrs.py summary build\arabic-ink\xrs\set_1
```
