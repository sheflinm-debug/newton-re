# Arabic ink tools

Tools for collecting Arabic handwriting as pen strokes and replaying it
through the reconstruction's handwriting recognisers.  The work they serve
is described in [docs/recognition/arabic.md](../../docs/recognition/arabic.md).

| Tool | Purpose |
|---|---|
| `capture.html` | The capture page: prompts a word (the starter words, the commonest everyday words, phrases), takes stylus strokes (points, time, pressure; palm rejection by ignoring touch unless allowed), saves each sample. Served by `serve.py`; also publishable as a claude.ai artifact (capabilities `db` and `user`), where it saves to the artifact's own store with an anonymous id for each writer, so friends can write from their own devices (they need access that lets them use the page, not only view it). |
| `serve.py` | Serves `capture.html` to a phone or tablet on the local network and writes each sample to `build/arabic-ink/samples/` (git-ignored). |
| `to_ns.py` | Turns the samples into a NewtonScript replay script that hands each one to `Recognize()` with the cursive letter set; `--mirror` flips them left to right first. |
| `dte.py` | Decodes ParaGraph's letter table (DTEMain) into JSON and encodes it back; `check` proves the round trip on the ROM's table is exact; `show` prints one letter's variants and prototypes. |
| `adab.py` | Converts a downloaded copy of the ADAB database (`build/adab/`) into samples (`build/arabic-ink/adab/set_N/`). ADAB is not redistributed: see below. |
| `harvest.py` | Runs a directory of samples through the cursive reader in batches with `NEWTON_XR_DUMP` on, keeping the xrs the reader cut each sample into (`build/arabic-ink/xrs/<name>/`). |
| `xrs.py` | Reads a harvest back, joined to the samples' words (the builder's input); `summary` prints its statistics. |
| `arabic.py` | The registry of Arabic-script characters and the reader's codes for them (letters, hamza seats, vowel marks; other languages' letters reserved); a word's letters in their positional forms. |
| `build_dte.py` | Builds an Arabic letter table (DTEMain) from a harvest: letters aligned to the xrs, counts made nibbles on the ROM's scale. Load it with `NEWTON_DTE_MAIN=<file>`. |
| `train_net.py` | Trains the host's own Arabic word engine - a bidirectional GRU over the pen's trajectory, CTC - on the samples, measures it on held-out words, and exports it to ONNX for the Newton host. Needs `torch` (CPU build: `pip install torch --index-url https://download.pytorch.org/whl/cpu`) and `onnxruntime`. |
| `eval_net.py` | Reads a samples directory with a trained network through onnxruntime and scores the readings, freely and over a vocabulary: what the host engine will do, offline. |
| `audit.py` | The engine audited on the Newton itself: known samples (and phrases made of two) written with the pen onto the Notepad of a headless `newton`, the page read back and cleared, each case also read offline from its own points and from the exact screen points - so a loss shows where it happens (the network, the screen's size and grid, or the Newton side: grouping, ink, the paragraph). `build/venv/Scripts/python tools/arabic-ink/audit.py --words build/arabic-ink/samples --words build/arabic-ink/adab/set_3 --limit 60 --phrases build/arabic-ink/samples [--model M.onnx --anet M.anet]`; writes `build/arabic-ink/audit/audit.{ns,log,tsv,txt}`. |
| `fetch_lexicons.py` | Downloads the free Arabic word sources, standard and dialect, into `build/arabic-lex/sources/`, with a manifest of each one's URL, licence and SHA-256: Wiktionary as kaikki.org extracts it (Arabic; Egyptian, South and North Levantine, Hijazi, Gulf, Moroccan, Tunisian), the CAMeL Lab frequency lists (Modern Standard, dialect, Classical), drelhaj/Arabic-Dialects' per-dialect counts. About 1.1 GB, not committed. |
| `build_lexicon.py` | Merges them into `build/arabic-lex/arabic.lex`, the engine's composite lexicon: each word as written by hand (no vowel marks, look-alikes folded), its log frequency and the varieties it was found in (`m c e l g h n d`: Modern Standard, Classical, Egyptian, Levantine, Gulf, Hijazi, North African, dialect unspecified). Arabic is diglossic, so the varieties are kept apart and the engine can be told which to read by. |
| `lexdecode.py` | The decoder that reads the network's output by the lexicon (a CTC beam search over it), which the engine's `recognition/ArabicLexicon.cpp` mirrors step for step; scores sample directories with and without it; `--tune` chooses its constants on other writers (never the user's own samples, which are the end test); `--fixture` writes the check ctest `recognition.ArabicLexicon` holds the C++ to. |
| `fetch_ink.py` | Downloads the free online Arabic handwriting sets beyond ADAB (AHWDB1 and 2; Calliar, kept for reference only) into `build/arabic-ink/downloads/`. Arabic writing only: no Persian or other languages in the Arabic script (the network learns letter sequences as well as shapes). |
| `ink_sets.py` | Turns them into samples: `ink_sets.py ahwdb` writes `build/arabic-ink/ahwdb/` (160 writers' "محمد" and "محمد عبدالله", turned upright, strokes split where the clock jumps) and `ahwdb-held/` (the other 40, never trained on); `ink_sets.py writers` files other people's samples from the capture artifact's store (pulled by Claude with the ArtifactData tool into `build/arabic-ink/pulled/`) under `build/arabic-ink/writers/<anonymous id>/`. |
| `export_weights.py` | Writes a trained network as the one binary the host reads (`.anet`) and a check sample for ctest `recognition.ArabicNet`. Run the engine with `NEWTON_ARABIC_NET=<file>.anet`. |
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
