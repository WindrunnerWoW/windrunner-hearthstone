#!/usr/bin/env python3
"""Deterministic importer: pinned Battlegrounds source (patch 24.2, build 148526) -> bg.normalized.json.

Usage:
    python -I tools/battlegrounds/import_bg.py            # write data/battlegrounds/bg.normalized.json
    python -I tools/battlegrounds/import_bg.py --check    # build in memory, fail if the files on disk differ
    python -I tools/battlegrounds/import_bg.py --out X    # write the dataset somewhere else

The output contains no timestamps or machine-specific data, so running the
importer twice yields byte-identical files.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bglib  # noqa: E402


def print_summary(dataset: dict, analysis: dict) -> None:
    cards = dataset["cards"]
    print(f"dataset_id     : {dataset['dataset_id']}")
    print(f"source build   : {dataset['source']['hearthstonejson_build']} (patch {dataset['source']['game_patch']})")
    print(f"source entities: {analysis['source_entity_count']}")
    print(f"cards imported : {len(cards)}")
    print(f"content_hash   : {dataset['content_hash']}")
    print(f"source sha256  : {dataset['source']['sha256']}")
    for role in bglib.ROLES:
        sel = [c for c in cards if c["role"] == role]
        print(f"  {role:12s} {len(sel):4d}   slice 15A: {sum(1 for c in sel if c['slice'])}")
    print(f"unresolved links: {len(analysis['unresolved'])}; heroes without hero power: "
          f"{len(analysis['heroes_without_power'])}; id-guess disagreements: {len(analysis['power_crosscheck'])}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=bglib.NORMALIZED_PATH, help="output path of the dataset")
    ap.add_argument("--check", action="store_true",
                    help="do not write; exit 1 if the existing output differs from a fresh build")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    try:
        dataset, analysis = bglib.build_dataset()
    except bglib.BgDataError as exc:
        print(f"IMPORT FAILED: {exc}", file=sys.stderr)
        return 2

    if not args.quiet:
        print_summary(dataset, analysis)

    text = bglib.dumps_dataset(dataset)
    if args.check:
        bad = []
        if not args.out.is_file() or args.out.read_bytes() != text.encode("utf-8"):
            bad.append(args.out)
        if bad:
            print("CHECK FAILED: missing or different from a fresh build: " + ", ".join(str(p) for p in bad),
                  file=sys.stderr)
            return 1
        print(f"check ok: {args.out} matches a fresh build")
        return 0

    bglib.write_text(args.out, text)
    print(f"wrote {args.out} ({len(text.encode('utf-8'))} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
