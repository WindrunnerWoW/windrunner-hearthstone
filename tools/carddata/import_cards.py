#!/usr/bin/env python3
"""Deterministic importer: pinned Hearthstone 6024 sources -> cards.normalized.json.

Usage:
    python tools/carddata/import_cards.py            # write data/cards/cards.normalized.json
    python tools/carddata/import_cards.py --check    # build in memory, fail if the file on disk differs
    python tools/carddata/import_cards.py --out X    # write somewhere else

The output contains no timestamps or machine-specific data, so running the
importer twice yields byte-identical files.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cardlib  # noqa: E402


def print_report(dataset: dict, report: dict) -> None:
    cards = dataset["cards"]
    print(f"dataset_id     : {dataset['dataset_id']}")
    print(f"source build   : {dataset['source']['client_build']} ({dataset['source']['patch_date']})")
    print(f"source entities: {report['source_entity_count']}")
    print(f"cards imported : {len(cards)}")
    print(f"content_hash   : {dataset['content_hash']}")
    print("source sha256  :")
    for key, info in dataset["source"]["files"].items():
        print(f"  {info['file']}  {info['sha256']}")
    print(f"overrides      : {len(dataset['overrides_applied'])} applied")
    print(f"custom files   : {len(dataset['custom_files'])}")
    print("excluded entities by set (not CORE/EXPERT1/NAXX, not reward-only, not referenced):")
    for s, ids in report["excluded_by_set"].items():
        print(f"  {s:9s} {len(ids):4d}")
    if report["excluded_collectible"]:
        print("  excluded but flagged collectible in the source (promo/reward cards):")
        for cid in report["excluded_collectible"]:
            print(f"    {cid}  {report['excluded_names'][cid]}")
    print("reward_only cards imported (kept out of packs and crafting):")
    for cid, label in report["reward_only"].items():
        print(f"  {cid}  {label}")
    print(f"quest cards imported (collectible Naxxramas; out of packs, crafting, trader and arena): {len(report['quest'])}")
    print(f"runtime entities (acquisition none) by set: {report['runtime_by_set']}")
    if report["kept_by_reference"]:
        print(f"kept only because a kept card references them: {report['kept_by_reference']}")
    else:
        print("kept by reference from outside CORE/EXPERT1: none")
    print(f"cards without a dbf_id (not in manifest): {len(report['missing_dbf'])}: {report['missing_dbf']}")
    print(f"manifest ids absent from cards.json: {report['manifest_ids_not_in_json']}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=cardlib.NORMALIZED_PATH, help="output path")
    ap.add_argument("--check", action="store_true",
                    help="do not write; exit 1 if the existing output differs from a fresh build")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    try:
        dataset, report = cardlib.build_dataset()
    except cardlib.CardDataError as exc:
        print(f"IMPORT FAILED: {exc}", file=sys.stderr)
        return 2

    if not args.quiet:
        print_report(dataset, report)

    text = cardlib.dumps_dataset(dataset)
    if args.check:
        if not args.out.is_file() or args.out.read_bytes() != text.encode("utf-8"):
            print(f"CHECK FAILED: {args.out} is missing or differs from a fresh build", file=sys.stderr)
            return 1
        print(f"check ok: {args.out} matches a fresh build")
        return 0

    cardlib.write_dataset(dataset, args.out)
    print(f"wrote {args.out} ({len(text.encode('utf-8'))} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
