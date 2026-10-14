#!/usr/bin/env python3
"""Write a trained Arabic network in the plain form the Newton host reads
(recognition/ArabicNet.h), and a sample to check the host's reading with.

Purpose:  the host runs the network in its own C++ with no library (the
          same on every host, a phone included), from one binary file.
Inputs:   train_net.py's .onnx (and its .json beside it); a samples
          directory for the check sample.
Outputs:  <model>.anet - the network: a header, the alphabet, the weights
          (below); and, with --sample, <model>.sample.bin - one sample's
          strokes, the features train_net.py makes of them, and the logits
          onnxruntime gives, which ctest recognition.ArabicNet holds the
          C++ featurizer and network to.
Usage:    python tools/arabic-ink/export_weights.py build/arabic-ink/arabic-net.onnx
              [--sample build/arabic-ink/samples]
          (the venv's python: build/venv/Scripts/python)

The .anet file, little-endian throughout:
  "ANET", u32 version (1), u32 features (6), u32 hidden (128),
  u32 classes, f32 step, then classes x u16: each class's character as a
  UTF-16 code unit (0 for class 0, the CTC blank); then the arrays, each
  float32, in this order and these shapes (row-major, as ONNX holds them):
    inp.W [features, hidden]  inp.b [hidden]
    gru1.W [2, 3*hidden, hidden]  gru1.R [2, 3*hidden, hidden]  gru1.B [2, 6*hidden]
    gru2.W [2, 3*hidden, 2*hidden]  gru2.R [2, 3*hidden, hidden]  gru2.B [2, 6*hidden]
    out.W [2*hidden, classes]  out.b [classes]
The network, as train_net.py builds it and ONNX holds it:
  inp:   x [T, 6] -> relu(x @ inp.W + inp.b)
  gru1:  bidirectional over that; gru2: over gru1's [T, 256].  ONNX's GRU
         with linear_before_reset, gates in the order z, r, h:
           z = sigmoid(Wz x + Rz h + Wbz + Rbz)
           r = sigmoid(Wr x + Rr h + Wbr + Rbr)
           n = tanh(Wh x + Wbh + r * (Rh h + Rbh))
           h' = (1 - z) * n + z * h
         direction 0 runs forward, 1 backward; B is the W biases then
         the R biases.
  out:   [T, 256] @ out.W + out.b -> logits; class 0 is the CTC blank.
The .sample.bin: u32 strokes, then per stroke u32 points and points x
(f32 x, f32 y); u32 T, u32 features, T x features f32; u32 T, u32
classes, T x classes f32 (the logits); u32 n, n x u16 (the reading).
"""

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import train_net as T   # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("model")
    ap.add_argument("--sample", help="a samples directory: its first sample written for the C++ test")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    import onnx
    import numpy as np
    from onnx import numpy_helper
    m = onnx.load(args.model)
    inits = {i.name: numpy_helper.to_array(i) for i in m.graph.initializer}
    matmuls = [n for n in m.graph.node if n.op_type == "MatMul"]
    grus = [n for n in m.graph.node if n.op_type == "GRU"]
    if len(matmuls) != 2 or len(grus) != 2:
        print("not the network train_net.py builds (%d MatMul, %d GRU)" % (len(matmuls), len(grus)), file=sys.stderr)
        return 1
    arrays = [
        ("inp.W", inits[matmuls[0].input[1]]), ("inp.b", inits["inp.bias"]),
        ("gru1.W", inits[grus[0].input[1]]), ("gru1.R", inits[grus[0].input[2]]), ("gru1.B", inits[grus[0].input[3]]),
        ("gru2.W", inits[grus[1].input[1]]), ("gru2.R", inits[grus[1].input[2]]), ("gru2.B", inits[grus[1].input[3]]),
        ("out.W", inits[matmuls[1].input[1]]), ("out.b", inits["out.bias"]),
    ]
    base = os.path.splitext(args.model)[0]
    with open(base + ".json", encoding="utf-8") as f:
        meta = json.load(f)
    features = int(arrays[0][1].shape[0])
    hidden = int(arrays[0][1].shape[1])
    classes = int(arrays[-1][1].shape[0])
    alphabet = meta["alphabet"]
    assert len(alphabet) == classes and features == meta["features"]
    with open(base + ".anet", "wb") as f:
        f.write(b"ANET")
        f.write(struct.pack("<IIIIf", 1, features, hidden, classes, float(meta["step"])))
        for ch in alphabet:
            f.write(struct.pack("<H", ord(ch) if ch else 0))
        total = 0
        for name, a in arrays:
            a = np.ascontiguousarray(a.astype("<f4"))
            f.write(a.tobytes())
            total += a.size
    print("wrote %s.anet: features %d, hidden %d, classes %d, %d weights (%.1f MB)" % (base, features, hidden, classes, total, total * 4 / 1e6))
    if args.sample:
        import onnxruntime as ort
        import torch
        T.STEP = meta["step"]
        word, strokes = T.load_dir(args.sample)[0]
        feats = T.featurize(strokes)
        logits = ort.InferenceSession(args.model).run(None, {"features": [feats]})[0][0]
        text = T.greedy(torch.tensor(logits), [c or None for c in alphabet])
        with open(base + ".sample.bin", "wb") as f:
            f.write(struct.pack("<I", len(strokes)))
            for s in strokes:
                pts = [(s[i], s[i + 1]) for i in range(0, len(s), 4)]
                f.write(struct.pack("<I", len(pts)))
                for x, y in pts:
                    f.write(struct.pack("<ff", x, y))
            f.write(struct.pack("<II", len(feats), features))
            for row in feats:
                f.write(struct.pack("<%df" % features, *row))
            f.write(struct.pack("<II", len(logits), classes))
            for row in logits:
                f.write(struct.pack("<%df" % classes, *[float(x) for x in row]))
            f.write(struct.pack("<I", len(text)))
            for ch in text:
                f.write(struct.pack("<H", ord(ch)))
        print("wrote %s.sample.bin: %r read as %r, %d strokes, %d steps" % (base, word, text, len(strokes), len(feats)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
