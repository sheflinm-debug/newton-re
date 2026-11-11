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

Words and phrases (2026-11-11, the audit's finding: tools/arabic-ink/audit.py):
the alphabet's last class is the space, so the network reads a line of
writing - several words - and puts the spaces itself; on the Newton the
gap between two words is no wider than the gaps inside a word (between its
pieces: إ | متحا | ن), and no rule on the strokes' boxes told them apart.
--phrases N adds N phrases made of two or three training samples set side
by side, right to left, a random gap apart (compose()); ADAB's names of two
words keep their space.  --pixels P draws a share P of the training
samples again at a size the Newton's screen gives (an ink height of 6 to
20 pixels) rounded to whole pixels, as the tablet hands them over (the
audit found the screen's grid cost a quarter of the user's words).
--init from a network trained before the space class takes its weights,
the new class's starting near zero.
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


def make_alphabet(space=True):
    chars = list(arabic.TO_CODE.keys())
    return [None] + chars + ([" "] if space else [])     # 0: the CTC blank; the space last


def ink_frame(strokes):
    """(ink height: the 10th to 90th percentile of y; base line: the 75th;
    the left and right edges)."""
    xs = [s[i] for s in strokes for i in range(0, len(s), 4)]
    ys = [s[i + 1] for s in strokes for i in range(0, len(s), 4)]
    return max(percentile(ys, 0.9) - percentile(ys, 0.1), 1.0), percentile(ys, 0.75), min(xs), max(xs)


def compose(samples, rng):
    """Samples [(word, strokes)] written one after another on one line,
    right to left, as a phrase: each scaled to an ink height of 100, base
    lines together, a gap of 0.3 to 1.3 ink heights between one word's
    left edge and the next one's right.  ==> (the words with spaces, the
    strokes)."""
    out = []
    right = 0.0
    for k, (word, strokes) in enumerate(samples):
        h, base, lo, hi = ink_frame(strokes)
        scale = 100.0 / h
        if k > 0:
            right -= rng.uniform(0.3, 1.3) * 100.0
        for st in strokes:
            ns = []
            for i in range(0, len(st), 4):
                ns += [right - (hi - st[i]) * scale, (st[i + 1] - base) * scale, 0, 0.5]
            out.append(ns)
        right -= (hi - lo) * scale
    return " ".join(w for w, _ in samples), out


def pixelate(strokes, rng):
    """The strokes as the Newton's screen would have them: scaled to an ink
    height of 6 to 20 pixels and rounded to whole pixels, a point that
    rounds onto the one before dropped."""
    h, base, lo, hi = ink_frame(strokes)
    scale = rng.uniform(6.0, 20.0) / h
    ox, oy = rng.random(), rng.random()
    out = []
    for st in strokes:
        ns = []
        for i in range(0, len(st), 4):
            x, y = round((st[i] - lo) * scale + ox), round((st[i + 1] - base) * scale + oy)
            if len(ns) >= 4 and ns[-4] == x and ns[-3] == y:
                continue
            ns += [x, y, 0, 0.5]
        out.append(ns)
    return out


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


def load_from_onnx(torch, model, path, hidden):
    """The weights of an exported network put back into the model (the
    exporter's reordering undone: ONNX's gates are z, r, h; PyTorch's r,
    z, n; ONNX's B is the input biases then the hidden ones)."""
    import onnx
    from onnx import numpy_helper
    m = onnx.load(path)
    inits = {i.name: torch.tensor(numpy_helper.to_array(i).copy()) for i in m.graph.initializer}
    matmuls = [n for n in m.graph.node if n.op_type == "MatMul"]
    grus = [n for n in m.graph.node if n.op_type == "GRU"]
    H = hidden
    with torch.no_grad():
        model.inp.weight.copy_(inits[matmuls[0].input[1]].t())
        model.inp.bias.copy_(inits["inp.bias"])
        ow, ob = inits[matmuls[1].input[1]].t(), inits["out.bias"]
        n = ow.shape[0]
        # (a network trained before the space class: its classes are the
        # first n; the new ones keep their fresh weights, scaled down)
        model.out.weight[:n].copy_(ow)
        model.out.bias[:n].copy_(ob)
        if n < model.out.weight.shape[0]:
            model.out.weight[n:].mul_(0.1)
            model.out.bias[n:].fill_(float(ob.mean()))
        for layer, gru in enumerate(grus):
            W, R, B = inits[gru.input[1]], inits[gru.input[2]], inits[gru.input[3]]
            for d, suffix in ((0, ""), (1, "_reverse")):
                z, r, h = W[d][:H], W[d][H:2 * H], W[d][2 * H:]
                getattr(model.gru, "weight_ih_l%d%s" % (layer, suffix)).copy_(torch.cat([r, z, h]))
                z, r, h = R[d][:H], R[d][H:2 * H], R[d][2 * H:]
                getattr(model.gru, "weight_hh_l%d%s" % (layer, suffix)).copy_(torch.cat([r, z, h]))
                wb, rb = B[d][:3 * H], B[d][3 * H:]
                getattr(model.gru, "bias_ih_l%d%s" % (layer, suffix)).copy_(torch.cat([wb[H:2 * H], wb[:H], wb[2 * H:]]))
                getattr(model.gru, "bias_hh_l%d%s" % (layer, suffix)).copy_(torch.cat([rb[H:2 * H], rb[:H], rb[2 * H:]]))


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
    ap.add_argument("--init", help="start from this trained .onnx (its weights put back into the network: fine-tuning)")
    ap.add_argument("--oversample", nargs=2, metavar=("DIR", "TIMES"), action="append", default=[],
                    help="a samples directory counted TIMES over in the training set (a writer's own words, say)")
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--phrases", type=int, default=0, help="this many phrases made of training samples (needs the space class)")
    ap.add_argument("--pixels", type=float, default=0.0, help="the share of training samples drawn at screen size in whole pixels")
    ap.add_argument("--no-space", action="store_true", help="the alphabet without the space class (the networks before 2026-11-11)")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    import torch
    torch.manual_seed(args.seed)
    torch.set_num_threads(os.cpu_count() or 4)
    random.seed(args.seed)

    alphabet = make_alphabet(not args.no_space)
    index = {ch: i for i, ch in enumerate(alphabet) if ch is not None}
    space = " " in index

    def usable(word):
        return all(ch in index or ch == " " for ch in word)

    def raw(dirs):
        return [(w if space else w.replace(" ", ""), st) for d in dirs for w, st in load_dir(d) if usable(w)]

    def feats_of(samples, pixels=0.0, rng=None):
        out = []
        for word, strokes in samples:
            if pixels and rng.random() < pixels:
                strokes = pixelate(strokes, rng)
            f = featurize(strokes)
            if f is None or len(f) < 2 * len(word) + 1:
                continue
            out.append((word, f))
        return out

    def phrases(samples, n, rng):
        return [compose(rng.sample(samples, 2 if rng.random() < 0.7 else 3), rng) for _ in range(n)]

    rng = random.Random(args.seed)
    train_raw = raw(args.train)
    random.shuffle(train_raw)
    if args.limit:
        train_raw = train_raw[:args.limit]
    own = [(d, raw([d]), int(times)) for d, times in args.oversample]
    made = phrases(train_raw + [x for _, e, _ in own for x in e], args.phrases, rng) if space and args.phrases else []
    train = feats_of(train_raw + made, args.pixels, rng)
    for d, extra, times in own:
        for _ in range(times):
            train += feats_of(extra, args.pixels, rng)
        log("%s: %d samples, counted %d times" % (d, len(extra), times))
    if made:
        log("%d phrases made of two or three training samples" % len(made))
    valid_raw = raw(args.validate)
    random.shuffle(valid_raw)
    valid = feats_of(valid_raw)
    valid_phrases = valid_pixels = []
    if space:
        vrng = random.Random(7)
        valid_phrases = feats_of(phrases(valid_raw, max(50, args.validate_limit // 5), vrng))
        valid_pixels = feats_of(valid_raw[:args.validate_limit], 1.0, vrng)
    vocabulary = None
    if args.vocabulary:
        vocabulary = sorted(set(w if space else w.replace(" ", "") for w, _ in load_dir(args.vocabulary)))
    log("%d training samples, %d validation, %d classes%s" % (len(train), len(valid), len(alphabet),
        ", vocabulary of %d words" % len(vocabulary) if vocabulary else ""))

    model = build_model(torch, args.hidden, len(alphabet))
    if args.init:
        load_from_onnx(torch, model, args.init, args.hidden)
        cer, acc, _ = evaluate(torch, model, valid, alphabet, index, None, args.validate_limit)
        log("from %s: validation letters wrong %.1f%%, words right %.1f%%" % (args.init, 100 * cer, 100 * acc))
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
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
        if valid_phrases:
            pcer, pacc, _ = evaluate(torch, model, valid_phrases, alphabet, index)
            xcer, xacc, _ = evaluate(torch, model, valid_pixels, alphabet, index)
            log("          phrases: letters wrong %.1f%%, right %.1f%%; at screen size: letters wrong %.1f%%, words right %.1f%%"
                % (100 * pcer, 100 * pacc, 100 * xcer, 100 * xacc))
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
