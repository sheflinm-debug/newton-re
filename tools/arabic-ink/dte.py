#!/usr/bin/env python3
"""Read and write ParaGraph's letter table (DTEMain) as JSON.

Purpose:  the cursive reader matches writing against DTEMain, a table
          of how each letter may be written.  This decodes it into JSON a
          person or a tool can read and change, and encodes JSON back into
          the binary, so an Arabic table can be built and the ROM's own
          table studied.  `check` proves the two agree: the ROM's table
          decoded and encoded again must come back byte for byte.
Inputs:   a DTEMain binary - the ROM's is romsrc/resources/DTEM/5c73ad.bin
          (the charsetInfoResources frame's DTEMain) - or a JSON file this
          tool wrote.
Outputs:  decode: JSON (stdout or -o); encode: a DTEMain binary (-o);
          show: one letter's variants and prototypes as text; check: a
          line saying whether the round trip is exact (exit 1 if not).
Usage:    python tools/arabic-ink/dte.py check [romsrc/resources/DTEM/5c73ad.bin]
          python tools/arabic-ink/dte.py decode BIN -o table.json
          python tools/arabic-ink/dte.py encode table.json -o DTEMain.bin
          python tools/arabic-ink/dte.py show BIN a

The layout (established from XrMatrix.cpp's CountSym and XrMatch,
ParaGraph.cpp's GetSymDescriptor, and checked by `check`: every
descriptor decoded and the table rebuilt from them is the ROM's, byte for
byte).  Everything is big-endian.

  +0           a word (0x0109b8d8 in the US ROM; the engine skips it)
  +4           256 words: for each character code, the offset from +4 of
               its descriptor, or 0 for none.  The US ROM has 91, codes
               0x20-0xa1.
  descriptor   +0x00  the number of variants (at most 16)
               +0x01  a byte (0x2a for a, 0x09 for t: not yet known)
               +0x02  a character code: the letter's own, except for 8 of
                      the US table's 91 (space -> ?, _ -> -, 0x80-0x83 and
                      0xa0-0xa1 -> v): perhaps the letter it is drawn like
               +0x03  a byte (0 in the US table)
               +0x04  16 bytes: each variant's length in prototypes (<= 12)
               +0x14  16 bytes: each variant's default weight, its vex, in
                      bits 0-2 (7: not used); the learning info starts
                      from these
               +0x24  16 bytes: each variant's group (bits 1-3) and the
                      letter sets it is in (bits 4-7, one per style; the
                      matcher wants style << 4 to meet them); bit 0 set
                      keeps it out when the matcher's flag 0x20 is
               +0x34  16 bytes, then +0x44 16 more: a nibble pair per
                      variant (a: 0x78 and 0x57, t: 0xbd and 0xce) - by
                      their values the height zones the variant reaches;
                      not yet traced to the code that reads them
               +0x54  the prototypes, the variants' one after another,
                      0x4c bytes each
  prototype    +0x00  two bytes (not yet known)
               +0x02  flags: 0x80, it reads only an xr next to a break
               +0x03  what skipping it costs
               +0x04  64 nibbles: what each xr type is worth (0: it cannot
                      read that type at all)
               +0x24  16 nibbles: each height
               +0x2c  16 nibbles: each shift
               +0x34  16 nibbles: each link
               +0x3c  32 nibbles: each orientation
               An xr read against it scores the five nibbles its type,
               height, shift, link and orientation pick, summed; reading
               it costs 50, so a prototype pays its way at a sum over 50.

The descriptors are written in the order the ROM's are: by their offset,
which is by character code.  Fields not yet understood are kept as they
are, so a table edited through JSON changes only what the edit changed.
"""

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ROM_DTEMAIN = os.path.join(REPO, "romsrc", "resources", "DTEM", "5c73ad.bin")

XRP_SIZE = 0x4c
DESC_HEAD = 0x54
MAX_VARIANTS = 16
MAX_PROTOTYPES = 12

# the prototype's nibble tables: name, offset, count
NIBBLE_TABLES = [("type", 0x04, 64), ("height", 0x24, 16), ("shift", 0x2c, 16), ("link", 0x34, 16), ("orient", 0x3c, 32)]


def nibbles(data, count):
    out = []
    for i in range(count):
        b = data[i >> 1]
        out.append(b >> 4 if (i & 1) == 0 else b & 0xf)
    return out


def pack_nibbles(values):
    if len(values) % 2:
        raise ValueError("an odd number of nibbles")
    out = bytearray()
    for i in range(0, len(values), 2):
        hi, lo = values[i], values[i + 1]
        if not (0 <= hi <= 15 and 0 <= lo <= 15):
            raise ValueError("a nibble out of range: %r" % ((hi, lo),))
        out.append((hi << 4) | lo)
    return bytes(out)


def decode_prototype(p):
    proto = {"head": p[0:2].hex(), "breakOnly": bool(p[2] & 0x80), "flags": p[2], "skipCost": p[3]}
    for name, off, count in NIBBLE_TABLES:
        proto[name] = nibbles(p[off:off + count // 2], count)
    return proto


def encode_prototype(proto):
    p = bytearray(XRP_SIZE)
    p[0:2] = bytes.fromhex(proto["head"])
    p[2] = proto["flags"] | (0x80 if proto.get("breakOnly") else 0)
    p[3] = proto["skipCost"]
    for name, off, count in NIBBLE_TABLES:
        values = proto[name]
        if len(values) != count:
            raise ValueError("prototype table %s has %d values, wants %d" % (name, len(values), count))
        p[off:off + count // 2] = pack_nibbles(values)
    return bytes(p)


def decode(binary):
    lead = binary[:4]
    m = binary[4:]
    offsets = struct.unpack(">256I", m[:1024])
    letters = []
    for code in sorted(range(256), key=lambda c: offsets[c]):
        off = offsets[code]
        if off == 0:
            continue
        d = m[off:]
        n = d[0]
        lens = list(d[4:4 + MAX_VARIANTS])
        variants = []
        at = DESC_HEAD
        for v in range(MAX_VARIANTS):
            protos = [decode_prototype(d[at + k * XRP_SIZE: at + (k + 1) * XRP_SIZE]) for k in range(lens[v])]
            at += lens[v] * XRP_SIZE
            variants.append({"vexByte": d[0x14 + v], "groups": d[0x24 + v],
                             "zoneA": d[0x34 + v], "zoneB": d[0x44 + v], "prototypes": protos})
        # the unused slots after the last real variant are kept only if they hold anything
        while len(variants) > n and not variants[-1]["prototypes"] and not any(
                variants[-1][k] for k in ("vexByte", "groups", "zoneA", "zoneB")):
            variants.pop()
        letters.append({"code": code, "char": chr(code) if 0x20 <= code < 0x7f else None,
                        "count": n, "byte1": d[1], "byte2": d[2], "byte3": d[3], "variants": variants})
    return {"format": "ParaGraph DTEMain, decoded by tools/arabic-ink/dte.py",
            "lead": lead.hex(), "tableEnd": len(m), "letters": letters}


def encode(table):
    letters = table["letters"]
    body = bytearray()
    offsets = [0] * 256
    at = 1024
    for letter in letters:
        variants = letter["variants"]
        if len(variants) > MAX_VARIANTS:
            raise ValueError("letter %#x has %d variants (at most %d)" % (letter["code"], len(variants), MAX_VARIANTS))
        d = bytearray(DESC_HEAD)
        d[0] = letter["count"]
        d[1] = letter.get("byte1", 0)
        d[2] = letter.get("byte2", letter["code"])
        d[3] = letter.get("byte3", 0)
        protos = bytearray()
        for v, var in enumerate(variants):
            if len(var["prototypes"]) > MAX_PROTOTYPES:
                raise ValueError("letter %#x variant %d has %d prototypes (at most %d)" % (letter["code"], v, len(var["prototypes"]), MAX_PROTOTYPES))
            d[0x04 + v] = len(var["prototypes"])
            d[0x14 + v] = var["vexByte"]
            d[0x24 + v] = var["groups"]
            d[0x34 + v] = var["zoneA"]
            d[0x44 + v] = var["zoneB"]
            for proto in var["prototypes"]:
                protos += encode_prototype(proto)
        offsets[letter["code"]] = at
        body += d + protos
        at += len(d) + len(protos)
    m = struct.pack(">256I", *offsets) + bytes(body)
    return bytes.fromhex(table.get("lead", "00000000")) + m


def show(table, which):
    code = ord(which) if len(which) == 1 else int(which, 0)
    for letter in table["letters"]:
        if letter["code"] != code:
            continue
        print("code %#04x %r: %d variants (byte1 %#04x)" % (code, letter["char"], letter["count"], letter["byte1"]))
        for v, var in enumerate(letter["variants"][:letter["count"]]):
            print("  variant %d: vex %d, groups %#04x, zones %#04x %#04x, %d prototypes"
                  % (v, var["vexByte"] & 7, var["groups"], var["zoneA"], var["zoneB"], len(var["prototypes"])))
            for k, p in enumerate(var["prototypes"]):
                types = ", ".join("%#04x:%d" % (t, w) for t, w in enumerate(p["type"]) if w)
                print("    %2d skip %3d%s  types {%s}" % (k, p["skipCost"], " break-only" if p["breakOnly"] else "", types))
        return 0
    print("no letter %r in the table" % which, file=sys.stderr)
    return 1


def load_binary(path):
    with open(path, "rb") as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check"); c.add_argument("binary", nargs="?", default=ROM_DTEMAIN)
    d = sub.add_parser("decode"); d.add_argument("binary"); d.add_argument("-o", "--out")
    e = sub.add_parser("encode"); e.add_argument("json"); e.add_argument("-o", "--out", required=True)
    s = sub.add_parser("show"); s.add_argument("binary"); s.add_argument("letter")
    args = ap.parse_args()

    if args.cmd == "check":
        original = load_binary(args.binary)
        table = decode(original)
        again = encode(json.loads(json.dumps(table)))
        n = sum(len(v["prototypes"]) for l in table["letters"] for v in l["variants"])
        if again == original:
            print("round trip exact: %d letters, %d prototypes, %d bytes" % (len(table["letters"]), n, len(original)))
            return 0
        first = next((i for i, (a, b) in enumerate(zip(again, original)) if a != b), min(len(again), len(original)))
        print("round trip differs: %d bytes against %d, first difference at %#x" % (len(again), len(original), first))
        return 1
    if args.cmd == "decode":
        text = json.dumps(decode(load_binary(args.binary)), ensure_ascii=False, indent=1)
        if args.out:
            with open(args.out, "w", encoding="utf-8") as f:
                f.write(text)
        else:
            print(text)
        return 0
    if args.cmd == "encode":
        with open(args.json, encoding="utf-8") as f:
            data = encode(json.load(f))
        with open(args.out, "wb") as f:
            f.write(data)
        print("wrote %s: %d bytes" % (args.out, len(data)))
        return 0
    if args.cmd == "show":
        return show(decode(load_binary(args.binary)), args.letter)


if __name__ == "__main__":
    sys.exit(main())
