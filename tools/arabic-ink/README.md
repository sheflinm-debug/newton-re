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
