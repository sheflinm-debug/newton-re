#!/usr/bin/env python3
"""Train a small network that reads Arabic words from pen strokes, and
measure it.

Purpose:  the host's own Arabic word engine (docs/recognition/arabic.md,
          "A host engine"): a bidirectional GRU over the pen's trajectory,
          trained with CTC on ADAB and the user's samples, exported to ONNX
          for the Newton host to run.  This script trains it and reports
          how well it reads words it has not seen, so the approach is
          proved before anything is wired into the OS.
Inputs:   samples directories (adab.py's and serve.py's): each sample's
          strokes as [x, y, t, pressure, ...] and its word.  The word's
          characters are arabic.py's registry (marks included); a sample
          whose word has an unregistered character is skipped.
Outputs:  -o <model>.onnx (the network, inputs "features" [1, T, 6] float
          and output "logits" [1, T, classes]), <model>.json (the
          alphabet: class index -> character, 0 the CTC blank, and the
          feature scaling), and on stdout each epoch's loss and the
          validation set's character error rate and word accuracy, greedy
          and, with --vocabulary, over the vocabulary's words.
Usage:    python tools/arabic-ink/train_net.py --train build/arabic-ink/adab/set_1
              build/arabic-ink/adab/set_2 --validate build/arabic-ink/adab/set_3
              -o build/arabic-ink/arabic-net.onnx [--epochs 20] [--hidden 128]
              [--vocabulary build/arabic-ink/adab] [--limit N]
          Needs torch (pip install torch --index-url
          https://download.pytorch.org/whl/cpu) and, to check the export,
          onnxruntime.

Features, per point, after the word is scaled so its height (the 10th to
90th percentile of y) is 1 and its x runs left to right in reading order
(the strokes are mirrored, as the Newton's reader is given them):
dx, dy (to the previous point), the pen down flag, 1 at a stroke's
first point, 1 at its last, and y above the base line (the 75th
percentile of y).  A stroke's points are resampled to a step of 0.05
of the height (STEP) so slow and fast writing look alike.
"""

import argparse
import glob
import json
import math
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arabic   # noqa: E402

STEP = 0.1
FEATURES = 6


def log(msg):
    print(msg, flush=True)


# ---------------------------------------------------------------- data

def load_dir(path):
    out = []
    for f in sorted(glob.glob(os.path.join(path, "**", "*.json"), recursive=True)):
        with open(f, encoding="utf-8") as h:
            d = json.load(h)
        word = arabic.fold(d.get("word", ""))
        if not word or not d.get("strokes"):
            continue
        out.append((word, d["strokes"]))
    return out


def percentile(values, q):
    v = sorted(values)
    return v[min(len(v) - 1, max(0, int(round(q * (len(v) - 1)))))]


def featurize(strokes):
    """The strokes as a sequence of feature vectors (a list of FEATURES-long
    lists); None when there is nothing to read."""
    pts = [(s[i], s[i + 1]) for s in strokes for i in range(0, len(s), 4)]
    if len(pts) < 2:
        return None
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    top, base = percentile(ys, 0.1), percentile(ys, 0.9)
    height = max(base - top, 1.0)
    baseline = percentile(ys, 0.75)
    right = max(xs)
    seq = []
    prev = None
    for s in strokes:
        raw = [((right - s[i]) / height, (s[i + 1] - baseline) / height) for i in range(0, len(s), 4)]   # mirrored, scaled
        # resample to an even step along the stroke
        pts2 = [raw[0]]
        acc = 0.0
        for a, b in zip(raw, raw[1:]):
            d = math.hypot(b[0] - a[0], b[1] - a[1])
            if d == 0:
                continue
            acc += d
            while acc >= STEP:
                acc -= STEP
                t = 1 - acc / d if d else 1
                pts2.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
        if pts2[-1] != raw[-1]:
            pts2.append(raw[-1])
        for k, p in enumerate(pts2):
            dx, dy = (p[0] - prev[0], p[1] - prev[1]) if prev is not None else (0.0, 0.0)
            seq.append([dx, dy, 1.0, 1.0 if k == 0 else 0.0, 1.0 if k == len(pts2) - 1 else 0.0, -p[1]])
            prev = p
    return seq


def make_alphabet():
    chars = list(arabic.TO_CODE.keys())
    return [None] + chars          # 0: the CTC blank


# ---------------------------------------------------------------- the net

def build_model(torch, hidden, classes):
    nn = torch.nn

    class Net(nn.Module):
        def __init__(self):
            super().__init__()
            self.inp = nn.Linear(FEATURES, hidden)
            self.gru = nn.GRU(hidden, hidden, num_layers=2, batch_first=True, bidirectional=True, dropout=0.2)
            self.out = nn.Linear(2 * hidden, classes)

        def forward(self, x):
            h = torch.relu(self.inp(x))
            h, _ = self.gru(h)
            return self.out(h)

    return Net()


def greedy(logits, alphabet):
    """CTC greedy decoding of one sequence's logits (T x classes)."""
    best = logits.argmax(dim=-1).tolist()
    out = []
    prev = 0
    for c in best:
        if c != prev and c != 0:
            out.append(alphabet[c])
        prev = c
    return "".join(out)


def levenshtein(a, b):
    if len(a) < len(b):
        a, b = b, a
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def vocabulary_decode(logprobs, alphabet, vocabulary, index):
    """The vocabulary word whose CTC score over the logits is best: a
    forward (alpha) pass per word over the collapsed label; the words
    narrowed first by the greedy reading's edit distance (the 40 nearest)."""
    import torch
    greedy_text = greedy(logprobs, alphabet)
    near = sorted(vocabulary, key=lambda w: levenshtein(w, greedy_text))[:40]
    T = logprobs.shape[0]
    best, best_s = None, None
    lp = logprobs
    for w in near:
        labels = [index[ch] for ch in w if ch in index]
        if not labels or 2 * len(labels) + 1 > T:
            continue
        ext = [0]
        for l in labels:
            ext += [l, 0]
        S = len(ext)
        alpha = torch.full((S,), -1e9)
        alpha[0] = lp[0, 0]
        if S > 1:
            alpha[1] = lp[0, ext[1]]
        for t in range(1, T):
            new = alpha.clone()
            for s in range(S):
                cand = alpha[s]
                if s > 0:
                    cand = torch.logaddexp(cand, alpha[s - 1])
                if s > 1 and ext[s] != 0 and ext[s] != ext[s - 2]:
                    cand = torch.logaddexp(cand, alpha[s - 2])
                new[s] = cand + lp[t, ext[s]]
            alpha = new
        score = torch.logaddexp(alpha[S - 1], alpha[S - 2]) if S > 1 else alpha[S - 1]
        if best_s is None or score > best_s:
            best, best_s = w, score
    return best or greedy_text


def evaluate(torch, model, data, alphabet, index, vocabulary=None, limit=0):
    model.eval()
    chars = errors = words = right = vright = 0
    with torch.no_grad():
        for word, feats in (data[:limit] if limit else data):
            x = torch.tensor([feats], dtype=torch.float32)
            logits = model(x)[0]
            text = greedy(logits, alphabet)
            errors += levenshtein(text, word)
            chars += len(word)
            words += 1
            right += text == word
            if vocabulary:
                vright += vocabulary_decode(torch.log_softmax(logits, dim=-1), alphabet, vocabulary, index) == word
    return errors / max(1, chars), right / max(1, words), vright / max(1, words)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--train", nargs="+", required=True)
    ap.add_argument("--validate", nargs="+", required=True)
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--limit", type=int, default=0, help="train on this many samples only")
    ap.add_argument("--validate-limit", type=int, default=500)
    ap.add_argument("--vocabulary", help="a samples directory whose words are the vocabulary the validation also decodes over")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    import torch
    torch.manual_seed(args.seed)
    torch.set_num_threads(os.cpu_count() or 4)
    random.seed(args.seed)

    alphabet = make_alphabet()
    index = {ch: i for i, ch in enumerate(alphabet) if ch is not None}

    def prepare(dirs):
        out = []
        for d in dirs:
            for word, strokes in load_dir(d):
                if any(ch not in index and ch != " " for ch in word):
                    continue
                feats = featurize(strokes)
                if feats is None or len(feats) < 2 * len(word) + 1:
                    continue
                out.append((word.replace(" ", ""), feats))
        return out

    train = prepare(args.train)
    random.shuffle(train)
    if args.limit:
        train = train[:args.limit]
    valid = prepare(args.validate)
    random.shuffle(valid)
    vocabulary = None
    if args.vocabulary:
        vocabulary = sorted(set(w.replace(" ", "") for w, _ in load_dir(args.vocabulary)))
    log("%d training samples, %d validation, %d classes%s" % (len(train), len(valid), len(alphabet),
        ", vocabulary of %d words" % len(vocabulary) if vocabulary else ""))

    model = build_model(torch, args.hidden, len(alphabet))
    opt = torch.optim.Adam(model.parameters(), lr=2e-3)
    ctc = torch.nn.CTCLoss(blank=0, zero_infinity=True)
    for epoch in range(1, args.epochs + 1):
        model.train()
        random.shuffle(train)
        total = 0.0
        started = time.time()
        for b in range(0, len(train), args.batch):
            batch = train[b:b + args.batch]
            T = max(len(f) for _, f in batch)
            x = torch.zeros((len(batch), T, FEATURES))
            lengths = []
            targets = []
            tlens = []
            for i, (word, feats) in enumerate(batch):
                x[i, :len(feats)] = torch.tensor(feats)
                lengths.append(len(feats))
                targets += [index[ch] for ch in word]
                tlens.append(len(word))
            logp = torch.log_softmax(model(x), dim=-1).transpose(0, 1)      # T x N x C
            loss = ctc(logp, torch.tensor(targets), torch.tensor(lengths), torch.tensor(tlens))
            opt.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()
            total += loss.item() * len(batch)
        cer, acc, vacc = evaluate(torch, model, valid, alphabet, index, vocabulary if epoch == args.epochs or epoch % 5 == 0 else None, args.validate_limit)
        log("epoch %2d: loss %.3f, validation: letters wrong %.1f%%, words right %.1f%%%s (%.0f s)"
            % (epoch, total / max(1, len(train)), 100 * cer, 100 * acc,
               ", with the vocabulary %.1f%%" % (100 * vacc) if vocabulary and (epoch == args.epochs or epoch % 5 == 0) else "",
               time.time() - started))

    model.eval()
    example = torch.zeros((1, 50, FEATURES))
    # (the TorchScript exporter: the newer one bakes the example's length in)
    torch.onnx.export(model, example, args.out, input_names=["features"], output_names=["logits"],
                      dynamic_axes={"features": {1: "time"}, "logits": {1: "time"}}, opset_version=14, dynamo=False)
    with open(os.path.splitext(args.out)[0] + ".json", "w", encoding="utf-8") as f:
        json.dump({"alphabet": ["" if c is None else c for c in alphabet], "features": FEATURES, "step": STEP,
                   "format": "tools/arabic-ink/train_net.py"}, f, ensure_ascii=False, indent=1)
    log("wrote %s" % args.out)
    try:
        import onnxruntime as ort
        sess = ort.InferenceSession(args.out)
        word, feats = valid[0]
        out = sess.run(None, {"features": [feats]})[0]
        text = greedy(torch.tensor(out[0]), alphabet)
        log("onnxruntime reads the first validation word %r as %r" % (word, text))
    except ImportError:
        log("(onnxruntime not installed: the export was not checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
