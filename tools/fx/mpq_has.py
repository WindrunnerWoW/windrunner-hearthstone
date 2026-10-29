#!/usr/bin/env python3
"""Checks whether files exist in the WoW 1.12 client's MPQ archives (hash table lookup only, nothing is extracted).

Used to see that the model / sound paths of data/fx/effect_profiles.json point at real client files before the owner
looks at them in game:

    python tools/fx/mpq_has.py "Spells\\Frostbolt.mdx" "Sound\\Spells\\Swipe.wav"   paths (a .mdx is looked up as .m2)
    python tools/fx/mpq_has.py --manifest            check every preset model and every sound of the manifest
    python tools/fx/mpq_has.py --manifest --update   also rewrite the "exists" flags of the presets in the manifest
                                                     (then run tools/fx/gen_effect_profiles.py)

The archives are read from --data (default C:/TurtleDev/04_client/game/Data). "exists" only says the file is in the
client data; "verified" (looks right in game) is set by hand after trying the preset with /hs fx.
"""
import argparse
import glob
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(ROOT, "data", "fx", "effect_profiles.json")
DEFAULT_DATA = "C:/TurtleDev/04_client/game/Data"


def _crypt_table():
    t = [0] * 0x500
    seed = 0x00100001
    for i in range(0x100):
        idx = i
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            a = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            t[idx] = a | (seed & 0xFFFF)
            idx += 0x100
    return t


T = _crypt_table()


def mpq_hash(s, kind):
    s1, s2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in s.upper().replace("/", "\\"):
        c = ord(ch)
        s1 = (T[kind * 0x100 + c] ^ (s1 + s2)) & 0xFFFFFFFF
        s2 = (c + s1 + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return s1


def decrypt(data, key):
    out = []
    seed = 0xEEEEEEEE
    for (v,) in struct.iter_unpack("<I", data):
        seed = (seed + T[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        d = (v ^ (key + seed)) & 0xFFFFFFFF
        out.append(d)
        key = (((~key << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (d + seed + (seed << 5) + 3) & 0xFFFFFFFF
    return out


def load_hash_table(path):
    with open(path, "rb") as f:
        data = f.read()
    off = data.find(b"MPQ\x1a")
    hdr = struct.unpack_from("<4sIIHHIIII", data, off)
    htpos, htsize = hdr[5], hdr[7]
    raw = data[off + htpos: off + htpos + htsize * 16]
    ent = decrypt(raw, mpq_hash("(hash table)", 3))
    table = {}
    for i in range(htsize):
        a, b, _lp, block = ent[i * 4:i * 4 + 4]
        if block < 0xFFFFFFFE:
            table[(a, b)] = True
    return table


class Client:
    def __init__(self, data_dir):
        self.tables = {}
        for a in sorted(glob.glob(os.path.join(data_dir, "*.[mM][pP][qQ]"))):
            try:
                self.tables[os.path.basename(a)] = load_hash_table(a)
            except Exception as e:  # a damaged archive does not stop the check
                print("skip %s: %s" % (a, e), file=sys.stderr)
        if not self.tables:
            raise SystemExit("no MPQ archives found in %s (use --data)" % data_dir)

    def where(self, name):
        """The archives that hold the file; a .mdx is looked up under its stored name, .m2."""
        if name.lower().endswith(".mdx"):
            name = name[:-4] + ".m2"
        key = (mpq_hash(name, 1), mpq_hash(name, 2))
        return [n for n, t in self.tables.items() if key in t]


def check_manifest(client, update):
    with open(MANIFEST, "r", encoding="utf-8") as f:
        text = f.read()
    doc = json.loads(text)
    bad = 0
    found = {}
    for p in doc["presets"]:
        model = client.where(p["model"])
        snd = client.where(p["sound"]) if p.get("sound") else None
        found[p["key"]] = bool(model)
        print("%-18s model %-4s %s%s" % (p["key"], "YES" if model else "NO", p["model"],
                                         "" if snd is None else ("   sound %s %s" % ("YES" if snd else "NO", p["sound"]))))
        if not model or snd == []:
            bad += 1
    for key, f in doc["families"].items():
        if f.get("sound") and not client.where(f["sound"]):
            print("family %s: sound NO %s" % (key, f["sound"]))
            bad += 1
    for cid, c in doc["cards"].items():
        if c.get("sound") and not client.where(c["sound"]):
            print("card %s: sound NO %s" % (cid, c["sound"]))
            bad += 1
    if update:
        out = []
        for line in text.split("\n"):
            m = re.search(r'"key": "([a-z0-9_]+)"', line)
            if m and m.group(1) in found and '"exists"' in line:
                line = re.sub(r'"exists": (true|false)', '"exists": %s' % ("true" if found[m.group(1)] else "false"), line)
            out.append(line)
        with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out))
        print("manifest updated; run tools/fx/gen_effect_profiles.py")
    return 1 if bad else 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--data", default=DEFAULT_DATA)
    ap.add_argument("--manifest", action="store_true")
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args(argv)
    client = Client(args.data)
    if args.manifest:
        return check_manifest(client, args.update)
    missing = 0
    for name in args.paths:
        hits = client.where(name)
        print(("YES " if hits else "no  ") + name + ("  " + ",".join(hits) if hits else ""))
        if not hits:
            missing += 1
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
