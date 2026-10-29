#!/usr/bin/env python3
"""Fit the banner-title baseline curves (per card type) from the 512x renders and store them in
data/art/banner_curves.json. Needs the hi-res cache: fetch_renders.py --hires-all first.
Run only when the cache or the method changes; patch_renders.py reads the stored result."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import artlib as A  # noqa: E402
import banner as B  # noqa: E402


def main():
    ds = A.load_dataset()
    have = {f[:-4] for f in os.listdir(os.path.join(A.CACHE, "512"))}
    out = {}
    for typ, deg in (("MINION", 3), ("SPELL", 2)):
        entries = [(c["id"], c["name"]) for c in sorted(ds["cards"], key=lambda c: c["id"])
                   if c["type"] == typ and c["id"] in have and c["acquisition"] != "none"]
        cur, info = B.fit_curve(entries, deg)
        d = cur.to_json()
        d["fit"] = info
        out[typ] = d
        print(typ, info, [round(v, 7) for v in cur.c], cur.center_x, cur.letter_gap, cur.space_gap)
    A.dump_json(os.path.join(A.ART_DATA, "banner_curves.json"), out)


if __name__ == "__main__":
    main()
