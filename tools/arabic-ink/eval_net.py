#!/usr/bin/env python3
"""Read a samples directory with a trained Arabic network and score it.

Purpose:  what the host engine will do, done offline: each sample's
          strokes made features (train_net.py's), the network run through
          onnxruntime, the reading decoded greedily and, with a vocabulary,
          as the vocabulary word the network scores best; the readings
          against the samples' words.
Inputs:   a model (train_net.py's .onnx, its .json beside it) and samples
          directories (serve.py's or adab.py's).
Outputs:  each sample's word and readings, then the letters wrong, the
          words right freely and, with --vocabulary, over the vocabulary.
Usage:    python tools/arabic-ink/eval_net.py build/arabic-ink/arabic-net.onnx
              build/arabic-ink/samples [--vocabulary build/arabic-ink/samples]
              [--limit N] [--quiet]
          (the venv's python: build/venv/Scripts/python)
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arabic           # noqa: E402
import train_net as T   # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("model")
    ap.add_argument("samples", nargs="+")
    ap.add_argument("--vocabulary")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    import onnxruntime as ort
    import torch
    with open(os.path.splitext(args.model)[0] + ".json", encoding="utf-8") as f:
        meta = json.load(f)
    alphabet = [c or None for c in meta["alphabet"]]
    index = {ch: i for i, ch in enumerate(alphabet) if ch is not None}
    T.STEP = meta.get("step", T.STEP)
    sess = ort.InferenceSession(args.model)
    vocabulary = None
    if args.vocabulary:
        vocabulary = sorted(set(w.replace(" ", "") for w, _ in T.load_dir(args.vocabulary)))
    data = []
    for d in args.samples:
        data += T.load_dir(d)
    if args.limit:
        data = data[:args.limit]
    chars = errors = words = right = vright = 0
    for word, strokes in data:
        target = word.replace(" ", "")
        feats = T.featurize(strokes)
        if feats is None:
            continue
        logits = torch.tensor(sess.run(None, {"features": [feats]})[0][0])
        text = T.greedy(logits, alphabet)
        vtext = T.vocabulary_decode(torch.log_softmax(logits, dim=-1), alphabet, vocabulary, index) if vocabulary else None
        errors += T.levenshtein(text, target)
        chars += len(target)
        words += 1
        right += text == target
        vright += vtext == target
        if not args.quiet:
            print("%-12s -> %-12s%s" % (target, text, ("  vocabulary: " + vtext + (" *" if vtext == target else "")) if vocabulary else ""))
    print("%d words: letters wrong %.1f%%, words right %.1f%%%s" % (
        words, 100.0 * errors / max(1, chars), 100.0 * right / max(1, words),
        ", with the vocabulary %.1f%%" % (100.0 * vright / max(1, words)) if vocabulary else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
