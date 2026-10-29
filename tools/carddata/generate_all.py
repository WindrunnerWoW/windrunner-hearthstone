#!/usr/bin/env python3
"""Regenerate every output derived from data/cards/cards.normalized.json in one go.

Usage (from the module root):
    python tools/carddata/generate_all.py            # rewrite all outputs
    python tools/carddata/generate_all.py --check    # exit 1 if any output is stale

Outputs:
    src/HsDataset.h                                    (scripts/gen_dataset_header.py)
    data/sql/world/0001_hearthstone_card_template.sql  (gen_world_sql.py)
    addon/Hearthstone/CardDB.lua                       (gen_addon_carddb.py)

The dataset file itself is produced by import_cards.py; this script never touches it.
"""

from __future__ import annotations

import importlib.util
import sys

import gen_addon_carddb
import gen_world_sql
import gencommon as gc


def _load_header_generator():
    path = gc.REPO_ROOT / "scripts" / "gen_dataset_header.py"
    spec = importlib.util.spec_from_file_location("gen_dataset_header", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def outputs(ds: dict):
    """(label, path, text) for every generated file."""
    header = _load_header_generator()
    from pathlib import Path
    return [
        ("src/HsDataset.h", Path(header.OUT), header.render()),
        (gc.relpath(gen_world_sql.OUT_PATH), gen_world_sql.OUT_PATH, gen_world_sql.render(ds)),
        (gc.relpath(gen_addon_carddb.OUT_PATH), gen_addon_carddb.OUT_PATH, gen_addon_carddb.render(ds)),
    ]


def main(argv) -> int:
    check = "--check" in argv
    ds = gc.load()
    ok = True
    for label, path, text in outputs(ds):
        ok = gc.check_or_write(path, text, check, label) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(gc.main_wrapper(main))
