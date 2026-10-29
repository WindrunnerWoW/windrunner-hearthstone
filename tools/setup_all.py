#!/usr/bin/env python3
"""One command to rebuild everything that is not in git (card data, card and board art).

  python tools/setup_all.py               # card data, then the artwork chain (downloads, needs a network)
  python tools/setup_all.py --cards-only  # card data only (no art downloads)

Steps, each run by its own script (see the READMEs of tools/carddata and tools/art):
  1. tools/carddata/fetch_sources.py   download the pinned Hearthstone sources, check their sha256
  2. tools/carddata/import_cards.py    build data/cards/cards.normalized.json
  3. tools/carddata/validate_cards.py  stop here on a hard error
  4. tools/carddata/generate_all.py    src/HsDataset.h, the world SQL template, addon/Hearthstone/CardDB.lua
  5. the Battlegrounds pipeline (tools/battlegrounds): fetch the pinned 24.2 sources, import bg.normalized.json,
     then gen_bg_cpp.py (src/battlegrounds/HsBgCardData.inc) and gen_bg_lua.py (addon BgCardDB.lua)
  6. tools/art/setup_art.py            venv, card renders, patches, card / minion / Battlegrounds / power / hero textures

Afterwards tools/deploy/deploy-addon.ps1 copies the addon and textures into a WoW client.
Run from anywhere; paths resolve against the module root. Standard library only.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

CARD_STEPS = [
    ["tools/carddata/fetch_sources.py"],
    ["tools/carddata/import_cards.py"],
    ["tools/carddata/validate_cards.py"],
    ["tools/carddata/generate_all.py"],
]
# The Battlegrounds dataset is a separate pipeline (its own pinned build); its generated files are not in git either.
BG_STEPS = [
    ["tools/battlegrounds/fetch_bg_source.py"],
    ["tools/battlegrounds/import_bg.py"],
    ["tools/battlegrounds/gen_bg_cpp.py"],
    ["tools/battlegrounds/gen_bg_lua.py"],
]
ART_STEP = ["tools/art/setup_art.py"]


def run(script_args):
    print("\n$ python " + " ".join(script_args), flush=True)
    subprocess.run([sys.executable] + script_args, cwd=ROOT, check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--cards-only", action="store_true", help="skip the artwork chain")
    args = ap.parse_args()
    steps = CARD_STEPS + BG_STEPS if args.cards_only else CARD_STEPS + BG_STEPS + [ART_STEP]
    for step in steps:
        run(step)
    print("\nSetup complete. Next: tools/deploy/deploy-addon.ps1 (WoW client), or build the server.")


if __name__ == "__main__":
    sys.exit(main())
